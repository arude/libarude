///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// Documents: one blob, one endpoint instance, one transport instance, and any
/// number of independently typed and independently versioned sections.
///
/// A registration binds all of it together, so sections sharing one file are
/// read in one parse and written in one emit — no read-modify-write per
/// section, no lost updates, and a root section whose upgrade can move fields
/// between sections.
///
/// Two rules keep a document safe, and both are the same idea — only write
/// what you own:
///
///   - The parsed tree is retained, and store() starts from it, so a table
///     nobody registered survives untouched.
///   - A section holds std::optional<T>. Disengaged — never loaded, never
///     set — means its subtree is left exactly as parsed, not written from
///     defaults. A section added after a load cannot clobber a value already
///     in the file, which is what makes adding one after a load allowed.
///
/// load() never writes, on any policy. load_policy::create is a decision about
/// the cache alone: a source that is not there leaves every section at its
/// defaults, and the source appears when the application calls store(). That
/// keeps the two rules above from contradicting each other — a store() from
/// inside load(), before any section was engaged, would emit a document with
/// every section skipped.
///
/// load() is all or nothing. Every section is read before any is changed, so
/// a load that fails — for one section or for the whole source — leaves every
/// section and the retained tree as they were.
///
/// Locking: one shared_mutex per document, not per section — sections cannot
/// be locked independently when a write serializes them all together. The I/O
/// and the parsing happen outside that lock, on a snapshot of the sections
/// taken under it, and the lock is taken only to swap the finished values in,
/// or a slow fetch would block every get() on that document for the length of
/// the request. A load keeps what it read to itself until that swap, and of
/// two loads under way at once the one that started later wins: the other
/// installs nothing once it has, so the cache never holds a mix of two reads.
/// A section attached while a load is under way is not part of that load and
/// stays disengaged. The mutex guards the cache and nothing else: it is not
/// held across I/O, so a read overlapping a write of the same source is the
/// transport's to make safe, as transport_file does by replacing the file in
/// one step. There is no cross-process locking either: two processes writing
/// one document still race, and the last write wins.
///
#pragma once

#include "arude/config/config_handle.hpp"
#include "arude/config/endpoint.hpp"
#include "arude/config/load_policy.hpp"
#include "arude/config/migration.hpp"
#include "arude/config/transport.hpp"
#include "arude/config/type_registry.hpp"
#include "arude/exception_report.hpp"
#include "arude/type_key.hpp"

#include <rfl/Generic.hpp>

