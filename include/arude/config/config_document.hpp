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
/// Locking: one shared_mutex per document, not per section — sections cannot
/// be locked independently when a write serializes them all together. The I/O
/// and the parsing happen outside that lock, and the lock is taken
/// only to swap the finished values in, or a slow fetch would block every
/// get() on that document for the length of the request. There is no
/// cross-process locking: two processes writing one document still race;
/// within a process the document mutex suffices.
///
#pragma once

#include "arude/config/config_handle.hpp"
#include "arude/config/endpoint.hpp"
#include "arude/config/load_policy.hpp"
#include "arude/config/migration.hpp"
#include "arude/config/transport.hpp"
#include "arude/exception_report.hpp"
#include "arude/type_key.hpp"

#include <rfl/Generic.hpp>

#include <cstddef>
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
/// One registration of a type: the section and the document it belongs to.
/// Non-template, so a manager can hold an index over every type at once.
///
struct type_entry
{
  ///
  /// The registrations, one per add_section or add_root of this type.
  ///
  struct registration
  {
    ///
    /// The section the type is registered as.
    ///
    std::shared_ptr<section_base> section;

    ///
    /// The document that section belongs to.
    ///
    std::shared_ptr<document_base> document;
  };

  ///
  /// Every registration, so get<T>() can find the one handle and name the
  /// locations when there is more than one.
  ///
  std::vector<registration> registrations;
};

///
/// One typed section of a document, behind the erased section_base below.
/// \see section_base
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
  /// Reads the section's value out of a parsed document.
  /// Computes into a pending value and does not touch the cached one: the
  /// document commits pending values under its lock, once every
  /// section has been read, so a slow parse never blocks a get().
  ///
  /// \param document The parsed document. For the root section the document
  ///                  has every registered subsection already stripped.
  /// \param policy What to do about a differently versioned section.
  /// \throws exception_t With invalid_payload or invalid_version.
  ///
  virtual auto read_from(const rfl::Generic::Object& document, load_policy policy) -> void = 0;

  ///
  /// Swaps the value read by read_from() into the cached value.
  /// Called by the document under its lock.
  ///
  virtual auto commit() -> void = 0;

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
  /// Destroys the document. Virtual, because the manager owns documents
  /// through pointers to this class and deletes them through it.
  ///
  ///
  /// Constructs the base. The class is abstract, so only a concrete document can.
  ///
  document_base() = default;

  virtual ~document_base() = default;

  // A document is one source, not a value to duplicate: copying one would
  // fork its cache and its transport.
  document_base(const document_base&) = delete;
  document_base(document_base&&) = delete;
  auto operator=(const document_base&) -> document_base& = delete;
  auto operator=(document_base&&) -> document_base& = delete;

  ///
  /// Loads the document: one read, one parse, all sections filled.
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
  /// Attaches a section. Its value stays disengaged until the next load().
  ///
  /// \param section The section to attach. Kept.
  ///
  virtual auto attach(std::shared_ptr<section_base> section) -> void = 0;

  ///
  /// Registers a section with the manager's type index.
  ///
  /// \param key The type key, from type_key<T>().
  /// \param section The section to register. Kept.
  /// \param document The document it belongs to. Kept.
  ///
  virtual auto register_type(
    const void* key, std::shared_ptr<section_base> section, std::shared_ptr<document_base> document) -> void = 0;
};

///
/// A document bound to one endpoint instance and one transport instance.
/// format_ and carrier_ are moved in at construction and only ever used
/// const, so the mutex guards the sections' values and parsed_ alone.
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
  /// \param registrar Called when a section type is registered, so the
  ///                  manager's index can pick it up. The manager must
  ///                  outlive every registration made through this document.
  ///
  document(
    E format,
    Tr carrier,
    std::function<void(const void*, std::shared_ptr<section_base>, std::shared_ptr<document_base>)> registrar);

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
  auto register_type(const void* key, std::shared_ptr<section_base> section, std::shared_ptr<document_base> document)
    -> void override;

