///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// What load() should do about a source or a section that is missing, or of
/// another version.
///
#pragma once

#include "arude/enum.hpp"

#include <cstdint>

namespace arude::config
{

///
/// What load() should do about a source or a section that is missing, or of
/// another version.
/// A bitmask, so the flags combine through the magic_enum bitwise operators
/// that arude/enum.hpp re-exports as arude::bitwise_operators; is_set() is
/// what reads one back out.
///
/// A section's version must match the one asked for exactly unless
/// upgrade_to_current is given; without it, an older section is an
/// invalid_version, as is a newer one whatever the policy says.
///
enum class load_policy : std::uint8_t
{
  ///
  /// The source must be there, every registered section must be in it, and
  /// each must be the version asked for.
  ///
  none = 0x00,

  ///
  /// What is missing takes its defaults rather than being an error. That
  /// covers a source that is not there, which leaves every section at its
  /// defaults, and equally a section that is not in a source that is there,
  /// which leaves that one section at its defaults and every other as read —
  /// so a release that adds a section still loads a file written before it.
  /// Without this flag either is a not_found.
  ///
  /// Nothing is written: load() never touches the medium, so what was created
  /// reaches the source only when the application calls store(), and a
  /// read-only transport can be loaded this way rather than refusing.
  ///
  create = 0x01,

  ///
  /// An older version is migrated forward; without this, an older source is an error.
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
