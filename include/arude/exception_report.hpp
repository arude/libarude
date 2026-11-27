///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// Turning a caught exception into a printable report.
///
/// Kept apart from arude/exception.hpp because the two have different reach:
/// throwing is done everywhere, reporting only where an exception is finally
/// handled. Reporting is what needs \<exception\>, \<expected\> and
/// \<system_error\>, so a header that only throws does not pay for them.
///
#pragma once

// Included for the std::formatter it carries for every enum, and not for a
// name used here: without it exception<errors> reports its payload as not
// formattable, which is the part of the report a caller wants most.
#include "arude/enum.hpp"
#include "arude/exception.hpp"

#include <cstddef>
#include <exception>
#include <expected>
#include <format>
#include <string>
#include <system_error>
#include <utility>

///
/// Most levels of nested exception arude::exception_report() will unwind.
/// Defaults to 16. Ten levels of genuine nesting is already pathological, so
/// anything past this is a runaway rather than a report that got cut short;
/// the report stops there and says it was truncated. Define it ahead of this
/// header to override. Must be at least 1.
///
#if !(defined ARUDE_EXCEPTION_REPORT_MAX_DEPTH)
  #define ARUDE_EXCEPTION_REPORT_MAX_DEPTH 16
#endif // #if !(defined ARUDE_EXCEPTION_REPORT_MAX_DEPTH)

namespace arude::detail
{

// A limit below one would make the outermost report truncate itself, turning
// every report into the truncation note. Catch that at the point of override
// rather than leaving it to be puzzled over at runtime.
static_assert(ARUDE_EXCEPTION_REPORT_MAX_DEPTH >= 1, "ARUDE_EXCEPTION_REPORT_MAX_DEPTH must be at least 1.");

///
/// Bounds the recursion depth of arude::exception_report().
/// exception_report() recurses to walk a chain of nested exceptions, and it
/// makes that recursive call from a catch handler which also catches whatever
/// the report building itself throws. That pairing has no natural end: if
/// formatting throws std::bad_alloc, the handler catches it, reports it, and
/// formats again. A cyclic nesting chain arrives at the same place by another
/// route, and a merely deep one exhausts the stack. Counting the levels and
/// refusing to go further is the only exit that covers all three.
///
/// The count is per thread, since two threads may be reporting unrelated
/// exceptions at the same time.
///
class exception_report_guard final
{
public: // Structors / Operators
  ///
  /// Enters one level of the report, which is left again on destruction.
  ///
  exception_report_guard();

  ///
  /// Leaves the level entered by the constructor.
  ///
  ~exception_report_guard();

  exception_report_guard(const exception_report_guard&) = delete;
  exception_report_guard(exception_report_guard&&) = delete;
  auto operator=(const exception_report_guard&) -> exception_report_guard& = delete;
  auto operator=(exception_report_guard&&) -> exception_report_guard& = delete;

public: // Accessors
  ///
  /// Reports whether this level is past the limit.
  /// \return true if the report must stop rather than recurse again.
  ///
  [[nodiscard]] auto exhausted() const -> bool;

private: // Data
  static inline thread_local std::size_t depth_ = 0;

  // The depth this guard entered at. Held per instance rather than read back
  // from depth_, so a guard reports on its own level and not on whatever a
  // deeper call happens to have left behind.
  std::size_t level_;
};

///
///
inline exception_report_guard::exception_report_guard()
  : level_{++depth_}
{
}

///
///
inline exception_report_guard::~exception_report_guard()
{
  --depth_;
}

///
///
inline auto exception_report_guard::exhausted() const -> bool
{
  return level_ > ARUDE_EXCEPTION_REPORT_MAX_DEPTH;
}

///
/// One link of a chain of nested exceptions, as arude::exception_message()
/// reads it.
///
struct exception_link
{
  ///
  /// The message of the exception at this link.
  ///
  exception_string_t message;

