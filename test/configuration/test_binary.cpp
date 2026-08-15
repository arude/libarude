///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///

#include "arude/config/binary.hpp"
#include "arude/exception.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <vector>

SCENARIO("binary holds any range of bytes", "[config][binary]")
{
  GIVEN("a default constructed payload")
  {
    const auto payload = arude::config::binary{};

    THEN("it is empty and encodes to nothing")
    {
      REQUIRE(payload.empty());
      REQUIRE(payload.bytes().empty());
      REQUIRE(payload.base64().empty());
    }
  }

  GIVEN("an array, a vector and a span over the same bytes")
  {
    constexpr auto data = std::array{std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE}, std::byte{0xEF}};
    const auto vector = std::vector<std::byte>{data.begin(), data.end()};

    WHEN("a payload is built from each")
    {
      const auto from_array = arude::config::binary{data};
      const auto from_vector = arude::config::binary{vector};
      const auto from_span = arude::config::binary{std::span{vector}};

      THEN("all three hold the same bytes")
      {
        REQUIRE(from_array == from_vector);
        REQUIRE(from_array == from_span);
        REQUIRE(from_array.size() == data.size());
        REQUIRE(std::ranges::equal(from_array.bytes(), data));
      }
    }
  }

  GIVEN("a payload built from a span")
  {
    auto vector = std::vector<std::byte>{std::byte{0x01}, std::byte{0x02}};
    const auto payload = arude::config::binary{std::span{vector}};

    WHEN("what the span pointed at is changed")
    {
      vector.at(0) = std::byte{0xFF};

      THEN("the payload is unaffected, because it copied")
      {
        REQUIRE(payload.bytes().front() == std::byte{0x01});
      }
    }
  }
}

SCENARIO("binary converts to and from base64", "[config][binary]")
{
  GIVEN("bytes with a known encoding")
  {
    constexpr auto data = std::array{std::byte{'f'}, std::byte{'o'}, std::byte{'o'}};

    THEN("the payload encodes to it, at compile time as well")
    {
      STATIC_REQUIRE(arude::config::binary{data}.base64() == "Zm9v");

      REQUIRE(arude::config::binary{data}.base64() == "Zm9v");
    }
  }

  GIVEN("a payload holding bytes")
  {
    constexpr auto data = std::array{std::byte{0x00}, std::byte{0xFF}, std::byte{0x2A}};
    const auto payload = arude::config::binary{data};

    WHEN("the encoding is read back into another payload")
    {
      auto restored = arude::config::binary{};
      restored.base64(payload.base64());

      THEN("the two are equal")
      {
        REQUIRE(restored == payload);
      }
    }
  }

  GIVEN("a payload and text that is not base64")
  {
    auto payload = arude::config::binary{std::vector{std::byte{0x01}}};

    WHEN("the text is assigned")
    {
      THEN("it throws and leaves the payload as it was")
      {
        REQUIRE_THROWS_AS(payload.base64("not base64"), arude::exception_base);
        REQUIRE(payload.size() == 1);
        REQUIRE(payload.bytes().front() == std::byte{0x01});
      }
    }
  }
}

SCENARIO("binary is a value type", "[config][binary]")
{
  GIVEN("a payload")
  {
    const auto initial = std::vector{std::byte{0x01}, std::byte{0x02}};
    auto payload = arude::config::binary{initial};

    WHEN("its bytes are replaced")
    {
      payload.bytes(std::vector{std::byte{0x03}});

      THEN("it holds the new bytes alone")
      {
        REQUIRE(payload.size() == 1);
        REQUIRE(payload == arude::config::binary{std::vector{std::byte{0x03}}});
      }
    }

    WHEN("it is copied")
    {
      const auto copy = payload;

      THEN("the copy compares equal and is independent")
      {
        REQUIRE(copy == payload);

        payload.bytes({});

        REQUIRE(copy.size() == 2);
        REQUIRE(!(copy == payload));
      }
    }
  }
}
