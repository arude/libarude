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

#include "arude/exception.hpp"

#include <cstddef>
#include <exception>
#include <expected>
#include <format>
#include <string>
#include <system_error>

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
      std::rethrow_if_nested(ex);
    }
// <expected> exists as a header on toolchains that do not implement it, so the
// include alone proves nothing; this is the feature test that does. Where it is
// missing the handlers simply go, and such an exception falls through to the
// std::exception branch below rather than being reported by its own name.
#if (defined __cpp_lib_expected)
    catch(const std::bad_expected_access<std::string>& ex)
    {
      nested_string += std::format("std::bad_expected_access - Error: {}", ex.error());
      std::rethrow_if_nested(ex);
    }
    catch(const std::bad_expected_access<void>& ex)
    {
      nested_string += std::format("std::bad_expected_access {}", ex.what());
      std::rethrow_if_nested(ex);
    }
#endif // #if (defined __cpp_lib_expected)
    catch(const std::system_error& ex)
    {
      nested_string += std::format("std::system_error: {} ({})", ex.what(), ex.code().value());
      std::rethrow_if_nested(ex);
    }
    catch(const std::exception& ex)
    {
      nested_string += ex.what();
      std::rethrow_if_nested(ex);
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

} // namespace arude