#include <cstddef>
#include <cstdint>
#include <flat_set>
#include <format>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace arude::config::detail
{

class document_base;
class section_base;

///
/// A section of a document with its type erased.
/// What a document holds and iterates over; section<T> below is the typed
/// implementation, and config_handle<T> is what a caller sees.
///
class section_base
{
public:
  ///
  /// Constructs the base. The class is abstract, so only a concrete section can.
  ///
  section_base() = default;

  ///
  /// Destroys the section. Virtual, because a document owns sections through
  /// pointers to this class and deletes them through it.
  ///
  virtual ~section_base() = default;

  // A section is one place, not a value to duplicate: copying one would fork
  // its cache.
  section_base(const section_base&) = delete;
  section_base(section_base&&) = delete;
  auto operator=(const section_base&) -> section_base& = delete;
  auto operator=(section_base&&) -> section_base& = delete;

  ///
  /// Returns the section's name, empty for the root section.
  /// \return The name.
  ///
  [[nodiscard]] virtual auto name() const -> std::string_view = 0;

  ///
  /// Prepares a load of the section, and changes nothing yet.
  /// Reads the section out of a parsed document and hands back the step that
  /// installs what was read. The section itself is untouched until that step
  /// runs: the document gathers one from every section first, so a load in
  /// which any section fails changes none of them, a slow parse never blocks
  /// a get(), and two loads under way at once each hold steps of their own
  /// rather than sharing a place to put a half-finished result.
  ///
  /// \param document The parsed document. For the root section the document
  ///                  has every registered subsection already stripped.
  /// \param policy What to do about a differently versioned section.
  /// \return The step that installs the value read. The document calls it
  ///         under its exclusive lock, at most once, and keeps the section
  ///         alive until it has.
  /// \throws exception_t With invalid_payload or invalid_version.
  ///
  [[nodiscard]] virtual auto prepare_load(const rfl::Generic::Object& document, load_policy policy)
    -> std::function<void()> = 0;

  ///
  /// Engages the section with a default-constructed value.
  /// What load_policy::create does to a source that is not there: the section
  /// becomes readable through get(), and nothing reaches the transport until
  /// store() is called.
  ///
  /// Called by the document under its lock.
  ///
  virtual auto reset_to_default() -> void = 0;

  ///
  /// Writes the section's value into a document.
  /// A no-op while the section is disengaged: its subtree is left exactly as
  /// parsed, never written from defaults.
  ///
  /// \param document The document to write into.
  ///
  virtual auto write_into(rfl::Generic::Object& document) const -> void = 0;

  ///
  /// Reports whether a value is cached.
  /// \return true if the section was loaded or set at least once.
  ///
  [[nodiscard]] virtual auto engaged() const -> bool = 0;
};

///
/// What the manager's registry owns, and what a handle reaches through.
/// Everything here is virtual so the manager can hold one erased type.
///
class document_base
{
public:
  ///
  /// Constructs the base. The class is abstract, so only a concrete document can.
  ///
  document_base() = default;

  ///
  /// Destroys the document. Virtual, because the manager owns documents
  /// through pointers to this class and deletes them through it.
  ///
  virtual ~document_base() = default;

  // A document is one source, not a value to duplicate: copying one would
  // fork its cache and its transport.
  document_base(const document_base&) = delete;
  document_base(document_base&&) = delete;
  auto operator=(const document_base&) -> document_base& = delete;
  auto operator=(document_base&&) -> document_base& = delete;

  ///
  /// Loads the document: one read, one parse, all sections filled.
  /// All or nothing: where it throws, no section and nothing else the
  /// document holds has changed. Where a load that started after this one
  /// has finished first, this one returns having installed nothing, as the
  /// later read is the one that stands.
  ///
  /// \param policy What to do about a missing or differently versioned document.
  ///
  virtual auto load(load_policy policy) -> void = 0;

  ///
  /// Stores the document with one write of the whole document.
  ///
  virtual auto store() -> void = 0;

  ///
  /// Returns the location, which is what the manager keys on and what errors quote.
  /// \return The location.
  ///
  [[nodiscard]] virtual auto location() const -> std::string = 0;

  ///
  /// Reports whether the document can be stored.
  /// \return true if store() will be attempted rather than refused.
  ///
  [[nodiscard]] virtual auto writable() const -> bool = 0;

  ///
  /// Returns the document's mutex, so a section handle can lock the whole
  /// document around its cached value.
  ///
  /// \return The mutex.
  ///
  [[nodiscard]] virtual auto mutex() const -> std::shared_mutex& = 0;

  ///
  /// Attaches a section. Its value stays disengaged until it is set, or until
  /// a load() that starts after this call has finished.
  ///
  /// \param section The section to attach. Kept.
  ///
  virtual auto attach(std::shared_ptr<section_base> section) -> void = 0;

  ///
  /// Registers a section with the manager's type index.
  /// Does nothing once the manager is gone: its index went with it, so there
  /// is nothing left to register with and nobody left to look the type up.
  ///
  /// \param key The type key, from arude::type_key<T>().
  /// \param section The section to register. Kept.
  /// \param document The document it belongs to. Kept.
  ///
  virtual auto register_type(
    const type_key_t& key, std::shared_ptr<section_base> section, std::shared_ptr<document_base> document) -> void = 0;
};

///
/// A document bound to one endpoint instance and one transport instance.
/// format_ and carrier_ are moved in at construction and only ever used
/// const, so the mutex guards the rest alone: the list of sections, each
/// section's value, the retained tree, and the load numbers.
///
/// \tparam E Endpoint type.
/// \tparam Tr Transport type.
///
template<endpoint_c E, transport_c Tr>
class document final : public document_base
{
public: // Structors
  ///
  /// Constructs a document from its endpoint and transport.
  ///
  /// \param format The endpoint instance. Moved in.
  /// \param carrier The transport instance. Moved in.
  /// \param registry The manager's type index, which every section type added
  ///                 to this document is registered with. Held weakly: the
  ///                 document does not keep it alive and may outlive it, after
  ///                 which register_type() does nothing.
  ///
  document(E format, Tr carrier, std::weak_ptr<type_registry> registry);

public: // Overrides
  ///
  /// \see document_base::load
  ///
  auto load(load_policy policy) -> void override;

  ///
  /// \see document_base::store
  ///
  auto store() -> void override;

  ///
  /// \see document_base::location
  ///
  [[nodiscard]] auto location() const -> std::string override;

  ///
  /// \see document_base::writable
  ///
  [[nodiscard]] auto writable() const -> bool override;

  ///
  /// \see document_base::mutex
  ///
  [[nodiscard]] auto mutex() const -> std::shared_mutex& override;

  ///
  /// \see document_base::attach
  ///
  auto attach(std::shared_ptr<section_base> section) -> void override;

  ///
  /// \see document_base::register_type
  ///
  auto
  register_type(const type_key_t& key, std::shared_ptr<section_base> section, std::shared_ptr<document_base> document)
    -> void override;

private: // Typedefs
  using section_vec_t = std::vector<std::shared_ptr<section_base>>;

private: // Helpers
  ///
  /// Builds the document a root section reads: the parsed tree minus every
  /// registered subsection, so the root section does not see them as unknown
  /// fields.
  ///
  /// \param parsed The parsed tree.
  /// \param sections The sections whose names are stripped.
  /// \return The stripped tree.
  ///
  [[nodiscard]] static auto strip_subsection(const rfl::Generic::Object& parsed, const section_vec_t& sections)
    -> rfl::Generic::Object;

  ///
  /// Starts a load: numbers it, and takes the sections attached so far.
  /// Both under one lock, so a load numbered later never has fewer sections
  /// than one numbered earlier. The sections are a copy, so the load can work
  /// through them outside the lock while attach() adds to the original, and
  /// every section in it stays alive for as long as the copy does.
  ///
  /// \return The sections, in the order they were attached, and the load's
  ///         number, which is greater than that of every load started before.
  ///
  [[nodiscard]] auto begin_load() -> std::pair<section_vec_t, std::uint64_t>;

private: // Variables
  E format_;
  Tr carrier_;
  rfl::Generic::Object parsed_;      // The tree of the source as last loaded, which store() starts from.
  std::uint64_t loads_begun_ = 0;    // Numbers every load in the order begin_load() was called.
  std::uint64_t load_installed_ = 0; // The number of the load whose read the cache holds.
  section_vec_t sections_;
  std::weak_ptr<type_registry> registry_; // Weak, as the registry owns this document.
  mutable std::shared_mutex mutex_;
};

///
/// One typed section of a document.
/// The value is a std::optional<T>: disengaged means never loaded, never set,
/// which is what write_into reads to leave the subtree exactly as parsed.
///
/// \tparam T Configuration type.
///
template<configuration_c T>
class section final : public section_base
{
public: // Structors
  ///
  /// Constructs a disengaged section.
  /// \param name The section's name, empty for the root section.
  ///
  explicit section(std::string_view name);

public: // Overrides
  // The overrides below are virtual members of a class template, which is the
  // whole point of this type: T is erased through section_base, so the
  // virtuals cannot move out of the template. Every instantiation used by a
  // program is emitted in the translation unit that instantiates it, which
  // is the behaviour these warnings question.
  ///
  /// \see section_base::name
  ///
  // NOLINTBEGIN(portability-template-virtual-member-function)
  [[nodiscard]] auto name() const -> std::string_view override;

  ///
  /// \see section_base::prepare_load
  ///
  [[nodiscard]] auto prepare_load(const rfl::Generic::Object& document, load_policy policy)
    -> std::function<void()> override;

  ///
  /// \see section_base::reset_to_default
  ///
  auto reset_to_default() -> void override;

  ///
  /// \see section_base::write_into
  ///
  auto write_into(rfl::Generic::Object& document) const -> void override;

  ///
  /// \see section_base::engaged
  ///
  [[nodiscard]] auto engaged() const -> bool override;
  // NOLINTEND(portability-template-virtual-member-function)

public: // Accessors
  ///
  /// Returns the cached value. The caller holds the document's lock.
  ///
  /// \return The cached value.
  /// \throws exception_t With not_found if the section has never been loaded or set.
  ///
  [[nodiscard]] auto value() const -> const T&;

  ///
  /// Replaces the cached value. The caller holds the document's lock.
  ///
  /// \param value Value to cache. Not retained.
  ///
  auto set_value(const T& value) -> void;

private: // Variables
  std::string name_;
  std::optional<T> value_;
};

///
///
template<configuration_c T>
section<T>::section(const std::string_view name)
  : name_{name}
{
}

///
///
template<configuration_c T>
auto section<T>::name() const -> std::string_view
{
  return name_;
}

///
///
template<configuration_c T>
auto section<T>::prepare_load(const rfl::Generic::Object& document, const load_policy policy) -> std::function<void()>
{
  auto result = detail::parse_section<T>(document, name_, policy, false);

  // What was read lives in the closure and nowhere else until the document
  // runs it, so a load that is abandoned leaves no trace here, and two loads
  // cannot see each other's results. Moved out on the one call there is.
  return [this, result = std::move(result)] mutable -> void { value_ = std::move(result); };
}

///
///
template<configuration_c T>
auto section<T>::reset_to_default() -> void
{
  value_ = T{};
}

///
///
template<configuration_c T>
auto section<T>::write_into(rfl::Generic::Object& document) const -> void
{
  if(!value_.has_value())
  {
    return;
  }

  detail::write_section_value<T>(document, name_, value_.value());
}

///
///
template<configuration_c T>
auto section<T>::engaged() const -> bool
{
  return value_.has_value();
}

///
///
template<configuration_c T>
auto section<T>::value() const -> const T&
{
  // Checked here rather than left to the caller. std::optional::value() throws
  // std::bad_optional_access, which derives from std::exception and not from
  // arude::exception_base, so it walks straight past the catch every other
  // failure in this subsystem is caught by.
  if(!value_.has_value())
  {
    const auto where = name_.empty() ? std::string{"the root section"} : std::format("section '{}'", name_);

    throw exception_t{std::format("arude::config: {} has not been loaded or set.", where), config_error::not_found};
  }

  return value_.value();
}

///
///
template<configuration_c T>
auto section<T>::set_value(const T& value) -> void
{
  value_ = value;
}

///
///
template<endpoint_c E, transport_c Tr>
document<E, Tr>::document(E format, Tr carrier, std::weak_ptr<type_registry> registry)
  : format_{std::move(format)}
  , carrier_{std::move(carrier)}
  , registry_{std::move(registry)}
{
}

///
///
template<endpoint_c E, transport_c Tr>
auto document<E, Tr>::load(const load_policy policy) -> void
{
  // Everything down to the lock works on this snapshot and on locals: the
  // I/O and the parsing are the slow part and must not block a get(), and
  // whatever throws on the way has changed nothing. A section attached after
  // this line is not part of this load.
  const auto [sections, number] = begin_load();

  auto present = false;

  try
  {
    present = carrier_.exists();
  }
  catch(const exception_t&)
  {
    // A transport carries a config_error of its own — read_only, invalid_location,
    // io_error — and it knows better than this layer which one it was. Passed
    // through rather than flattened; only a transport that threw something else
    // needs a code putting on it.
    throw;
  }
  catch(...)
  {
    throw exception_t{
      std::format("arude::config: '{}' could not be reached: {}", carrier_.location(), exception_report()),
      config_error::io_error};
  }

  auto parsed = rfl::Generic::Object{};
  auto installs = std::vector<std::function<void()>>{};
  installs.reserve(sections.size());

  if(!present)
  {
    if(!is_set(policy, load_policy::create))
    {
      throw exception_t{
        std::format("arude::config: there is no configuration at '{}'.", carrier_.location()), config_error::not_found};
    }

    // Nothing is written here. create is a decision about the cache and not
    // about the medium: every section takes its defaults, and the transport is
    // untouched until somebody calls store(). Writing from load() would need a
    // writable transport to read a missing one, and it is store() that the
    // never-write-a-disengaged-section rule is written for — a store() from
    // here, before any section is engaged, emitted an empty document and left
    // the source unparseable on the next run. The tree goes with the sections:
    // there is no source, so there is nothing of it to retain.
    for(const auto& section : sections)
    {
      installs.push_back([section] -> void { section->reset_to_default(); });
    }
  }
  else
  {
    try
    {
      parsed = format_.parse(carrier_);
    }
    catch(const exception_t&)
    {
      // The transport's own error, or the endpoint's invalid_payload. Either is
      // more precise than io_error; see the note in the exists() branch above.
      throw;
    }
    catch(...)
    {
      throw exception_t{
        std::format("arude::config: '{}' could not be read: {}", carrier_.location(), exception_report()),
        config_error::io_error};
    }

    const auto stripped = strip_subsection(parsed, sections);

    for(const auto& section : sections)
    {
      installs.push_back(section->prepare_load(section->name().empty() ? stripped : parsed, policy));
    }
  }

  // The lock is taken once, and only to swap the finished results in: the
  // tree and every section's value change together, so they always describe
  // one and the same read.
  const auto lock = std::scoped_lock{mutex_};

  // A load that started later has installed its read already. Installing
  // this one over it would leave a section attached in between with the
  // later read's value beside this read's tree, and a store would then write
  // the one over the other. Loads are numbered in the order they took their
  // sections, so the later one holds every section this one does, and its
  // read stands for all of them.
  if(load_installed_ > number)
  {
    return;
  }

  load_installed_ = number;
  parsed_ = std::move(parsed);

  for(const auto& install : installs)
  {
    install();
  }
}

///
///
template<endpoint_c E, transport_c Tr>
auto document<E, Tr>::store() -> void
{
  if(!carrier_.writable())
  {
    throw exception_t{
      std::format("arude::config: the transport at '{}' is read-only.", carrier_.location()), config_error::read_only};
  }

  rfl::Generic::Object output;

  {
    const auto lock = std::shared_lock{mutex_};

    output = parsed_;

    for(const auto& section : sections_)
    {
      section->write_into(output);
    }
  }

  format_.emit(carrier_, output);
}

///
///
template<endpoint_c E, transport_c Tr>
auto document<E, Tr>::location() const -> std::string
{
  return carrier_.location();
}

///
///
template<endpoint_c E, transport_c Tr>
auto document<E, Tr>::writable() const -> bool
{
  return carrier_.writable();
}

///
///
template<endpoint_c E, transport_c Tr>
auto document<E, Tr>::mutex() const -> std::shared_mutex&
{
  return mutex_;
}

///
///
template<endpoint_c E, transport_c Tr>
auto document<E, Tr>::attach(std::shared_ptr<section_base> section) -> void
{
  const auto lock = std::scoped_lock{mutex_};
  sections_.push_back(std::move(section));
}

///
///
template<endpoint_c E, transport_c Tr>
auto document<E, Tr>::register_type(
  const type_key_t& key, std::shared_ptr<section_base> section, std::shared_ptr<document_base> document) -> void
{
  // The registry belongs to the manager, and a handle to this document may
  // have outlived it. The lock keeps the registry alive for the length of the
  // call where a manager is being destroyed on another thread meanwhile.
  if(const auto registry = registry_.lock())
  {
    registry->add(key, std::move(section), std::move(document));
  }
}

///
///
template<endpoint_c E, transport_c Tr>
auto document<E, Tr>::strip_subsection(const rfl::Generic::Object& parsed, const section_vec_t& sections)
  -> rfl::Generic::Object
{
  auto subsection_names = std::flat_set<std::string>{};

  for(const auto& section : sections)
  {
    if(!section->name().empty())
    {
      subsection_names.emplace(section->name());
    }
  }

  auto stripped = rfl::Generic::Object{};

  for(const auto& [key, value] : parsed)
  {
    if(!subsection_names.contains(key))
    {
      stripped.insert(key, value);
    }
  }

  return stripped;
}

///
///
template<endpoint_c E, transport_c Tr>
auto document<E, Tr>::begin_load() -> std::pair<section_vec_t, std::uint64_t>
{
  const auto lock = std::scoped_lock{mutex_};

  return {sections_, ++loads_begun_};
}

} // namespace arude::config::detail

