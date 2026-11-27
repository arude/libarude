///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
#pragma once

#include <cstddef>
#include <source_location>
#include <string_view>

namespace arude::detail
{

///
/// Finds the opening parenthesis of a signature's parameter list.
/// That is the first '(' outside angle brackets that is not the "()" of an
/// operator(). msvc writes the latter as "operator ()", so both spellings are
/// skipped.
///
/// \param signature Compiler-generated signature, as from std::source_location::function_name().
/// \return Position of the '(', or std::string_view::npos if there is none.
///
[[nodiscard]] constexpr auto parameter_list_open(std::string_view signature) -> std::size_t;

///
/// Finds where the qualified function name starts.
/// That is just past the last space before the parameter list that is outside
/// angle brackets and not part of "operator ()", which drops the return type
/// and msvc's calling convention.
///
/// \param signature Compiler-generated signature.
/// \param open Position of the parameter list's '(', from parameter_list_open().
/// \return Position of the name's first character; 0 if nothing precedes it.
///
[[nodiscard]] constexpr auto qualified_name_begin(std::string_view signature, std::size_t open) -> std::size_t;

///
/// Finds the parenthesis closing a signature's parameter list.
///
/// \param signature Compiler-generated signature.
/// \param open Position of the parameter list's '(', from parameter_list_open().
/// \return Position of the matching ')', or std::string_view::npos if it is unbalanced.
///
[[nodiscard]] constexpr auto parameter_list_close(std::string_view signature, std::size_t open) -> std::size_t;

///
/// Cuts a compiler-generated signature down to "name(parameters)".
/// Apart from function_signature() so the parse can be fed a captured msvc,
/// gcc or clang signature on any platform, not only the one being compiled.
///
/// \param signature Compiler-generated signature.
/// \return The qualified name and parameter list, with a trailing " const" if
///         present, or signature unchanged if it cannot be parsed, which is
///         visibly wrong rather than silently plausible.
///
[[nodiscard]] constexpr auto extract_function_signature(std::string_view signature) -> std::string_view;

///
///
constexpr auto parameter_list_open(const std::string_view signature) -> std::size_t
{
  auto angle = 0;

  for(auto i = std::size_t{0}; i < signature.size(); ++i)
  {
    const auto c = signature[i];

    // The '-' test keeps operator-> from closing a bracket that never opened.
    if(c == '<')
    {
      ++angle;
    }
    else if(c == '>' && (i == 0 || signature[i - 1] != '-'))
    {
      --angle;
    }
    else if(c == '(' && angle == 0)
    {
      const auto head = signature.substr(0, i);

      if(!head.ends_with("operator") && !head.ends_with("operator "))
      {
        return i;
      }

      i = signature.find(')', i);

      if(i == std::string_view::npos)
      {
        return std::string_view::npos;
      }
    }
  }

  return std::string_view::npos;
}

///
///
constexpr auto qualified_name_begin(const std::string_view signature, const std::size_t open) -> std::size_t
{
  auto depth = 0;

  // Walking backwards, so '>' opens a bracket and '<' closes it.
  for(auto i = open; i-- > 0;)
  {
    const auto c = signature[i];

    if(c == '>' && (i == 0 || signature[i - 1] != '-'))
    {
      ++depth;
    }
    else if(c == '<')
    {
      --depth;
    }
    else if(c == ' ' && depth == 0 && !signature.substr(0, i).ends_with("operator"))
    {
      return i + 1;
    }
  }

  return 0;
}

///
///
constexpr auto parameter_list_close(const std::string_view signature, const std::size_t open) -> std::size_t
{
  auto paren = 0;

  for(auto i = open; i < signature.size(); ++i)
  {
    if(signature[i] == '(')
    {
      ++paren;
    }
    else if(signature[i] == ')' && --paren == 0)
    {
      return i;
    }
  }

  return std::string_view::npos;
}

///
///
constexpr auto extract_function_signature(const std::string_view signature) -> std::string_view
{
  static constexpr auto const_qualifier = std::string_view{" const"};

  const auto open = parameter_list_open(signature);

  if(open == std::string_view::npos)
  {
    return signature;
  }

  const auto close = parameter_list_close(signature, open);

  if(close == std::string_view::npos)
  {
    return signature;
  }

  auto begin = qualified_name_begin(signature, open);

  // clang binds a pointer or reference return type to the name, as in
  // "int *f()", so the space found above sits before the '*'. No qualified
  // name starts with either character.
  while(begin < open && (signature[begin] == '*' || signature[begin] == '&'))
  {
    ++begin;
  }

  auto end = close + 1;

  // Only const survives. A trailing &, &&, volatile or noexcept is dropped, as
  // is gcc's "[with T = ...]" tail.
  if(signature.substr(end).starts_with(const_qualifier))
  {
    end += const_qualifier.size();
  }

  return signature.substr(begin, end - begin);
}

} // namespace arude::detail

namespace arude
{

///
/// Returns the caller's qualified name and parameter list.
/// "Class::method(args)", with no return type, calling convention or template
/// argument tail, and a trailing " const" kept where the function has one. For
/// logging and diagnostics.
///
/// The spelling is otherwise the compiler's: msvc writes "int,double" where gcc
/// and clang write "int, double", and a function template shows its parameters
/// as written on gcc and clang, as in "tpl(T)". Inside a lambda, gcc and clang
/// report the enclosing function, as in "main()", where msvc reads
/// "<lambda_hash>::operator ()(...)". A name containing operator<, operator>
/// or operator<< can defeat the angle bracket tracking, in which case the result
/// is cut in the wrong place. Do not parse or compare the result.
///
/// \param location Call site to describe. Leave it defaulted: the default is
///        evaluated where function_signature() is called, which is what makes
///        it the caller's. Pass one explicitly to describe a location captured
///        earlier, such as an exception's.
/// \return The signature. The view points into static storage and stays valid
///         for the lifetime of the program.
///
[[nodiscard]] constexpr auto function_signature(std::source_location location = std::source_location::current())
  -> std::string_view;

///
///
constexpr auto function_signature(const std::source_location location) -> std::string_view
{
  return detail::extract_function_signature(location.function_name());
}

} // namespace arude
