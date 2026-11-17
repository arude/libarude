///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// The type-erased document and section: what the manager's registry owns,
/// what a handle reaches through, and what a document holds and iterates over.
///
/// A header of their own, apart from the typed implementations in
/// arude/config/config_document.hpp, so that config_handle.hpp can call into a
/// complete document_base without including the document it is erased from.
///
#pragma once

#include "arude/config/common.hpp"
#include "arude/config/load_policy.hpp"
#include "arude/type_key.hpp"

#include <functional>
#include <memory>
#include <shared_mutex>
#include <string>
#include <string_view>

namespace arude::config
{

class tree_reader;
class tree_writer;

} // namespace arude::config

namespace arude::config::detail
{

///
/// A section of a document with its type erased.
/// What a document holds and iterates over; section<T>, in
/// arude/config/config_document.hpp, is the typed implementation, and
/// config_handle<T> is what a caller sees.
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
  /// Returns the keys of the document's top level that the section writes.
  /// For a named section that is its name alone. For the root section it is
  /// every key its type can write, declared_keys() of the type, whether or
  /// not a value of it is cached. Computed afresh on every call.
  ///
  /// \return The keys.
  ///
  [[nodiscard]] virtual auto top_level_keys() const -> key_set_t = 0;

  ///
  /// Prepares a load of the section, and changes nothing yet.
  /// Reads the section out of a parsed tree, through the endpoint's reader,
  /// and hands back the step that installs what was read. The section itself
  /// is untouched until that step runs: the document gathers one from every
  /// section first, so a load in which any section fails changes none of
  /// them, a slow parse never blocks a get(), and two loads under way at once
  /// each hold steps of their own rather than sharing a place to put a
  /// half-finished result.
  ///
  /// A section that is not in the document takes its type's defaults, owning
  /// no key, where the policy carries load_policy::create. That is also what
  /// a source that is not there comes to: it is handed in as an empty
  /// tree, which no section is in.
  ///
  /// \param tree The endpoint's reader over the parsed tree. Not retained.
  /// \param foreign The names of the registered named sections, which the
  ///                root section does not see.
  /// \param policy What to do about a missing or differently versioned section.
  /// \return The step that installs the value read and the keys the section
  ///         owns in the source. The document calls it under its exclusive
  ///         lock, at most once, and keeps the section alive until it has.
  /// \throws exception_t With not_found if the section is not in the document
  ///                     and the policy does not create it, and with
  ///                     invalid_payload or invalid_version if it is there
  ///                     and cannot be read.
  ///
  [[nodiscard]] virtual auto prepare_load(const tree_reader& tree, const key_set_t& foreign, load_policy policy)
    -> std::function<void()> = 0;

  ///
  /// Writes the section's value into a tree, through the endpoint's writer.
  /// A no-op while the section is disengaged: its subtree is left exactly as
  /// parsed, never written from defaults. Otherwise the keys the section owns
  /// in its table are replaced by the fields of its value, and every other
  /// key in that table is left alone, node and type and all.
  ///
  /// Const, and it has to stay so: the keys a section owns describe the tree
  /// as last loaded, which is the tree every store() starts from, so writing
  /// must not move them on.
  ///
  /// \param tree The endpoint's writer over the tree to write into. Not retained.
  ///
  virtual auto write_into(tree_writer& tree) const -> void = 0;

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
  /// \param policy What to do about a source or a section that is missing, or
  ///               of another version.
  /// \throws exception_t With not_found if the source, or a section of it, is
  ///                     not there and the policy does not create it; with
  ///                     invalid_payload or invalid_version if a section
  ///                     cannot be read; with the transport's own error, or
  ///                     io_error, if the source cannot be.
  ///
  virtual auto load(load_policy policy) -> void = 0;

  ///
  /// Stores the document with one write of the whole document.
  /// What is written is the source as last loaded, with what each engaged
  /// section owns in it replaced by that section's value. Until the document
  /// has been loaded, every store reads the source first, so that everything
  /// already in it is carried over rather than wiped; nothing the document
  /// holds is engaged or changed by that read.
  ///
  /// Stores are written one at a time, in the order they took their
  /// snapshots, so the one written last holds the newest values. A store
  /// waits for another under way to finish writing first. get(), set() and
  /// load() never wait for a store's write, only for the brief snapshot it
  /// takes under the cache lock.
  ///
  /// \throws exception_t With read_only if the transport cannot be written;
  ///                     with invalid_payload if the source has to be read
  ///                     first and does not parse, in which case it is left
  ///                     as it is and discard_source() is the way past; with
  ///                     the transport's own error, or io_error, if the read
  ///                     or the write fails.
  ///
  virtual auto store() -> void = 0;

  ///
  /// Forgets the source, so that a store() writes what the engaged sections
  /// hold and nothing else.
  /// No read happens, and every key, table and section the engaged sections
  /// do not hold is dropped from the source by the next store(). That is
  /// what it is for: replacing a source that does not parse, which store()
  /// otherwise refuses to write over. Section values are untouched, and a
  /// disengaged section is still not written. Lasts until the next load().
  ///
  virtual auto discard_source() -> void = 0;

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
  /// Refused where the section's name is taken in the document: where a
  /// section of that name is attached already, or where a key in the
  /// section's top_level_keys() is in those of one attached already. The
  /// check and the attach are one step under the document's lock, so of two
  /// sections attached under one name at once, only one gets through.
  ///
  /// \param section The section to attach. Kept, unless it is refused.
  /// \throws exception_t With already_registered_section, naming the section
  ///                     and the location, where the section is refused. The
  ///                     document is then left as it was.
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

} // namespace arude::config::detail
