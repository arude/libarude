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
///   - The parsed tree is retained, in the endpoint's own type, and store()
///     starts from it, so a table nobody registered survives untouched, down
///     to the type of every value in it. Until the first load(), every
///     store() reads the source first, for the same reason: what the document
///     has not looked at is not its to wipe. discard_source() is the one way
///     to say otherwise, for a source that does not parse.
///   - A section holds std::optional<T>. Disengaged — never loaded, never
///     set — means its subtree is left exactly as parsed, not written from
///     defaults. A section added after a load cannot clobber a value already
///     in the file, which is what makes adding one after a load allowed.
///
/// Owning goes down to the key. A section remembers which keys it read out of
/// its table, and a store replaces exactly those: a key its value no longer
/// produces — a std::optional that was reset, a field a migration renamed —
/// leaves the source, and a key its type never declared stays where it is.
///
/// Owning a key needs exactly one owner for it. A named section's key is its
/// name, and the root's keys are its fields, all at the same top level: so a
/// document refuses a second section under a name it holds, a second root, and
/// a section named like a field of the root, at the point it is added.
///
/// load() never writes, on any policy. load_policy::create is a decision about
/// the cache alone: a source that is not there, or a section that is not in
/// it, leaves the section at its defaults, and it reaches the source when the
/// application calls store(). That keeps the two rules above from
/// contradicting each other — a store() from inside load(), before any section
/// was engaged, would emit a document with every section skipped.
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
/// stays disengaged. The mutex guards the cache and nothing else, and is never
/// held across I/O.
///
/// Stores are put in order by a second mutex per document, a std::mutex for
/// I/O, which a store holds from before it takes its snapshot until its write
/// has finished. Stores are therefore written in the order their snapshots
/// were taken, and the one written last holds the newest values; a store that
/// took its snapshot first cannot finish writing after a newer one. Only a
/// store takes it, always before the cache lock and never under it, so a get()
/// or a set() never waits for a write; another store does.
///
/// A load does not take the I/O mutex. Which read the cache holds is settled
/// by the order loads started in, as above, so a load needs nothing from the
/// order of stores, and making it wait would hold up whoever asked for it for
/// as long as a slow store takes, a write over the network say. A load that
/// overlaps a store reads the source as it was before that write or as it is
/// after, whichever the transport hands it, and installs that read as a whole.
/// So a read can overlap a write of the same source, and making that safe is
/// the transport's job: transport_file does so by replacing the file in one
/// step, and a transport that cannot has to serialise its own read and write.
/// There is no cross-process locking either: two processes writing one
/// document still race, and the last write wins.
///
#pragma once

