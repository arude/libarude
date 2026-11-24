///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// What load() should do about a source that is missing, or a source or
/// member of another version.
///
#pragma once

#include "arude/enum.hpp"

#include <cstdint>

namespace arude::config
{

///
/// What load() should do about a source that is missing, or a source or
/// member of another version.
/// A bitmask, so the flags combine through the magic_enum bitwise operators
/// that arude/enum.hpp re-exports as arude::bitwise_operators; is_set() is
/// what reads one back out.
///
/// A member's version must match the one its type declares unless
/// upgrade_to_current is given; without it, an older member is an
/// invalid_version, as is a newer one whatever the policy says.
///
/// No flag lets a field be missing from a source that is there. Every field
/// is required, and its absence is an invalid_payload naming it, unless its
/// type is one reflect-cpp itself reads as optional: std::optional, or
/// std::shared_ptr. Such a field reads as empty, which for std::optional is
/// std::nullopt even where the member has an initialiser. reflect-cpp reads
/// std::unique_ptr that way too, but a configuration is copied, so it cannot
/// hold one. A versioned table must carry its `version` key.
///
enum class load_policy : std::uint8_t
{
  ///
  /// The source must be there, and it and every member must be the version
  /// their types declare.
  ///
  none = 0x00,

  ///
  /// A source that is not there at all leaves the root at `T{}`, its default
  /// member initialisers, rather than being a not_found. That is all it does:
  /// a source that is there is read exactly as without the flag, so a field
  /// missing from it is still an invalid_payload.
  ///
  /// Nothing is written: load() never touches the medium, so what was created
  /// reaches the source only when the application calls store(), and a
  /// read-only transport can be loaded this way rather than refusing.
  ///
  create = 0x01,

  ///
  /// An older version is migrated forward; without this, an older source or
  /// member is an error.
  ///
  upgrade_to_current = 0x02
};

///
/// Reports whether a policy carries a flag.
///
/// \param value Policy to test.
/// \param flag Flag to look for.
/// \return true if the policy carries it.
///
[[nodiscard]] constexpr auto is_set(load_policy value, load_policy flag) -> bool;

///
///
constexpr auto is_set(const load_policy value, const load_policy flag) -> bool
{
  using arude::bitwise_operators::operator&;

  return (value & flag) == flag;
}

} // namespace arude::config
