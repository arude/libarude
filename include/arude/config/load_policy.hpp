///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// What load() should do about a source or a member that is missing, or of
/// another version.
///
#pragma once

#include "arude/enum.hpp"

#include <cstdint>

namespace arude::config
{

///
/// What load() should do about a source or a member that is missing, or of
/// another version.
/// A bitmask, so the flags combine through the magic_enum bitwise operators
/// that arude/enum.hpp re-exports as arude::bitwise_operators; is_set() is
/// what reads one back out.
///
/// A member's version must match the one its type declares unless
/// upgrade_to_current is given; without it, an older member is an
/// invalid_version, as is a newer one whatever the policy says.
///
enum class load_policy : std::uint8_t
{
  ///
  /// The source must be there, and it and every member must be the version
  /// their types declare.
  ///
  none = 0x00,

  ///
  /// What is missing takes its defaults rather than being an error. That
  /// covers a source that is not there, which leaves the root at `T{}`, and
  /// equally a member or field that is not in a source that is there, which
  /// takes a default: reflect-cpp's rfl::DefaultIfMissing. Without this flag
  /// a source that is not there is a not_found, and a missing field is an
  /// invalid_payload.
  ///
  /// Where the source is there, a struct is read by starting from its own
  /// type's `{}` and replacing what the file holds. So:
  ///
  /// - a member whose table is missing entirely takes the root's initialiser
  ///   for that member;
  /// - a member whose table is present but lacks a field takes that field's
  ///   initialiser in the member's own type, not the value the root's
  ///   initialiser gives it;
  /// - a versioned table without a `version` key is read as the newest
  ///   version;
  /// - an older table's missing fields take the older type's defaults, and
  ///   are then migrated.
  ///
  /// A type used under `create` must be default-constructible all the way
  /// down. One with no initialiser is value-initialised.
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