#include "arude/config/common.hpp"
#include "arude/config/config_handle.hpp"
#include "arude/config/document_base.hpp"
#include "arude/config/endpoint.hpp"
#include "arude/config/load_policy.hpp"
#include "arude/config/migration.hpp"
#include "arude/config/tree_reader.hpp"
#include "arude/config/tree_writer.hpp"
#include "arude/config/type_registry.hpp"
#include "arude/type_key.hpp"

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <flat_map>
#include <format>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <source_location>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace arude::config::detail
{

///
/// A document bound to one endpoint instance and one transport instance.
/// format_ and carrier_ are moved in at construction and only ever used
/// const, so mutex_ guards the rest alone: the list of sections and the
/// top-level keys they write, each section's value and owned keys, the
/// retained tree, and the load numbers. io_mutex_ guards no data; it puts
/// stores in order.
///
/// The retained tree is the endpoint's own, E::document_t, and the document
/// never looks inside it: a load reads it through the reader the endpoint
/// makes for it, and a store writes it through the writer.
///
/// \tparam E Endpoint type.
/// \tparam Tr Transport type, which the endpoint has to suit.
///
template<typename E, typename Tr>
  requires endpoint_c<E, Tr>
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
  auto load(load_policy policy, std::source_location loc) -> void override;

  ///
  /// \see document_base::store
  ///
  auto store(std::source_location loc) -> void override;

  ///
  /// \see document_base::discard_source
  ///
  auto discard_source() -> void override;

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
  auto attach(std::shared_ptr<section_base> section, std::source_location loc) -> void override;

  ///
  /// \see document_base::register_type
  ///
  auto
  register_type(const type_key_t& key, std::shared_ptr<section_base> section, std::shared_ptr<document_base> document)
    -> void override;

private: // Typedefs
  using document_t = typename E::document_t;
  using section_vec_t = std::vector<std::shared_ptr<section_base>>;
  using key_owner_map_t = std::flat_map<std::string, std::string>; // Top-level key to the name of its section.

private: // Helpers
  ///
  /// Returns the names of the named sections among some, which are what the
  /// root section does not see: it would read them as fields of its own.
  ///
  /// \param sections The sections.
  /// \return The names, the root's empty one left out.
  ///
  [[nodiscard]] static auto subsection_names(const section_vec_t& sections) -> key_set_t;

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

  ///
  /// Runs one step against the endpoint, the transport or the tree, and puts
  /// a config_error on whatever it throws that carries none.
  /// An exception_t passes through as it is: a transport knows better than
  /// this layer whether it failed with read_only or io_error, and the
  /// endpoint's invalid_payload is more precise than either. Anything else —
  /// a std::filesystem::filesystem_error, or a fault in an endpoint's reader
  /// or writer, say — becomes an exception_t with the error given, naming the
  /// location.
  ///
  /// \param action What the step does to the source, as a past participle
  ///               completing "could not be ...".
  /// \param error What anything that is not an exception_t is reported as:
  ///              io_error for reading and writing the source, invalid_payload
  ///              for reading sections out of the tree.
  /// \param step The step to run. Not retained.
  /// \param loc The call site a failure reports.
  /// \return Whatever the step returns.
  /// \throws exception_t With the step's own error, or with the error given.
  ///                     The message then carries that of what the step
  ///                     threw, and nothing else of it.
  ///
  auto
  guarded(std::string_view action, config_error error, const std::invocable auto& step, std::source_location loc) const
    -> decltype(auto);

  ///
  /// Reads the source and parses it, in the one read the transport makes.
  /// Takes no lock and changes nothing.
  ///
  /// \param loc The call site a failure reports.
  /// \return The parsed tree, or nothing where the transport has nothing at
  ///         its address.
  /// \throws exception_t With the endpoint's or the transport's own error, or
  ///                     with io_error.
  ///
  [[nodiscard]] auto read_source(std::source_location loc) const -> std::optional<document_t>;

  ///
  /// Reads the source for a store, where the document has no tree of its own
  /// to start from yet. Retains nothing: a store that comes before any load()
  /// reads the source afresh every time, so a store does not wipe what was
  /// added to the source since an earlier one.
  ///
  /// \param loc The call site a failure reports.
  /// \return The source's tree, empty where there is no source; or nothing,
  ///         without a read, where a load() or discard_source() has given the
  ///         document a tree.
  /// \throws exception_t As read_source() does.
  ///
  [[nodiscard]] auto read_unknown_source(std::source_location loc) const -> std::optional<document_t>;

private: // Variables
  E format_;
  Tr carrier_;
  document_t parsed_;                // The tree of the source as last loaded, which store() starts from.
  bool source_known_ = false;        // Whether parsed_ is that tree: set by a load() or discard_source().
  std::uint64_t loads_begun_ = 0;    // Numbers every load in the order begin_load() was called.
  std::uint64_t load_installed_ = 0; // The number of the load whose read the cache holds.
  section_vec_t sections_;
  key_owner_map_t key_owners_;            // The top_level_keys() of every section in sections_.
  std::weak_ptr<type_registry> registry_; // Weak, as the registry owns this document.
  mutable std::shared_mutex mutex_;
  std::mutex io_mutex_; // Held by store() from before its snapshot until its write has finished.
};

///
/// Returns what an error message calls a section.
///
/// \param name The section's name, empty for the root section.
/// \return "the root section", or the name in quotes.
///
[[nodiscard]] auto section_label(std::string_view name) -> std::string;

///
/// One typed section of a document.
/// The value is a std::optional<T>: disengaged means never loaded, never set,
/// which is what write_into reads to leave the subtree exactly as parsed.
///
/// Beside the value it holds the keys it owns in the source, which are the
/// ones the last load found the section to consist of. They are what a store
/// replaces, and they are empty until a load has read the section.
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
  /// \see section_base::top_level_keys
  ///
  [[nodiscard]] auto top_level_keys() const -> key_set_t override;

  ///
  /// \see section_base::prepare_load
  ///
  [[nodiscard]] auto
  prepare_load(const tree_reader& tree, const key_set_t& foreign, load_policy policy, std::source_location loc)
    -> std::function<void()> override;

  ///
  /// \see section_base::write_into
  ///
  auto write_into(tree_writer& tree) const -> void override;

  ///
  /// \see section_base::engaged
  ///
  [[nodiscard]] auto engaged() const -> bool override;
  // NOLINTEND(portability-template-virtual-member-function)

