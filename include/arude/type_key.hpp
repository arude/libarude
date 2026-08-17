///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
#pragma once

namespace arude::detail
{

///
/// The per-T byte whose address is T's identity token.
///
/// 	param T Type to identify.
///
template<typename T>
inline constexpr char type_key_storage = 0;

} // namespace arude::detail

namespace arude
{

///
/// Returns an identity token for a type, usable as a map key.
/// A pointer to a unique inline variable per T: comparing two keys compares
/// pointers, which works without RTTI and, unlike std::type_info::operator==,
/// never falls back to comparing type names across shared-library boundaries.
/// Diagnostics go through arude::type_name<T>() instead; do not compare or
/// persist names.
///
/// \tparam T Type to identify. Need not be complete.
/// \return An opaque identity token for T.
///
template<typename T>
[[nodiscard]] constexpr auto type_key() -> const void*
{
  return &detail::type_key_storage<T>;
}

} // namespace arude
