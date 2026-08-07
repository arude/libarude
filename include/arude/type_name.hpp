///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
#pragma once

#include <array>
#include <string_view>

namespace arude::detail
{

///
/// Extracts a type name from a compiler-generated function signature.
/// The name sits between prefix and the end of the template argument. Three
/// signature shapes have to be handled, and the search order below covers all
/// of them:
///
///     clang: std::string_view arude::type_name() [T = int]
///     gcc:   ... arude::type_name() [with T = int; std::string_view = ...]
///     msvc:  ... __cdecl arude::type_name<int>(void)
///
/// gcc appends "; <alias> = <expansion>" whenever the return type is an alias,
/// which std::string_view is, so a semicolon after the argument ends the name.
/// Searching for it first rather than assuming it is present keeps the parse
/// correct if the return type ever stops being an alias.
///
/// \param signature Compiler-generated signature, from __PRETTY_FUNCTION__ or __FUNCSIG__.
/// \param prefix Marker immediately preceding the type name.
/// \param terminator Marker closing the template argument, used when no semicolon follows it.
/// \return The type name, or signature unchanged if the markers are absent, which is
///         visibly wrong rather than silently plausible.
///
[[nodiscard]] constexpr auto
extract_type_name(std::string_view signature, std::string_view prefix, std::string_view terminator) -> std::string_view;

///
/// Removes a leading elaborated type specifier from a type name.
/// msvc spells class types as "class my_type" in __FUNCSIG__ where gcc and
/// clang write "my_type"; stripping the specifier makes the three agree for
/// the common case of a plain class, struct, enum, or union.
///
/// \param name Type name to strip.
/// \return name without its leading specifier, or name unchanged if it has none.
///
[[nodiscard]] constexpr auto strip_elaborated_specifier(std::string_view name) -> std::string_view;

///
///
constexpr auto
extract_type_name(const std::string_view signature, const std::string_view prefix, const std::string_view terminator)
  -> std::string_view
{
  const auto prefix_pos = signature.find(prefix);

  if(prefix_pos == std::string_view::npos)
  {
    return signature;
  }

  const auto begin = prefix_pos + prefix.size();
  auto end = signature.find(';', begin);

  if(end == std::string_view::npos)
  {
    end = signature.rfind(terminator);
  }

  if(end == std::string_view::npos || end < begin)
  {
    return signature;
  }

  // msvc writes nested closing angle brackets apart, as "> >", leaving a
  // trailing space once the outer one is cut off.
  while(end > begin && signature[end - 1] == ' ')
  {
    --end;
  }

  return signature.substr(begin, end - begin);
}

///
///
constexpr auto strip_elaborated_specifier(const std::string_view name) -> std::string_view
{
  static constexpr auto specifiers = std::array{
    std::string_view{"class "}, std::string_view{"struct "}, std::string_view{"enum "}, std::string_view{"union "}};

  for(const auto specifier : specifiers)
  {
    if(name.starts_with(specifier))
    {
      return name.substr(specifier.size());
    }
  }

  return name;
}

} // namespace arude::detail

namespace arude
{

///
/// Returns the name of a type as the compiler spells it.
/// Recovered from the compiler's own signature macro, so the exact spelling is
/// a property of the toolchain, not of libarude: standard library types carry
/// whichever internal namespace the implementation uses, references and
/// pointers differ in spacing, and template arguments on msvc keep their
/// elaborated specifiers. Suitable for diagnostics and logging; do not
/// persist a name, parse it, or compare names produced by different compilers.
///
/// \tparam T Type to name. Need not be complete.
/// \return The type name. The view points into static storage and stays valid
///         for the lifetime of the program.
///
template<typename T>
[[nodiscard]] constexpr auto type_name() -> std::string_view;

///
///
template<typename T>
constexpr auto type_name() -> std::string_view
{
#if (defined __clang__) || (defined __GNUC__)
  return detail::strip_elaborated_specifier(detail::extract_type_name(__PRETTY_FUNCTION__, "T = ", "]"));
#elif (defined _MSC_VER)
  return detail::strip_elaborated_specifier(detail::extract_type_name(__FUNCSIG__, "type_name<", ">(void)"));
#else
  // Reached only on a compiler with neither signature macro. Under C++23 this
  // fires on instantiation rather than at parse time, so a consumer that never
  // calls type_name() still builds.
  static_assert(false, "arude::type_name: unsupported compiler.");
  return {};
#endif // #if (defined __clang__) || (defined __GNUC__)
}

} // namespace arude
