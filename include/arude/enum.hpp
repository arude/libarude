///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// Static reflection for enumerations, re-exported from magic_enum so callers
/// write arude::enum_name rather than reaching for a second namespace.
///
/// A using-declaration rather than a using-directive: it names one entity and
/// carries its whole overload set, so this list is the interface and nothing
/// arrives merely because a future magic_enum release added it. The list is
/// also what src/arude.cppm re-exports; test/module/test_module.cpp holds the
/// two in step.
///
/// Reflection has a range. magic_enum only sees enumerators whose underlying
/// values fall in [MAGIC_ENUM_RANGE_MIN, MAGIC_ENUM_RANGE_MAX], -128 to 127 by
/// default; anything outside is invisible to every function here. Widen it with
/// a magic_enum::customize::enum_range specialization for the one enum that
/// needs it, which is a specialization of magic_enum's template and so has to
/// be written in magic_enum's namespace rather than this one. Do not widen it
/// by defining the macro in a header: a translation unit that reached
/// magic_enum first would compile a different range into the same inline
/// entities, which is an ODR violation no linker reports. If it must be global,
/// set it as a compile definition on the libarude target.
///
/// The re-exports are documented by the table below rather than one block per
/// declaration, because a block there would reach nobody: magic_enum's headers
/// are not in the Doxyfile's INPUT, so doxygen cannot resolve what a
/// using-declaration names and silently drops the whole declaration. It does
/// not warn about it either, for the same reason, which is why the omission is
/// easy to miss. A file's own detailed description has no such problem, so this
/// is where the descriptions live. What libarude declares itself — enum_c and
/// enum_cast_throw — documents normally and is not repeated here.
///
/// | Name | Purpose |
/// | ---- | ------- |
/// | [case_insensitive][cast] | Predicate for the name-matching enum_cast and enum_contains. |
/// | [enum_cast][cast] | Converts an integer or a name to the value, empty if it is not one. |
/// | [enum_contains][contains] | Whether a value, integer, or name denotes a declared enumerator. |
/// | [enum_count][count] | Number of enumerators in the enumeration. |
/// | [enum_entries][entries] | The enumerators paired with their names, by underlying value. |
/// | [enum_index][index] | Index of an enumerator within enum_values(), empty if it is not one. |
/// | [enum_integer][integer] | The underlying integer of an enumeration value. |
/// | [enum_name][name] | Name of a value, empty if it is not a declared enumerator. |
/// | [enum_names][names] | The names of the enumerators, by underlying value. |
/// | [enum_type_name][type_name] | Name of the enumeration type itself, unqualified. |
/// | [enum_value][value] | The enumerator at an index, which must be less than enum_count(). |
/// | [enum_values][values] | The enumerators, ordered by underlying value. |
///
/// [cast]: https://github.com/Neargye/magic_enum/blob/v0.9.8/doc/reference.md#enum_cast
/// [contains]: https://github.com/Neargye/magic_enum/blob/v0.9.8/doc/reference.md#enum_contains
/// [count]: https://github.com/Neargye/magic_enum/blob/v0.9.8/doc/reference.md#enum_count
/// [entries]: https://github.com/Neargye/magic_enum/blob/v0.9.8/doc/reference.md#enum_entries
/// [index]: https://github.com/Neargye/magic_enum/blob/v0.9.8/doc/reference.md#enum_index
/// [integer]: https://github.com/Neargye/magic_enum/blob/v0.9.8/doc/reference.md#enum_integer
/// [name]: https://github.com/Neargye/magic_enum/blob/v0.9.8/doc/reference.md#enum_name
/// [names]: https://github.com/Neargye/magic_enum/blob/v0.9.8/doc/reference.md#enum_names
/// [type_name]: https://github.com/Neargye/magic_enum/blob/v0.9.8/doc/reference.md#enum_type_name
/// [value]: https://github.com/Neargye/magic_enum/blob/v0.9.8/doc/reference.md#enum_value
/// [values]: https://github.com/Neargye/magic_enum/blob/v0.9.8/doc/reference.md#enum_values
///
/// \see https://github.com/Neargye/magic_enum/blob/v0.9.8/doc/reference.md
///
#pragma once