namespace arude::config
{

///
/// The handle a registration returns: a whole document, any number of typed
/// sections on it. Copyable and self-contained — it keeps the document alive,
/// so it stays valid even where the config_manager it came from was destroyed
/// first.
///
class config_document_handle final
{
public: // Structors
  ///
  /// Constructs a handle from the document it reaches.
  /// \param document The document. Kept.
  ///
  explicit config_document_handle(std::shared_ptr<detail::document_base> document);

public: // Accessors
  ///
  /// Returns the location, which is what the manager keys on and what errors quote.
  /// \return The location.
  ///
  [[nodiscard]] auto location() const -> std::string;

  ///
  /// Reports whether the document can be stored.
  /// \return true if store() will be attempted rather than refused.
  ///
  [[nodiscard]] auto writable() const -> bool;

public: // Methods
  ///
  /// Loads the document: one read, one parse, all sections filled.
  /// All or nothing: where it throws, every section holds what it held
  /// before.
  ///
  /// \param policy What to do about a missing or differently versioned document.
  ///
  auto load(load_policy policy = load_policy::upgrade_to_current) -> void;

  ///
  /// Stores the document: one write of the whole document.
  ///
  auto store() -> void;

  ///
  /// Adds a section for the root of the document, registering the type with
  /// the manager's index.
  ///
  /// \see add_section for what happens where the manager is gone.
  ///
  /// \tparam T Configuration type.
  /// \return The section handle.
  ///
  template<configuration_c T>
  [[nodiscard]] auto add_root() -> config_handle<T>;

