///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///

#include "arude/config.hpp"
#include "arude/config/binary.hpp"
#include "arude/exception.hpp"
#include "test/helpers/temp_file.hpp"
#include "test/helpers/write_text.hpp"

#include <catch2/catch_test_macros.hpp>
#include <rfl/toml/read.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace
{
namespace binary_test
{

///
/// A table holding a payload, so the payload is read from inside a table
/// rather than from the top level of the text.
///
struct credentials
{
  ///
  /// The payload, as base64 text in the file.
  ///
  arude::config::binary secret;
};

///
/// A configuration whose payload sits one table down.
///
struct vault
{
  ///
  /// The version this type is.
  ///
  static constexpr auto config_version = arude::config::version_t{1};

  ///
  /// The table holding the payload.
  ///
  binary_test::credentials credentials;
};

} // namespace binary_test

} // namespace

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

SCENARIO("binary's reflector reports text that is not base64 as a std::exception", "[config][binary]")
{
  using reflector_t = rfl::Reflector<arude::config::binary>;

  GIVEN("a payload")
  {
    const auto payload = arude::config::binary{
      std::array{std::byte{'f'}, std::byte{'o'}, std::byte{'o'}}
    };

    THEN("the reflector renders it as base64 and reads it back")
    {
      REQUIRE(reflector_t::from(payload) == "Zm9v");
      REQUIRE(reflector_t::to(reflector_t::from(payload)) == payload);
    }
  }

  GIVEN("text that is not base64")
  {
    const auto text = std::string{"!!!!"};

    THEN("the reflector throws std::invalid_argument carrying the decoder's message")
    {
      // reflect-cpp calls the reflector from a noexcept parser that catches
      // std::exception and nothing else. Any other type ends the process.
      try
      {
        std::ignore = reflector_t::to(text);
        FAIL("the reflector should have thrown");
      }
      catch(const std::invalid_argument& ex)
      {
        REQUIRE(std::string_view{ex.what()}.contains("is not a base64 character"));
      }
    }

    WHEN("reflect-cpp's own TOML parser reads it into a payload")
    {
      const auto result = rfl::toml::read<binary_test::credentials>(std::string{"secret = \"!!!!\"\n"});

      THEN("it is a parse error, and the process carries on")
      {
        REQUIRE(!result.has_value());
        REQUIRE(std::string{result.error().what()}.contains("is not a base64 character"));
      }
    }
  }
}

SCENARIO("a binary field in a table that is not base64 fails the load as invalid_payload", "[config][binary]")
{
  GIVEN("TOML text whose table holds a payload that is not base64")
  {
    constexpr auto text = std::string_view{"version = 1\n[credentials]\nsecret = \"!!!!\"\n"};

    THEN("parsing it as a configuration is an invalid_payload naming the field and the base64 failure")
    {
      try
      {
        std::ignore = arude::config::from_toml<binary_test::vault>(text);
        FAIL("parsing a payload that is not base64 should have thrown");
      }
      catch(const arude::config::exception_t& ex)
      {
        REQUIRE(ex.data() == arude::config::config_error::invalid_payload);
        REQUIRE(ex.str().contains("'secret'"));
        REQUIRE(ex.str().contains("is not a base64 character"));
      }
    }
  }

  GIVEN("a file whose source holds a payload that is not base64")
  {
    const auto file = test_helpers::temp_file{"arude_binary_bad_file.toml"};
    test_helpers::write_text(file.path(), "version = 1\n[credentials]\nsecret = \"!!!!\"\n");

    auto config = arude::config::config_file<binary_test::vault, arude::config::transport_file>{
      arude::config::transport_file{file.path()}};

    THEN("loading the file is an invalid_payload naming the field and the base64 failure")
    {
      try
      {
        config.load();
        FAIL("loading a payload that is not base64 should have thrown");
      }
      catch(const arude::config::exception_t& ex)
      {
        REQUIRE(ex.data() == arude::config::config_error::invalid_payload);
        REQUIRE(ex.str().contains("'secret'"));
        REQUIRE(ex.str().contains("is not a base64 character"));
      }

      REQUIRE(!config.loaded());
    }
  }
}