public: // Accessors
  ///
  /// Returns the cached value. The caller holds the document's lock.
  ///
  /// \param loc The call site a failure reports.
  /// \return The cached value.
  /// \throws exception_t With not_found if the section has never been loaded or set.
  ///
  [[nodiscard]] auto value(std::source_location loc) const -> const T&;

  ///
  /// Replaces the cached value. The caller holds the document's lock.
  /// The owned keys stay as they are, as they describe the source and not the
  /// cache. For a section that was never loaded that means none: it has read
  /// nothing, so it owns nothing yet, and a store merges its fields over
  /// whatever its table holds without removing a key.
  ///
  /// \param value Value to cache. Not retained.
  ///
  auto set_value(const T& value) -> void;

private: // Variables
  std::string name_;
  std::optional<T> value_;
  key_set_t owned_keys_; // Guarded by the document's lock, like value_.
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
auto section<T>::top_level_keys() const -> key_set_t
{
  if(name_.empty())
  {
    return detail::declared_keys<T>();
  }

  return key_set_t{name_};
}

///
///
template<configuration_c T>
auto section<T>::prepare_load(
  const tree_reader& tree, const key_set_t& foreign, const load_policy policy, const std::source_location loc)
  -> std::function<void()>
{
  // Value-initialised is what a section that is not there takes when it is
  // created: the type's defaults, and no key owned, as none was read.
  auto result = parsed_section<T>{};

  if(detail::section_present(tree, name_, foreign))
  {
    result = detail::parse_section<T>(tree, name_, foreign, policy, loc);
  }
  else if(!is_set(policy, load_policy::create))
  {
    throw exception_t{
      std::format(
        "arude::config: {} is not in the source, and the load policy does not create it.", section_label(name_)),
      config_error::not_found,
      loc};
  }

  // What was read lives in the closure and nowhere else until the document
  // runs it, so a load that is abandoned leaves no trace here, and two loads
  // cannot see each other's results. Moved out on the one call there is.
  return [this, result = std::move(result)] mutable -> void
  {
    value_ = std::move(result.value);
    owned_keys_ = std::move(result.owned_keys);
  };
}