  ///
  /// Adds a section under the name given, registering the type with the
  /// manager's index. An empty name is the root. Adding a section after a
  /// load is allowed: it stays disengaged, and so leaves its part of the
  /// source as it is on a store, until it is set or the next load() fills it.
  /// A load() already under way when the section is added does not fill it.
  ///
  /// Where the config_manager the document came from has been destroyed, the
  /// section is added all the same and the handle returned works in full; only
  /// the registration is skipped, as the index went with the manager.
  ///
  /// \tparam T Configuration type.
  /// \param name The section's name, which is also its key in the document.
  /// \return The section handle.
  ///
  template<configuration_c T>
  [[nodiscard]] auto add_section(std::string_view name) -> config_handle<T>;

private: // Variables
  std::shared_ptr<detail::document_base> document_;
};

///
///
inline config_document_handle::config_document_handle(std::shared_ptr<detail::document_base> document)
  : document_{std::move(document)}
{
}

///
///
inline auto config_document_handle::location() const -> std::string
{
  return document_->location();
}

///
///
inline auto config_document_handle::writable() const -> bool
{
  return document_->writable();
}

///
///
inline auto config_document_handle::load(const load_policy policy) -> void
{
  document_->load(policy);
}

///
///
inline auto config_document_handle::store() -> void
{
  document_->store();
}

///
///
template<configuration_c T>
auto config_document_handle::add_root() -> config_handle<T>
{
  return add_section<T>("");
}

///
///
template<configuration_c T>
auto config_document_handle::add_section(const std::string_view name) -> config_handle<T>
{
  auto section = std::make_shared<detail::section<T>>(name);

  document_->attach(section);
  document_->register_type(type_key<T>(), section, document_);

  return config_handle<T>{std::move(section), document_};
}

} // namespace arude::config
