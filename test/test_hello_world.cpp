///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///

#include "arude/hello_world.hpp"

#include <catch2/catch_test_macros.hpp>

#include <limits>

SCENARIO("hello_world returns what it is given", "[hello_world]")
{
  GIVEN("an ordinary value")
  {
    const int value = 42;

    WHEN("it is passed to hello_world")
    {
      const int result = arude::hello_world(value);

      THEN("the same value comes back")
      {
        REQUIRE(result == value);
      }
    }
  }

  GIVEN("zero")
  {
    const int value = 0;

    WHEN("it is passed to hello_world")
    {
      const int result = arude::hello_world(value);

      THEN("zero comes back")
      {
        REQUIRE(result == value);
      }
    }
  }

  GIVEN("the extremes of the range")
  {
    WHEN("the minimum is passed to hello_world")
    {
      const int result = arude::hello_world(std::numeric_limits<int>::min());

      THEN("the minimum comes back")
      {
        REQUIRE(result == std::numeric_limits<int>::min());
      }
    }

    WHEN("the maximum is passed to hello_world")
    {
      const int result = arude::hello_world(std::numeric_limits<int>::max());

      THEN("the maximum comes back")
      {
        REQUIRE(result == std::numeric_limits<int>::max());
      }
    }
  }
}
