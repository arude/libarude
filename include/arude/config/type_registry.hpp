///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// The index from configuration type to the sections registered as that type.
///
/// It is an object of its own, apart from config_manager, because of who
/// writes to it: a document adds to the index every time a section is added,
/// and a document outlives the manager wherever a handle to it does. So the
/// manager owns the registry through a std::shared_ptr and each document holds
/// a std::weak_ptr back to it. Once the manager is gone the weak pointer no
/// longer locks, and a section added from then on is simply not indexed —
/// there is nothing left that could look it up.
///
/// Ownership runs one way only. The registry keeps every indexed section and
/// its document alive; a document never keeps the registry alive, so the two
/// cannot hold each other in a cycle.
///
#pragma once

#include "arude/type_key.hpp"

#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace arude::config::detail
{

class document_base;
class section_base;

///
/// Every registration of one type: the sections and the documents they belong to.
/// Non-template, so one index can cover every type at once.
///
struct type_entry
{
  ///
  /// One registration, made by one add_section or add_root of this type.
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
/// The type index: registrations by type key, behind a lock of their own.
///
/// Held through std::shared_ptr by the manager and std::weak_ptr by its
/// documents, see the file comment. Neither copyable nor movable, as its mutex
/// is not: it stays where std::make_shared put it.
///
class type_registry final
{
public: // Methods
  ///
  /// Adds one registration under a type key, keeping every earlier one, so an
  /// ambiguity error can name all the locations.
  ///
  /// \param key The type key, from arude::type_key<T>().
  /// \param section The section to register. Kept.
  /// \param document The document it belongs to. Kept.
  ///
  auto add(const type_key_t& key, std::shared_ptr<section_base> section, std::shared_ptr<document_base> document)
    -> void;

public: // Accessors
  ///
  /// Returns the registrations made under a type key.
  /// A copy, taken under the lock: the caller can walk it, and call into the
  /// documents it names, without holding the registry.
  ///
  /// \param key The type key, from arude::type_key<T>().
  /// \return The registrations in the order they were added, empty if there are none.
  ///
  [[nodiscard]] auto registrations(const type_key_t& key) const -> std::vector<type_entry::registration>;

private: // Variables
  mutable std::shared_mutex mutex_;
  std::unordered_map<type_key_t, type_entry> types_; // Keyed by type_key<T>().
};

// Defined here rather than in src/config, unlike the rest of the configuration
// support: type_key_t is std::type_index with RTTI and a pointer wrapper
// without, so both members' signatures and the map behind them follow the RTTI
// setting of whoever compiles them. Kept inline, they are always compiled with
// the consumer's setting, so one built with -fno-rtti against a library built
// with RTTI still links and still finds what it registered.

///
///
inline auto type_registry::add(
  const type_key_t& key, std::shared_ptr<section_base> section, std::shared_ptr<document_base> document) -> void
{
  const auto lock = std::scoped_lock{mutex_};

  // Creates the entry on the first registration of a type and finds it on
  // every later one.
  types_.try_emplace(key).first->second.registrations.push_back(
    type_entry::registration{.section = std::move(section), .document = std::move(document)});
}

///
///
inline auto type_registry::registrations(const type_key_t& key) const -> std::vector<type_entry::registration>
{
  const auto lock = std::shared_lock{mutex_};

  const auto found = types_.find(key);

  if(found == types_.end())
  {
    return {};
  }

  return found->second.registrations;
}

} // namespace arude::config::detail
