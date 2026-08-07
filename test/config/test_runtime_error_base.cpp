///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///

// Built into its own executable, with ARUDE_EXCEPTION_RUNTIME_ERROR_BASE set by
// CMake rather than defined here, so the setting cannot drift from the target.
// This cannot join the main test binary: the macro changes exception_base's
// base class, and two definitions of that class in one program is an ODR
// violation no amount of care inside the file would fix.

#include "arude/exception.hpp"

#include <catch2/catch_test_macros.hpp>

#include <exception>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

static_assert(
  std::is_base_of_v<std::runtime_error, arude::exception_base>,
  "ARUDE_EXCEPTION_RUNTIME_ERROR_BASE must put std::runtime_error under exception_base.");

SCENARIO("the runtime_error base makes arude exceptions std exceptions", "[exception][runtime_error_base]")
{
  GIVEN("an arude exception")
  {
    WHEN("it is caught as a std::exception")
    {
      auto message = std::string{};

      try
      {
        throw arude::exception{"boom"};
      }
      catch(const std::exception& ex)
      {
        message = ex.what();
      }

      THEN("what() carries the message the exception was built with")
      {
        REQUIRE(message == "boom");
      }
    }

    WHEN("it is caught as an arude exception")
    {
      auto message = std::string{};

      try
      {
        throw arude::exception{"boom"};
      }
      catch(const arude::exception_base& ex)
      {
        message = ex.str();
      }

      THEN("str() still returns the message, so both accessors agree")
      {
        REQUIRE(message == "boom");
      }
    }
  }

  // std::runtime_error keeps its own copy of the message it was constructed
  // with, and exception_base does not override what(). The two therefore drift
  // apart as soon as str() is used to edit the message. Pinning that down here
  // so the divergence is a decision on record rather than a surprise.
  GIVEN("an exception whose message is edited after construction")
  {
    auto from_what = std::string{};
    auto from_str = std::string{};

    try
    {
      auto ex = arude::exception{"original"};
      ex.str() = "modified";

      // Throwing the named object is the point: the edit has to survive the
      // copy the throw makes, which an anonymous temporary could not show.
      // NOLINTNEXTLINE(misc-throw-by-value-catch-by-reference)
      throw ex;
    }
    catch(const arude::exception_base& ex)
    {
      from_what = ex.what();
      from_str = ex.str();
    }

    THEN("str() reports the edit")
    {
      REQUIRE(from_str == "modified");
    }

    THEN("what() still serves the message runtime_error was constructed with")
    {
      REQUIRE(from_what == "original");
    }
  }
}

// exception_report orders its exception_base handler ahead of its
// std::exception one. That ordering only matters in this configuration, where
// an arude exception satisfies both.
SCENARIO("exception_report still prefers the arude handler", "[exception][runtime_error_base]")
{
  GIVEN("an arude exception that is also a std::exception")
  {
    WHEN("it is reported")
    {
      auto report = arude::exception_string_t{};

      try
      {
        throw arude::exception{"reported once"};
      }
      catch(...)
      {
        report = arude::exception_report();
      }

      THEN("the report is the arude one, with location and stacktrace")
      {
        REQUIRE(report.contains("reported once"));
        REQUIRE(report.contains("location:"));
        REQUIRE(report.contains("stacktrace:"));
      }
    }
  }
}

SCENARIO("the runtime_error base does not disturb the rest of exception_base", "[exception][runtime_error_base]")
{
  GIVEN("an exception carrying a payload")
  {
    const auto ex = arude::exception{"failed", 42};

    THEN("the payload, location and stacktrace are unaffected")
    {
      REQUIRE(ex.data() == 42);
      REQUIRE(ex.str() == "failed");
      REQUIRE(ex.stack().empty() != arude::stacktrace_available);
      REQUIRE(std::string_view{ex.where().file_name()}.contains("test_runtime_error_base.cpp"));
    }
  }

  GIVEN("a nested pair of arude exceptions")
  {
    auto report = arude::exception_string_t{};

    try
    {
      try
      {
        throw arude::exception{"inner"};
      }
      catch(...)
      {
        std::throw_with_nested(arude::exception{"outer"});
      }
    }
    catch(...)
    {
      report = arude::exception_report();
    }

    THEN("both levels are still unwound")
    {
      REQUIRE(report.contains("outer"));
      REQUIRE(report.contains("inner"));
    }
  }
}