  ///
  /// The exception nested in it, or null where there is none.
  ///
  std::exception_ptr nested;
};

///
/// Whether the build has RTTI. clang and gcc define __GXX_RTTI, msvc defines
/// _CPP_RTTI, and __cpp_rtti is the standard spelling newer front ends also
/// provide.
///
inline constexpr auto rtti_available =
#if (defined __cpp_rtti) || (defined __GXX_RTTI) || (defined _CPP_RTTI)
  true;
#else
  false;
#endif // #if (defined __cpp_rtti) || (defined __GXX_RTTI) || (defined _CPP_RTTI)

///
/// Rethrows the exception nested in the one being handled, if there is one.
/// With RTTI this is std::rethrow_if_nested. Without it, libc++'s
/// std::rethrow_if_nested does not compile, as it finds the nesting with
/// dynamic_cast, so the exception being handled is rethrown and caught as a
/// std::nested_exception instead. That route keeps one more exception in
/// flight per level, which MSVC pays for in stack, so it is taken only where
/// it is needed.
///
/// Call it only from inside the catch block that caught ex.
///
/// \param ex The exception being handled.
/// \throws Whatever is nested in it.
///
auto rethrow_if_nested_in(const auto& ex) -> void;

///
/// Returns the exception nested in the one being handled, if there is one.
/// Call it only from inside the catch block that caught ex.
///
/// \param ex The exception being handled.
/// \return The nested exception, or null where there is none.
///
[[nodiscard]] auto nested_exception_of(const auto& ex) -> std::exception_ptr;

///
/// Reads the message of an exception and the exception nested in it.
///
/// \param eptr The exception. Must not be null.
/// \return Its message, as arude::exception_message() describes it, and the
///         exception nested in it.
/// \throws std::bad_alloc If the message string cannot be allocated.
///
[[nodiscard]] auto read_exception_link(const std::exception_ptr& eptr) -> exception_link;

///
///
auto rethrow_if_nested_in(const auto& ex) -> void
{
  if constexpr(rtti_available)
  {
    std::rethrow_if_nested(ex);
  }
  else
  {
    try
    {
      throw;
    }
    catch(const std::nested_exception& nested)
    {
      // A nested_exception made outside a catch block holds nothing, and
      // rethrow_nested() on that terminates.
      if(nested.nested_ptr() != nullptr)
      {
        nested.rethrow_nested();
      }
    }
    catch(...)
    {
      // Not a nested_exception, so there is nothing nested to rethrow.
      return;
    }
  }
}

///
///
auto nested_exception_of(const auto& ex) -> std::exception_ptr
{
  try
  {
    rethrow_if_nested_in(ex);
  }
  catch(...)
  {
    return std::current_exception();
  }

  return nullptr;
}

///
///
inline auto read_exception_link(const std::exception_ptr& eptr) -> exception_link
{
  try
  {
    std::rethrow_exception(eptr);
  }
  catch(const char chr)
  {
    return {.message = exception_string_t(1, chr), .nested = nullptr};
  }
  catch(const char* const str)
  {
    // A thrown null pointer is still a const char*, and no string to copy.
    return {.message = str != nullptr ? str : "Unknown exception", .nested = nullptr};
  }
  catch(const short num)
  {
    return {.message = std::format("{}", num), .nested = nullptr};
  }
  catch(const int num)
  {
    return {.message = std::format("{}", num), .nested = nullptr};
  }
  catch(const float num)
  {
    return {.message = std::format("{}", num), .nested = nullptr};
  }
  catch(const double num)
  {
    return {.message = std::format("{}", num), .nested = nullptr};
  }
  catch(const exception_base& ex)
  {
    return {.message = ex.str(), .nested = nested_exception_of(ex)};
  }
  catch(const std::exception& ex)
  {
    return {.message = ex.what(), .nested = nested_exception_of(ex)};
  }
  catch(...)
  {
    return {.message = "Unknown exception", .nested = nullptr};
  }
}

} // namespace arude::detail

