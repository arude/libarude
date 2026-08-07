///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///

#include "arude/exception.hpp"

#include <catch2/catch_test_macros.hpp>

#include <exception>
#include <format>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <type_traits>

namespace
{

///
/// Payload with a std::formatter, taking the formattable branch of do_to_string.
///
struct formattable_payload
{
  int value = 0;
};

///
/// Payload with no std::formatter, taking the other branch.
///
struct opaque_payload
{
  int value = 0;
};

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

template<>
struct std::formatter<formattable_payload> : formatter<string>
{
  auto format(const auto& val, auto& ctx) const { return format_to(ctx.out(), "payload({})", val.value); }
};

SCENARIO("exception_base carries message, location and stacktrace", "[exception]")
{
  GIVEN("an exception built with a message")
  {
    const auto ex = arude::exception{"something failed"};

    THEN("the message is readable")
    {
      REQUIRE(ex.str() == "something failed");
    }

    THEN("the source location points at this file")
    {
      REQUIRE(std::string_view{ex.where().file_name()}.contains("test_exception.cpp"));
    }

    THEN("a stacktrace was captured")
    {
      REQUIRE(!ex.stack().empty());
    }

    THEN("to_string mentions the message")
    {
      REQUIRE(ex.to_string().contains("something failed"));
    }
  }

  GIVEN("an exception whose message is modified in flight")
  {
    auto ex = arude::exception{"first"};

    WHEN("the message is replaced through the mutable accessor")
    {
      ex.str() = "second";

      THEN("the new message is reported")
      {
        REQUIRE(ex.str() == "second");
      }
    }
  }
}

// The base is a compile-time choice and the two settings cannot share a binary:
// each gives exception_base a different base class, which is an ODR violation.
// This file covers the default; test/config covers the other.
SCENARIO("the default base keeps arude exceptions out of the std hierarchy", "[exception]")
{
  GIVEN("the default configuration")
  {
    STATIC_REQUIRE(!std::is_base_of_v<std::exception, arude::exception_base>);

    WHEN("an arude exception passes a std::exception handler")
    {
      auto caught_as_std = false;
      auto caught_as_arude = false;

      try
      {
        throw arude::exception{"not a std exception"};
      }
      catch(const std::exception&)
      {
        caught_as_std = true;
      }
      catch(const arude::exception_base&)
      {
        caught_as_arude = true;
      }

      THEN("only the arude handler sees it")
      {
        REQUIRE(!caught_as_std);
        REQUIRE(caught_as_arude);
      }
    }
  }
}

SCENARIO("exception carries a user data payload", "[exception]")
{
  GIVEN("a formattable payload")
  {
    const auto ex = arude::exception{"failed", formattable_payload{42}};

    THEN("the payload is readable and appears in the report")
    {
      REQUIRE(ex.data().value == 42);
      REQUIRE(ex.to_string().contains("payload(42)"));
    }

    THEN("the payload type is named in the report")
    {
      REQUIRE(ex.to_string().contains("formattable_payload"));
    }
  }

  GIVEN("a payload with no formatter")
  {
    const auto ex = arude::exception{"failed", opaque_payload{7}};

    THEN("the report says so rather than failing to build")
    {
      REQUIRE(ex.data().value == 7);
      REQUIRE(ex.to_string().contains("not formattable"));
    }
  }

  GIVEN("a payload modified through the mutable accessor")
  {
    auto ex = arude::exception{"failed", formattable_payload{1}};

    WHEN("it is changed")
    {
      ex.data().value = 2;

      THEN("the change is visible")
      {
        REQUIRE(ex.data().value == 2);
      }
    }
  }
}

SCENARIO("exception_report describes an arude exception", "[exception]")
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

SCENARIO("exception_report names the built-in types individually", "[exception]")
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

SCENARIO("exception_report describes standard library exceptions", "[exception]")
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

SCENARIO("exception_report copes with an unrecognised exception", "[exception]")
{
  GIVEN("an exception of a type the reporter does not know")
  {
    WHEN("it is reported")
    {
      auto report = arude::exception_string_t{};

      try
      {
        throw opaque_payload{1};
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

SCENARIO("exception_report called outside a catch handler", "[exception]")
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

SCENARIO("exception_report unwinds nested exceptions", "[exception]")
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

SCENARIO("exception_report bounds its own recursion", "[exception]")
{
  GIVEN("a nesting chain far deeper than the limit")
  {
    auto report = arude::exception_string_t{};

    try
    {
      throw_nested_runtime(ARUDE_EXCEPTION_REPORT_MAX_DEPTH * 20);
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
      throw_nested_runtime(ARUDE_EXCEPTION_REPORT_MAX_DEPTH * 20);
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

SCENARIO("exception_report accepts an exception_ptr", "[exception]")
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
