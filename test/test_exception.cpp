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
#include <string_view>
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
/// Payload that is formattable but whose formatter always throws.
/// Formatting is the one thing to_string does, so this is what drives it into
/// its catch(...); nothing a caller can pass will otherwise get there.
///
struct throwing_payload
{
  int value = 0;
};

} // namespace

template<>
struct std::formatter<formattable_payload> : formatter<string>
{
  auto format(const auto& val, auto& ctx) const { return format_to(ctx.out(), "payload({})", val.value); }
};

// The context stays a deduced parameter, and the return type is spelled from
// it: std::formattable checks format() against a context type that need not be
// std::format_context, and a signature naming that type outright fails the
// concept, which silently takes the payload down the not-formattable branch
// instead of the one under test.
template<>
struct std::formatter<throwing_payload> : formatter<string>
{
  auto format([[maybe_unused]] const throwing_payload& val, auto& ctx) const -> decltype(ctx.out())
  {
    throw format_error{"throwing_payload has no representation"};
  }
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
      REQUIRE(ex.stack().empty() != arude::stacktrace_available);
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

  // to_string is called from arude::exception_report(), which runs inside a
  // catch handler. A second exception escaping there would replace a
  // diagnostic with a std::terminate, so the fallback is load-bearing.
  GIVEN("a payload whose formatter throws")
  {
    const auto ex = arude::exception{"failed", throwing_payload{1}};

    THEN("to_string reports the formatting failure instead of propagating it")
    {
      REQUIRE_NOTHROW(ex.to_string());
      REQUIRE(ex.to_string() == "Error formatting exception details");
    }

    THEN("formatting through the base is equally safe, since it goes the same way")
    {
      REQUIRE_NOTHROW(std::format("{}", static_cast<const arude::exception_base&>(ex)));
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
