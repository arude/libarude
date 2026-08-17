///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// What load() should do about a source that is missing or of another version.
///
#pragma once

#include "arude/enum.hpp"

#include <cstdint>

namespace arude::config
{

///
/// What load() should do about a source that is missing or of another version.
/// A bitmask, so the flags combine through the magic_enum bitwise operators
/// that arude/enum.hpp re-exports as arude::bitwise_operators; is_set() is
/// what reads one back out.
///
enum class load_policy : std::uint8_t
{
  ///
  /// A source must be there, and must be the version asked for.
  ///
  none = 0x00,

  ///
  /// A missing source is written out from the defaults rather than being an error.
  ///
  create = 0x01,

  ///
  /// An older version is migrated forward; without this, an older source is an error.
  ///
  upgrade_to_current = 0x02,

  ///
  /// The version must match exactly, whatever upgrade_to_current says.
  ///
  strict_version = 0x04
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