///
///
template<configuration_c T>
auto section<T>::write_into(tree_writer& tree) const -> void
{
  if(!value_.has_value())
  {
    return;
  }

  // owned_keys_ is read here and never brought up to date with what is
  // written. Every store() starts again from the tree as last loaded, and the
  // owned keys say what the section holds in that tree. Moved on to the keys
  // just written, they would no longer name a key that the loaded tree holds
  // and the value has since dropped, and the next store() would put it back.
  detail::write_section_value<T>(tree, name_, value_.value(), owned_keys_);
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
auto section<T>::value(const std::source_location loc) const -> const T&
{
  // Checked here rather than left to the caller. std::optional::value() throws
  // std::bad_optional_access, which derives from std::exception and not from
  // arude::exception_base, so it walks straight past the catch every other
  // failure in this subsystem is caught by.
  if(!value_.has_value())
  {
    throw exception_t{
      std::format("arude::config: {} has not been loaded or set.", section_label(name_)), config_error::not_found, loc};
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
template<typename E, typename Tr>
  requires endpoint_c<E, Tr>
document<E, Tr>::document(E format, Tr carrier, std::weak_ptr<type_registry> registry)
  : format_{std::move(format)}
  , carrier_{std::move(carrier)}
  , registry_{std::move(registry)}
{
}

///
///
template<typename E, typename Tr>
  requires endpoint_c<E, Tr>
auto document<E, Tr>::load(const load_policy policy, const std::source_location loc) -> void
{
  // Everything down to the lock works on this snapshot and on locals: the
  // I/O and the parsing are the slow part and must not block a get(), and
  // whatever throws on the way has changed nothing. A section attached after
  // this line is not part of this load.
  const auto [sections, number] = begin_load();

  auto parsed = read_source(loc);

  if(!parsed.has_value())
  {
    if(!is_set(policy, load_policy::create))
    {
      throw exception_t{
        std::format("arude::config: there is no configuration at '{}'.", carrier_.location()),
        config_error::not_found,
        loc};
    }

    // Nothing is written here. create is a decision about the cache and not
    // about the medium: the transport is untouched until somebody calls
    // store(), which is what the never-write-a-disengaged-section rule is
    // written for, and a read-only transport can be loaded this way. A source
    // that is not there reads as an empty tree, which no section is in, so
    // every section takes its defaults below by the same route a single
    // missing section does.
    parsed.emplace();
  }

  const auto foreign = subsection_names(sections);

  auto installs = std::vector<std::function<void()>>{};
  installs.reserve(sections.size());

  {
    // The endpoint's reader over this read alone, and gone before parsed is
    // moved below: it retains the tree it reads.
    const auto tree = guarded(
      "read",
      config_error::invalid_payload,
      [this, &parsed] -> decltype(auto) { return format_.make_reader(*parsed); },
      loc);

    for(const auto& section : sections)
    {
      installs.push_back(guarded(
        "read",
        config_error::invalid_payload,
        [&section, &tree, &foreign, policy, loc] -> std::function<void()>
        { return section->prepare_load(tree, foreign, policy, loc); },
        loc));
    }
  }

  // The lock is taken once, and only to swap the finished results in: the
  // tree and every section's value and owned keys change together, so they
  // always describe one and the same read.
  const auto lock = std::scoped_lock{mutex_};

  // A load that started later has installed its read already. Installing
  // this one over it would leave a section attached in between with the
  // later read's value and owned keys beside this read's tree, and a store
  // would then write the one over the other. Loads are numbered in the order
  // they took their sections, so the later one holds every section this one
  // does, and its read stands for all of them.
  if(load_installed_ > number)
  {
    return;
  }

  load_installed_ = number;
  parsed_ = std::move(parsed).value();
  source_known_ = true;

  for(const auto& install : installs)
  {
    install();
  }
}

///
///
template<typename E, typename Tr>
  requires endpoint_c<E, Tr>
auto document<E, Tr>::store(const std::source_location loc) -> void
{
  if(!carrier_.writable())
  {
    throw exception_t{
      std::format("arude::config: the transport at '{}' is read-only.", carrier_.location()),
      config_error::read_only,
      loc};
  }

  // Held from before the snapshot until the write is done, so stores are
  // written in the order their snapshots were taken and the last one written
  // holds the newest values. The read of a source not yet known is inside it
  // too, so a store before any load() reads what the store before it wrote.
  // Taken before the cache lock and never under it, and by no one but a
  // store, so neither a get() nor a load() waits for this write.
  const auto io_lock = std::scoped_lock{io_mutex_};

  auto unknown_source = read_unknown_source(loc);

  document_t output;

  {
    // Shared is enough, as nothing the document holds changes: neither
    // parsed_ nor a section's owned keys follow what is written. Together
    // they describe the source as last loaded, and the next store() starts
    // from that same tree again.
    const auto lock = std::shared_lock{mutex_};

    // A load() that finished since the read above installed its tree with
    // the owned keys read from it. That pair wins, as the read above has no
    // owned keys to go with it. source_known_ never goes back to false, so
    // where it is false still, the read above happened.
    output = source_known_ ? parsed_ : std::move(unknown_source).value_or(document_t{});

    guarded(
      "written",
      config_error::io_error,
      [this, &output] -> void
      {
        // The endpoint's writer over this store's copy alone, gone before
        // the copy is emitted.
        auto tree = format_.make_writer(output);

        for(const auto& section : sections_)
        {
          section->write_into(tree);
        }
      },
      loc);
  }

  guarded(
    "written",
    config_error::io_error,
    [this, &output, loc] -> void { detail::emit_to(format_, carrier_, output, loc); },
    loc);
}

///
///
template<typename E, typename Tr>
  requires endpoint_c<E, Tr>
auto document<E, Tr>::discard_source() -> void
{
  const auto lock = std::scoped_lock{mutex_};

  // The owned keys of a section loaded before are left as they are: against
  // an empty tree there is nothing for them to take out.
  parsed_ = document_t{};
  source_known_ = true;
}

///
///
template<typename E, typename Tr>
  requires endpoint_c<E, Tr>
auto document<E, Tr>::location() const -> std::string
{
  return carrier_.location();
}

///
///
template<typename E, typename Tr>
  requires endpoint_c<E, Tr>
auto document<E, Tr>::writable() const -> bool
{
  return carrier_.writable();
}

///
///
template<typename E, typename Tr>
  requires endpoint_c<E, Tr>
auto document<E, Tr>::mutex() const -> std::shared_mutex&
{
  return mutex_;
}

///
///
template<typename E, typename Tr>
  requires endpoint_c<E, Tr>
auto document<E, Tr>::attach(std::shared_ptr<section_base> section, const std::source_location loc) -> void
{
  // Taken before the lock: for the root section this serialises a value of
  // its type, which has no business holding up a get().
  const auto name = std::string{section->name()};
  const auto keys = section->top_level_keys();

  // The checks and the attach are under one lock, so that no section can be
  // attached in between and take the name after it was found free.
  const auto lock = std::scoped_lock{mutex_};

  if(std::ranges::any_of(
       sections_, [&name](const std::shared_ptr<section_base>& attached) -> bool { return attached->name() == name; }))
  {
    throw exception_t{
      std::format("arude::config: {} is already in the document at '{}'.", section_label(name), carrier_.location()),
      config_error::already_registered_section,
      loc};
  }

  // A named section's one key is its name, and the root's are its fields. So
  // this is where a name and a field of the root collide, whichever of the two
  // was added first: the root would read the table as one of its fields, and
  // hiding the section's name from the root would take its field away from it.
  for(const auto& key : keys)
  {
    if(const auto owner = key_owners_.find(key); owner != key_owners_.end())
    {
      throw exception_t{
        std::format(
          "arude::config: {} cannot be added to the document at '{}', as the key '{}' there belongs to {}.",
          section_label(name),
          carrier_.location(),
          key,
          section_label(owner->second)),
        config_error::already_registered_section,
        loc};
    }
  }

  // Built aside and swapped in, so that a failure to allocate leaves the
  // document as it was: either both change or neither does.
  auto owners = key_owners_;

  for(const auto& key : keys)
  {
    owners.emplace(key, name);
  }

  sections_.push_back(std::move(section));
  key_owners_.swap(owners);
}

///
///
template<typename E, typename Tr>
  requires endpoint_c<E, Tr>
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
template<typename E, typename Tr>
  requires endpoint_c<E, Tr>
auto document<E, Tr>::subsection_names(const section_vec_t& sections) -> key_set_t
{
  auto names = key_set_t{};

  for(const auto& section : sections)
  {
    if(!section->name().empty())
    {
      names.emplace(section->name());
    }
  }

  return names;
}

///
///
template<typename E, typename Tr>
  requires endpoint_c<E, Tr>
auto document<E, Tr>::begin_load() -> std::pair<section_vec_t, std::uint64_t>
{
  const auto lock = std::scoped_lock{mutex_};

  return {sections_, ++loads_begun_};
}

///
///
template<typename E, typename Tr>
  requires endpoint_c<E, Tr>
auto document<E, Tr>::guarded(
  const std::string_view action,
  const config_error error,
  const std::invocable auto& step,
  const std::source_location loc) const -> decltype(auto)
{
  try
  {
    return step();
  }
  catch(const exception_t&)
  {
    throw;
  }
  catch(...)
  {
    throw exception_t{
      std::format("arude::config: '{}' could not be {}: {}", carrier_.location(), action, detail::nested_message()),
      error,
      loc};
  }
}

///
///
template<typename E, typename Tr>
  requires endpoint_c<E, Tr>
auto document<E, Tr>::read_source(const std::source_location loc) const -> std::optional<document_t>
{
  return guarded(
    "read",
    config_error::io_error,
    [this, loc] -> std::optional<document_t> { return detail::parse_from(format_, carrier_, loc); },
    loc);
}

///
///
template<typename E, typename Tr>
  requires endpoint_c<E, Tr>
auto document<E, Tr>::read_unknown_source(const std::source_location loc) const -> std::optional<document_t>
{
  {
    const auto lock = std::shared_lock{mutex_};

    if(source_known_)
    {
      return std::nullopt;
    }
  }

  // Outside the lock, like every other read of the source.
  return read_source(loc).value_or(document_t{});
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
  /// before. A section that is not in the source is a not_found, unless the
  /// policy carries load_policy::create, which gives it its defaults.
  ///
  /// \param policy What to do about a source or a section that is missing, or
  ///               of another version.
  /// \param loc Where the call was made, which is what a failure reports. Left to its default.
  /// \throws exception_t With not_found, invalid_payload or invalid_version,
  ///                     or with the transport's own error, or io_error, if
  ///                     the source cannot be read.
  ///
  auto
  load(load_policy policy = load_policy::upgrade_to_current, std::source_location loc = std::source_location::current())
    -> void;

  ///
  /// Stores the document: one write of the whole document.
  /// Whatever is in the source that no engaged section owns is carried over
  /// as it is, whether or not the document was loaded first. A document
  /// loaded carries over the source as that load read it; one never loaded
  /// reads the source again on every store.
  ///
  /// Stores of one document are written one at a time, each taking its values
  /// once the one before it has finished writing, so the source is left with
  /// the newest values. A store waits for another under way. get(), set() and
  /// load() never wait for a store's write, only for the brief snapshot it
  /// takes under the cache lock.
  ///
  /// \param loc Where the call was made, which is what a failure reports. Left to its default.
  /// \throws exception_t With read_only if the transport cannot be written;
  ///                     with invalid_payload if the document was never
  ///                     loaded and the source does not parse, in which case
  ///                     the source is left as it is and discard_source() is
  ///                     the way to replace it; with the transport's own
  ///                     error, or io_error, if the read or the write fails.
  ///
  auto store(std::source_location loc = std::source_location::current()) -> void;

  ///
  /// Forgets the source, so that a store() writes what the engaged sections
  /// hold and nothing else.
  /// No read happens, and every key, table and section the engaged sections
  /// do not hold is dropped from the source by the next store(). That is
  /// what it is for: replacing a source that does not parse, which store()
  /// otherwise refuses to write over. Section values are untouched, and a
  /// disengaged section is still not written. Lasts until the next load().
  ///
  auto discard_source() -> void;

  ///
  /// Adds a section for the root of the document, registering the type with
  /// the manager's index.
  /// A document has one root, and the root's keys are the top level of the
  /// document, which the names of its other sections are keys of as well: a
  /// second root is refused, and so is a root with a field named like a
  /// section attached already.
  ///
  /// \see add_section for what happens where the manager is gone, and for
  ///      what a refusal leaves behind.
  ///
  /// \tparam T Configuration type.
  /// \param loc Where the call was made, which is what a failure reports. Left to its default.
  /// \return The section handle.
  /// \throws exception_t With already_registered_section if the document has
  ///                     a root already, or if a key that T can write at the
  ///                     top level, its version key included, is the name of
  ///                     a section attached already.
  ///
  template<configuration_c T>
  [[nodiscard]] auto add_root(std::source_location loc = std::source_location::current()) -> config_handle<T>;

  ///
  /// Adds a section under the name given, registering the type with the
  /// manager's index. An empty name is the root. Adding a section after a
  /// load is allowed: it stays disengaged, and so leaves its part of the
  /// source as it is on a store, until it is set or the next load() fills it.
  /// A load() already under way when the section is added does not fill it.
  ///
  /// The name is the section's key at the top level of the document, so it
  /// has to be free there. It is refused where a section of that name is
  /// attached already, which for an empty name means a root, and where it is
  /// a key the root's type can write, whichever of the two is added first:
  /// the root would read the section's table as a field of its own. A refused
  /// section is neither attached nor registered, so the document, and every
  /// load and store of it, behaves as though the call had not been made.
  /// The check is atomic with the attach: of two sections added under one
  /// name at once, one is refused.
  ///
  /// Where the source has no such section, a load() fails with not_found
  /// unless its policy carries load_policy::create, so an application that
  /// gains a section loads the files it wrote before with that flag.
  ///
  /// Where the config_manager the document came from has been destroyed, the
  /// section is added all the same and the handle returned works in full; only
  /// the registration is skipped, as the index went with the manager.
  ///
  /// \tparam T Configuration type.
  /// \param name The section's name, which is also its key in the document.
  /// \param loc Where the call was made, which is what a failure reports. Left to its default.
  /// \return The section handle.
  /// \throws exception_t With already_registered_section, naming the section
  ///                     and the location, if the name is taken as above.
  ///
  template<configuration_c T>
  [[nodiscard]] auto add_section(std::string_view name, std::source_location loc = std::source_location::current())
    -> config_handle<T>;

private: // Variables
  std::shared_ptr<detail::document_base> document_;
};

///
///
template<configuration_c T>
auto config_document_handle::add_root(const std::source_location loc) -> config_handle<T>
{
  return add_section<T>("", loc);
}

///
///
template<configuration_c T>
auto config_document_handle::add_section(const std::string_view name, const std::source_location loc)
  -> config_handle<T>
{
  auto section = std::make_shared<detail::section<T>>(name);

  // Attached first, as attach() is what refuses a name that is taken, and
  // registered only once it has gone through: a refused section is in neither.
  document_->attach(section, loc);
  document_->register_type(type_key<T>(), section, document_);

  return config_handle<T>{std::move(section), document_};
}

} // namespace arude::config
