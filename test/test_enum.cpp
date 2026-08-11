///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///

#include "arude/enum.hpp"

#include "arude/exception.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string_view>
#include <tuple>

namespace enum_test
{

///
/// Contiguous from zero, which is the ordinary case.
///
// The default int base is the point: it is what most enumerations have, and the
// contrast with status below is what makes the promotion test mean anything.
// NOLINTNEXTLINE(performance-enum-size)
enum class colour
{
  red,
  green,
  blue
};

///
/// A character-like underlying type, so the failure message is checked against
/// the promotion that keeps it printing a number rather than a glyph.
///
enum class status : unsigned char
{
  ok = 1,
  failed = 2
};

} // namespace enum_test

SCENARIO("enum_c admits enumerations and nothing else", "[enum]")
{
  GIVEN("the concept")
  {
    THEN("an enumeration satisfies it")
    {
      STATIC_REQUIRE(arude::enum_c<enum_test::colour>);
      STATIC_REQUIRE(arude::enum_c<enum_test::status>);
    }

    THEN("a non-enumeration does not")
    {
      STATIC_REQUIRE(!arude::enum_c<int>);
      STATIC_REQUIRE(!arude::enum_c<std::string_view>);
    }
  }
}

SCENARIO("enum_cast_throw converts an underlying integer", "[enum]")
{
  using enum_test::colour;

  GIVEN("an integer matching an enumerator")
  {
    THEN("the enumerator comes back, and does so at compile time")
    {
      STATIC_REQUIRE(arude::enum_cast_throw<colour>(0) == colour::red);
      STATIC_REQUIRE(arude::enum_cast_throw<colour>(2) == colour::blue);
    }
  }

  GIVEN("an integer matching no enumerator")
  {
    THEN("it throws rather than returning an empty optional")
    {
      REQUIRE_THROWS_AS(arude::enum_cast_throw<colour>(99), arude::exception_base);
    }

    THEN("the message names the type and the value")
    {
      try
      {
        std::ignore = arude::enum_cast_throw<colour>(99);
        FAIL("enum_cast_throw returned instead of throwing");
      }
      catch(const arude::exception_base& ex)
      {
        REQUIRE(ex.str().contains("colour"));
        REQUIRE(ex.str().contains("99"));
      }
    }
  }

  // The underlying type is unsigned char, so an unpromoted value would format
  // as whichever character has that code rather than as the number.
  GIVEN("an enumeration with a character-like underlying type")
  {
    THEN("the message still reports the value as a number")
    {
      try
      {
        std::ignore = arude::enum_cast_throw<enum_test::status>(65);
        FAIL("enum_cast_throw returned instead of throwing");
      }
      catch(const arude::exception_base& ex)
      {
        REQUIRE(ex.str().contains("65"));
        REQUIRE(!ex.str().contains("A"));
      }
    }
  }
}

SCENARIO("enum_cast_throw converts a name", "[enum]")
{
  using enum_test::colour;

  GIVEN("a name matching an enumerator")
  {
    THEN("the enumerator comes back")
    {
      REQUIRE(arude::enum_cast_throw<colour>("green") == colour::green);
    }
  }

  GIVEN("a name differing only in case")
  {
    THEN("the exact-match default rejects it")
    {
      REQUIRE_THROWS_AS(arude::enum_cast_throw<colour>("GREEN"), arude::exception_base);
    }

    THEN("case_insensitive accepts it, so the predicate reaches enum_cast")
    {
      REQUIRE(arude::enum_cast_throw<colour>("GREEN", arude::case_insensitive) == colour::green);
    }
  }

  GIVEN("a name matching no enumerator")
  {
    THEN("the message names the type and the name")
    {
      try
      {
        std::ignore = arude::enum_cast_throw<colour>("puce");
        FAIL("enum_cast_throw returned instead of throwing");
      }
      catch(const arude::exception_base& ex)
      {
        REQUIRE(ex.str().contains("colour"));
        REQUIRE(ex.str().contains("puce"));
      }
    }
  }
}

// The whole reason enum_cast_throw takes a source_location: an exception
// reporting enum.hpp as the throw site would say nothing about which call
// failed. This also pins the exception<void> spelling in the definition, since
// deducing the payload instead would leave the location defaulted to enum.hpp.
SCENARIO("enum_cast_throw reports the caller's location", "[enum]")
{
  GIVEN("a failing conversion")
  {
    THEN("the exception points at this file, not at enum.hpp")
    {
      try
      {
        std::ignore = arude::enum_cast_throw<enum_test::colour>(99);
        FAIL("enum_cast_throw returned instead of throwing");
      }
      catch(const arude::exception_base& ex)
      {
        REQUIRE(std::string_view{ex.where().file_name()}.contains("test_enum.cpp"));
      }
    }
  }
}