#include "arude/exception.hpp"

#include <magic_enum/magic_enum.hpp>

#include <format>
#include <functional>
#include <source_location>
#include <string_view>
#include <type_traits>

namespace arude
{

///
/// Concept to check if a type is an enum.
/// \tparam T Type to check
///
template<typename T>
concept enum_c = std::is_enum_v<T>;

using magic_enum::case_insensitive;
using magic_enum::enum_cast;
using magic_enum::enum_contains;
using magic_enum::enum_count;
using magic_enum::enum_entries;
using magic_enum::enum_index;
using magic_enum::enum_integer;
using magic_enum::enum_name;
using magic_enum::enum_names;
using magic_enum::enum_type_name;
using magic_enum::enum_value;
using magic_enum::enum_values;

///
/// Converts an underlying integer to the enumeration value, throwing if it is not one.
/// The checked counterpart of enum_cast: the same conversion, subject to the
/// same reflection range, but an unconvertible input raises instead of
/// returning an empty optional. Use it where a failure is a defect and there is
/// nothing sensible to do with a nullopt; prefer enum_cast where failure is
/// ordinary, which is what the conventions ask of exceptions.
///
/// \tparam E Enumeration to convert to.
/// \param value Underlying integer of the wanted enumerator.
/// \param loc Call site carried by the exception. Defaulted; pass nothing.
/// \return The enumerator of E whose underlying value is value.
/// \throws arude::exception If E has no such enumerator, or has one that
///         reflection cannot see because it falls outside the range.
///
template<enum_c E>
[[nodiscard]] constexpr auto
enum_cast_throw(std::underlying_type_t<E> value, std::source_location loc = std::source_location::current()) -> E;

///
/// Converts a name to the enumeration value, throwing if it is not one.
/// \see enum_cast_throw(std::underlying_type_t<E>, std::source_location)
///
/// \tparam E Enumeration to convert to.
/// \tparam BinaryPredicate Character comparison, defaulting to an exact match.
/// \param value Name of the wanted enumerator, compared unqualified.
/// \param predicate Character comparison; pass case_insensitive to ignore case.
/// \param loc Call site carried by the exception. Defaulted; pass nothing.
/// \return The enumerator of E named value.
/// \throws arude::exception If E has no enumerator of that name, or has one
///         that reflection cannot see because it falls outside the range.
///
template<enum_c E, typename BinaryPredicate = std::equal_to<>>
[[nodiscard]] constexpr auto enum_cast_throw(
  std::string_view value, BinaryPredicate predicate = {}, std::source_location loc = std::source_location::current())
  -> E;

///
///
template<enum_c E>
constexpr auto enum_cast_throw(const std::underlying_type_t<E> value, const std::source_location loc) -> E
{
  const auto result = enum_cast<E>(value);

  if(!result.has_value())
  {
    // exception<void> is spelled out rather than deduced: the two-argument
    // deduction guide takes the second argument as user data, so exception{msg,
    // loc} would build an exception<std::source_location> carrying the location
    // as a payload and report enum.hpp as the throw site.
    //
    // Unary plus promotes a character-like underlying type to int, so the
    // message shows a number rather than a glyph.
    throw exception<void>{
      std::format("arude::enum_cast_throw: {} has no enumerator with value {}", enum_type_name<E>(), +value), loc};
  }

  return result.value();
}

///
///
template<enum_c E, typename BinaryPredicate>
constexpr auto enum_cast_throw(const std::string_view value, BinaryPredicate predicate, const std::source_location loc)
  -> E
{
  const auto result = enum_cast<E>(value, predicate);

  if(!result.has_value())
  {
    throw exception<void>{
      std::format("arude::enum_cast_throw: {} has no enumerator named '{}'", enum_type_name<E>(), value), loc};
  }

  return result.value();
}

} // namespace arude