namespace arude
{

///
/// Creates an exception report as string by reading all known exception types and unwinding nested exceptions.
/// Must be called from inside a catch handler; with no exception in flight the report says so instead of failing.
///
/// Nested exceptions are unwound by recursion, bounded by ARUDE_EXCEPTION_REPORT_MAX_DEPTH. Past that the report
/// stops and ends with a note saying it was truncated, rather than recursing until the stack is gone.
///
/// \return The exception report as string, truncated if the depth limit was reached.
/// \throws std::bad_alloc If the report string cannot be allocated. Callers in a catch(...) handler that must not
///         throw should guard the call accordingly.
///
[[nodiscard]] auto exception_report() -> exception_string_t;

///
/// Calls exception_report() with a given exception pointer and returns the report as string.
/// This function is useful when you have an exception pointer and want to generate a report without rethrowing the
/// exception.
///
/// \param eptr Exception pointer.
/// \return String containing the exception information, truncated if the depth limit was reached.
/// \throws std::bad_alloc If the report string cannot be allocated.
///
[[nodiscard]] auto exception_report(const std::exception_ptr& eptr) -> exception_string_t;

///
/// Returns the message of the exception being handled, and nothing else.
/// What to embed in the message of an exception thrown in its place: unlike
/// exception_report(), it carries no location and no stacktrace, so the report
/// of the exception thrown in its place names one location and holds one
/// stacktrace. A nested exception's message follows its outer one after ": ",
/// for up to ARUDE_EXCEPTION_REPORT_MAX_DEPTH levels. Must be called from
/// inside a catch handler; with no exception in flight it says so instead of
/// failing.
///
/// \return str() of an arude exception, what() of a std::exception, the text of
///         a thrown string literal, the value of a thrown char, short, int,
///         float or double, or a stand-in naming the exception as unknown.
/// \throws std::bad_alloc If the message string cannot be allocated.
///
[[nodiscard]] auto exception_message() -> exception_string_t;

///
///
// The recursion is the mechanism for unwinding nested exceptions, not an
// oversight; exception_report_guard is what bounds it.
// NOLINTNEXTLINE(misc-no-recursion)
inline auto exception_report() -> exception_string_t
{
  const auto guard = detail::exception_report_guard{};

  if(guard.exhausted())
  {
    return "Exception report truncated: nesting depth limit reached\n";
  }

  auto nested_string = exception_string_t{};

  try
  {
    try
    {
      if(auto ex = std::current_exception())
      {
        std::rethrow_exception(ex);
      }
      else
      {
        throw exception{"Exception processing without current exception not possible. "
                        "This function must be called from inside a catch handler"};
      }
    }
    catch(const char str)
    {
      nested_string = std::format("{}Exception (char): {}\n", nested_string, str);
    }
    catch(const char* const str)
    {
      nested_string = std::format("{}Exception (char*): {}\n", nested_string, str);
    }
    catch(const short num)
    {
      nested_string = std::format("{}Exception (short): {}\n", nested_string, num);
    }
    catch(const int num)
    {
      nested_string = std::format("{}Exception (int): {}\n", nested_string, num);
    }
    catch(const float num)
    {
      nested_string = std::format("{}Exception (float): {}\n", nested_string, num);
    }
    catch(const double num)
    {
      nested_string = std::format("{}Exception (double): {}\n", nested_string, num);
    }
    catch(const exception_base& ex)
    {
      nested_string = std::format("{}{}", nested_string, ex.to_string());
      detail::rethrow_if_nested_in(ex);
    }
// <expected> exists as a header on toolchains that do not implement it, so the
// include alone proves nothing; this is the feature test that does. Where it is
// missing the handlers simply go, and such an exception falls through to the
// std::exception branch below rather than being reported by its own name.
#if (defined __cpp_lib_expected)
    catch(const std::bad_expected_access<std::string>& ex)
    {
      nested_string += std::format("std::bad_expected_access - Error: {}", ex.error());
      detail::rethrow_if_nested_in(ex);
    }
    catch(const std::bad_expected_access<void>& ex)
    {
      nested_string += std::format("std::bad_expected_access {}", ex.what());
      detail::rethrow_if_nested_in(ex);
    }
#endif // #if (defined __cpp_lib_expected)
    catch(const std::system_error& ex)
    {
      nested_string += std::format("std::system_error: {} ({})", ex.what(), ex.code().value());
      detail::rethrow_if_nested_in(ex);
    }
    catch(const std::exception& ex)
    {
      nested_string += ex.what();
      detail::rethrow_if_nested_in(ex);
    }
    catch(...)
    {
      nested_string += "Unknown exception\n";
    }
  }
  catch(...)
  {
    nested_string = std::format("{}\n\n{}", exception_report(), nested_string);
  }

  return nested_string;
}

///
///
inline auto exception_report(const std::exception_ptr& eptr) -> exception_string_t
{
  try
  {
    if(eptr != nullptr)
    {
      std::rethrow_exception(eptr);
    }
    else
    {
      throw exception{"Exception processing without exception ptr not possible. "
                      "This function must be called with a valid exception_ptr"};
    }
  }
  catch(...)
  {
    return exception_report();
  }
}

///
///
inline auto exception_message() -> exception_string_t
{
  auto current = std::current_exception();

  if(current == nullptr)
  {
    return "No exception is being handled";
  }

  auto message = exception_string_t{};

  // A loop rather than recursion, so a cyclic or runaway chain stops at the
  // depth limit without the guard exception_report() needs.
  for(auto depth = 0; current != nullptr && depth < ARUDE_EXCEPTION_REPORT_MAX_DEPTH; ++depth)
  {
    if(depth > 0)
    {
      message += ": ";
    }

    auto link = detail::read_exception_link(current);
    message += link.message;
    current = std::move(link.nested);
  }

  return message;
}

} // namespace arude
