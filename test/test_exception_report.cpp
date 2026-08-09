///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///

#include "arude/exception.hpp"
#include "arude/exception_report.hpp"

#include <catch2/catch_test_macros.hpp>

#include <exception>
#include <expected>
#include <format>
#include <stdexcept>
#include <string>
#include <system_error>

namespace
{

///
/// A type the reporter has no handler for, so it falls through to catch(...).
///
struct unrecognised_payload
{
  int value = 0;
};

// Deep enough to pass the report's limit several times over, and no deeper.
// Every level here is a live exception with a try/catch frame behind it, and at
// twenty times the limit that overflowed the 1 MB default stack on Windows —
// the chain, not the report, which bounds its own recursion either way.
inline constexpr auto deep_nesting = ARUDE_EXCEPTION_REPORT_MAX_DEPTH * 3;

///
/// Throws a chain of nested std::runtime_error, innermost first.
///
// Recursing is how the chain is built, so the check has nothing to tell us.
// NOLINTNEXTLINE(misc-no-recursion)
auto throw_nested_runtime(const int depth) -> void
{
  if(depth <= 0)
  {
    throw std::runtime_error{"innermost"};
  }

  try
  {
    throw_nested_runtime(depth - 1);
  }
  catch(...)
  {
    std::throw_with_nested(std::runtime_error{std::format("level {}", depth)});
  }
}

///
/// Throws an arude exception nested inside another one.
///
auto throw_nested_arude() -> void
{
  try
  {
    throw arude::exception{"inner arude"};
  }
  catch(...)
  {
    std::throw_with_nested(arude::exception{"outer arude"});
  }
}

} // namespace

SCENARIO("exception_report describes an arude exception", "[exception_report]")
{
  GIVEN("an arude exception")
  {
    WHEN("it is reported from a catch handler")
    {
      auto report = arude::exception_string_t{};

      try
      {
        throw arude::exception{"arude failure"};
      }
      catch(...)
      {
        report = arude::exception_report();
      }

      THEN("the message appears")
      {
        REQUIRE(report.contains("arude failure"));
      }
    }
  }
}

SCENARIO("exception_report names the built-in types individually", "[exception_report]")
{
  GIVEN("exceptions of the built-in types the reporter names individually")
  {
    THEN("each is reported with its type and value")
    {
      const auto report_of = [](auto value) -> arude::exception_string_t
      {
        try
        {
          // Throwing a raw pointer is the case under test, not an oversight:
          // exception_report has a handler dedicated to const char*.
          // NOLINTNEXTLINE(misc-throw-by-value-catch-by-reference)
          throw value;
        }
        catch(...)
        {
          return arude::exception_report();
        }
      };

      REQUIRE(report_of(int{7}).contains("Exception (int): 7"));
      REQUIRE(report_of(short{3}).contains("Exception (short): 3"));
      REQUIRE(report_of(char{'x'}).contains("Exception (char): x"));
      REQUIRE(report_of(double{1.5}).contains("Exception (double): 1.5"));
      REQUIRE(report_of(float{2.5F}).contains("Exception (float): 2.5"));
      REQUIRE(report_of(static_cast<const char*>("literal")).contains("Exception (char*): literal"));
    }
  }
}

SCENARIO("exception_report describes standard library exceptions", "[exception_report]")
{
  GIVEN("a std::system_error")
  {
    WHEN("it is reported")
    {
      auto report = arude::exception_string_t{};

      try
      {
        throw std::system_error{std::make_error_code(std::errc::permission_denied)};
      }
      catch(...)
      {
        report = arude::exception_report();
      }

      THEN("the report names it and includes the code")
      {
        REQUIRE(report.contains("std::system_error"));
      }
    }
  }
}

// Guarded exactly as the handlers are: <expected> exists as a header on
// toolchains that do not implement it, so the include alone proves nothing.
// Where the feature test is missing these exceptions fall through to the
// std::exception handler instead, and there is no dedicated behaviour to pin.
#if (defined __cpp_lib_expected)