private: // Helpers
  ///
  /// Builds the document a root section reads: the parsed tree minus every
  /// registered subsection, so the root section does not see them as unknown
  /// fields.
  ///
  /// \param parsed The parsed tree.
  /// \return The stripped tree.
  ///
  [[nodiscard]] auto strip_subsection(const rfl::Generic::Object& parsed) const -> rfl::Generic::Object;

private: // Variables
  E format_;
  Tr carrier_;
  rfl::Generic::Object parsed_;
  std::vector<std::shared_ptr<section_base>> sections_;
  std::function<void(const void*, std::shared_ptr<section_base>, std::shared_ptr<document_base>)> registrar_;
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
  /// \see section_base::read_from
  ///
  auto read_from(const rfl::Generic::Object& document, load_policy policy) -> void override;

  ///
  /// \see section_base::commit
  ///
  auto commit() -> void override;

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
  std::optional<T> pending_;
  mutable std::mutex pending_mutex_;
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
auto section<T>::read_from(const rfl::Generic::Object& document, const load_policy policy) -> void
{
  const auto lock = std::scoped_lock{pending_mutex_};

  pending_ = detail::parse_section<T>(document, name_, policy, false);
}

///
///
template<configuration_c T>
auto section<T>::commit() -> void
{
  const auto lock = std::scoped_lock{pending_mutex_};

  value_ = std::move(pending_);
  pending_.reset();
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
  // The caller holds the document's lock and has checked engaged() or loaded()
  // first, so the optional is engaged here.
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
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
document<E, Tr>::document(
  E format,
  Tr carrier,
  std::function<void(const void*, std::shared_ptr<section_base>, std::shared_ptr<document_base>)> registrar)
  : format_{std::move(format)}
  , carrier_{std::move(carrier)}
  , registrar_{std::move(registrar)}
{
}

///
///
template<endpoint_c E, transport_c Tr>
auto document<E, Tr>::load(const load_policy policy) -> void
{
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

  if(!present)
  {
    if(!is_set(policy, load_policy::create))
    {
      throw exception_t{
        std::format("arude::config: there is no configuration at '{}'.", carrier_.location()), config_error::not_found};
    }

    // Written out rather than only returned, so that the file the user is
    // meant to edit is there after the first run. On a read-only transport
    // that is a read_only failure, which is the honest answer: there is
    // nothing there and nothing this manager can do about it.
    store();

    return;
  }

  rfl::Generic::Object parsed;

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

  // All of this outside the lock: parsing is the slow part, and it must not
  // block get() for the length of a fetch.
  for(const auto& section : sections_)
  {
    const auto document = section->name().empty() ? strip_subsection(parsed) : parsed;
    section->read_from(document, policy);
  }

  // The lock is taken only to swap the finished values in.
  const auto lock = std::scoped_lock{mutex_};

  parsed_ = std::move(parsed);

  for(const auto& section : sections_)
  {
    section->commit();
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
  const void* key, std::shared_ptr<section_base> section, std::shared_ptr<document_base> document) -> void
{
  registrar_(key, std::move(section), std::move(document));
}

///
///
template<endpoint_c E, transport_c Tr>
auto document<E, Tr>::strip_subsection(const rfl::Generic::Object& parsed) const -> rfl::Generic::Object
{
  auto subsection_names = std::flat_set<std::string>{};

  for(const auto& section : sections_)
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
  /// \tparam T Configuration type.
  /// \return The section handle.
  ///
  template<configuration_c T>
  [[nodiscard]] auto add_root() -> config_handle<T>;

  ///
  /// Adds a section under the name given, registering the type with the
  /// manager's index. An empty name is the root. Adding a section after a
  /// load is allowed: it stays disengaged until the next load() and cannot be
  /// written meanwhile.
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