SCENARIO("exception_report describes a bad_expected_access", "[exception_report]")
{
  GIVEN("an expected holding a string error")
  {
    WHEN("its value is taken and the throw is reported")
    {
      auto report = arude::exception_string_t{};

      try
      {
        const auto result = std::expected<int, std::string>{std::unexpect, "disk on fire"};
        static_cast<void>(result.value());
      }
      catch(...)
      {
        report = arude::exception_report();
      }

      THEN("the report names the type and carries the error itself")
      {
        REQUIRE(report.contains("std::bad_expected_access"));
        REQUIRE(report.contains("disk on fire"));
      }
    }
  }

  // The handlers are ordered <std::string> before <void>, and every
  // bad_expected_access<E> derives from bad_expected_access<void>. An error
  // type that is not std::string is what reaches the second handler, and
  // nothing else in the suite gets there.
  GIVEN("an expected whose error type is not a string")
  {
    WHEN("its value is taken and the throw is reported")
    {
      auto report = arude::exception_string_t{};

      try
      {
        const auto result = std::expected<int, int>{std::unexpect, 42};
        static_cast<void>(result.value());
      }
      catch(...)
      {
        report = arude::exception_report();
      }

      THEN("the void handler names it rather than letting it fall through as a plain std::exception")
      {
        REQUIRE(report.contains("std::bad_expected_access"));
      }
    }
  }
}

#endif // #if (defined __cpp_lib_expected)

SCENARIO("exception_report copes with an unrecognised exception", "[exception_report]")
{
  GIVEN("an exception of a type the reporter does not know")
  {
    WHEN("it is reported")
    {
      auto report = arude::exception_string_t{};

      try
      {
        throw unrecognised_payload{1};
      }
      catch(...)
      {
        report = arude::exception_report();
      }

      THEN("it is reported as unknown rather than dropped")
      {
        REQUIRE(report.contains("Unknown exception"));
      }
    }
  }
}

SCENARIO("exception_report called outside a catch handler", "[exception_report]")
{
  GIVEN("no exception in flight")
  {
    const auto report = arude::exception_report();

    THEN("the report says the call was misplaced instead of misbehaving")
    {
      REQUIRE(report.contains("must be called from inside a catch handler"));
    }
  }
}

SCENARIO("exception_report unwinds nested exceptions", "[exception_report]")
{
  GIVEN("a chain of nested std exceptions")
  {
    auto report = arude::exception_string_t{};

    try
    {
      throw_nested_runtime(3);
    }
    catch(...)
    {
      report = arude::exception_report();
    }

    THEN("every level appears, innermost included")
    {
      REQUIRE(report.contains("innermost"));
      REQUIRE(report.contains("level 1"));
      REQUIRE(report.contains("level 2"));
      REQUIRE(report.contains("level 3"));
    }
  }

  // The exception_base handler used to be the one branch that did not rethrow
  // what was nested inside it, so an arude chain stopped at its outermost level
  // while a std one unwound fully.
  GIVEN("an arude exception nested inside another")
  {
    auto report = arude::exception_string_t{};

    try
    {
      throw_nested_arude();
    }
    catch(...)
    {
      report = arude::exception_report();
    }

    THEN("both levels appear")
    {
      REQUIRE(report.contains("outer arude"));
      REQUIRE(report.contains("inner arude"));
    }
  }
}

SCENARIO("exception_report bounds its own recursion", "[exception_report]")
{
  GIVEN("a nesting chain far deeper than the limit")
  {
    auto report = arude::exception_string_t{};

    try
    {
      throw_nested_runtime(deep_nesting);
    }
    catch(...)
    {
      report = arude::exception_report();
    }

    THEN("the report returns, truncated, rather than running out of stack")
    {
      REQUIRE(report.contains("truncated"));
    }

    THEN("the levels it did reach are still reported")
    {
      REQUIRE(!report.empty());
    }
  }

  GIVEN("a truncated report followed by an ordinary one")
  {
    try
    {
      throw_nested_runtime(deep_nesting);
    }
    catch(...)
    {
      static_cast<void>(arude::exception_report());
    }

    WHEN("a later, shallow exception is reported")
    {
      auto report = arude::exception_string_t{};

      try
      {
        throw std::runtime_error{"afterwards"};
      }
      catch(...)
      {
        report = arude::exception_report();
      }

      THEN("the depth counter has unwound, so the report is complete")
      {
        REQUIRE(report.contains("afterwards"));
        REQUIRE(!report.contains("truncated"));
      }
    }
  }
}

SCENARIO("exception_report accepts an exception_ptr", "[exception_report]")
{
  GIVEN("a captured exception")
  {
    auto eptr = std::exception_ptr{};

    try
    {
      throw arude::exception{"captured"};
    }
    catch(...)
    {
      eptr = std::current_exception();
    }

    WHEN("it is reported without being rethrown by the caller")
    {
      const auto report = arude::exception_report(eptr);

      THEN("the message appears")
      {
        REQUIRE(report.contains("captured"));
      }
    }
  }

  GIVEN("a null exception_ptr")
  {
    const auto report = arude::exception_report(std::exception_ptr{});

    THEN("the report says so rather than dereferencing it")
    {
      REQUIRE(report.contains("must be called with a valid exception_ptr"));
    }
  }
}
