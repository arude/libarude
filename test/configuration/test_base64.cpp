///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///

#include "arude/config/base64.hpp"
#include "arude/exception.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace
{
namespace base64_test
{

///
/// Encodes text a character at a time, so a test can be written in terms of
/// what the RFC's vectors actually say.
/// std::as_bytes is not usable here: it casts, and a cast is not a constant
/// expression, which would cost the compile-time half of these tests.
///
/// \param text Text to encode.
/// \return The encoded text.
///
[[nodiscard]] constexpr auto encode_text(const std::string_view text) -> std::string
{
  auto data = std::vector<std::byte>{};
  data.reserve(text.size());

  for(const auto character : text)
  {
    data.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
  }

  return arude::config::base64_encode(data);
}

///
/// Decodes base64 back to text, the inverse of encode_text().
///
/// \param text Encoded text.
/// \return The decoded text.
/// \throws arude::exception If the text is not valid base64.
///
[[nodiscard]] constexpr auto decode_text(const std::string_view text) -> std::string
{
  auto decoded = std::string{};

  for(const auto value : arude::config::base64_decode(text))
  {
    decoded.push_back(static_cast<char>(std::to_integer<unsigned char>(value)));
  }

  return decoded;
}

} // namespace base64_test

} // namespace

// The vectors from RFC 4648 section 10, which cover all three lengths a final
// group can have and are what every other base64 implementation is tested
// against.
SCENARIO("base64_encode produces the RFC 4648 test vectors", "[config][base64]")
{
  GIVEN("the inputs the RFC lists")
  {
    THEN("the encoding matches, at compile time as well as at run time")
    {
      STATIC_REQUIRE(base64_test::encode_text("").empty());
      STATIC_REQUIRE(base64_test::encode_text("f") == "Zg==");
      STATIC_REQUIRE(base64_test::encode_text("fo") == "Zm8=");
      STATIC_REQUIRE(base64_test::encode_text("foo") == "Zm9v");
      STATIC_REQUIRE(base64_test::encode_text("foob") == "Zm9vYg==");
      STATIC_REQUIRE(base64_test::encode_text("fooba") == "Zm9vYmE=");
      STATIC_REQUIRE(base64_test::encode_text("foobar") == "Zm9vYmFy");

      REQUIRE(base64_test::encode_text("foobar") == "Zm9vYmFy");
    }
  }
}

SCENARIO("base64_decode inverts base64_encode", "[config][base64]")
{
  GIVEN("the RFC's vectors, read the other way round")
  {
    THEN("the bytes come back")
    {
      STATIC_REQUIRE(base64_test::decode_text("").empty());
      STATIC_REQUIRE(base64_test::decode_text("Zg==") == "f");
      STATIC_REQUIRE(base64_test::decode_text("Zm8=") == "fo");
      STATIC_REQUIRE(base64_test::decode_text("Zm9v") == "foo");
      STATIC_REQUIRE(base64_test::decode_text("Zm9vYg==") == "foob");
      STATIC_REQUIRE(base64_test::decode_text("Zm9vYmE=") == "fooba");
      STATIC_REQUIRE(base64_test::decode_text("Zm9vYmFy") == "foobar");
    }
  }

  GIVEN("bytes that are not text at all")
  {
    const auto data = std::array{std::byte{0x00}, std::byte{0xFF}, std::byte{0x10}, std::byte{0x80}, std::byte{0x7F}};

    WHEN("they are encoded and decoded again")
    {
      const auto decoded = arude::config::base64_decode(arude::config::base64_encode(data));

      THEN("the round trip is exact")
      {
        REQUIRE(std::ranges::equal(decoded, data));
      }
    }
  }

  GIVEN("every byte value, so no bit pattern is special-cased")
  {
    auto data = std::vector<std::byte>{};

    for(auto value = std::size_t{0}; value < 256; ++value)
    {
      data.push_back(static_cast<std::byte>(value));
    }

    WHEN("the range is encoded and decoded again")
    {
      const auto encoded = arude::config::base64_encode(data);

      THEN("the round trip is exact and the text is padded to a multiple of four")
      {
        REQUIRE(encoded.size() % 4 == 0);
        REQUIRE(std::ranges::equal(arude::config::base64_decode(encoded), data));
      }
    }
  }
}

SCENARIO("base64_decode rejects what base64_encode cannot have produced", "[config][base64]")
{
  GIVEN("text whose length is not a multiple of four")
  {
    THEN("it is refused rather than decoded as far as it goes")
    {
      REQUIRE_THROWS_AS(arude::config::base64_decode("Zm9"), arude::exception_base);
    }
  }

  GIVEN("text holding a character outside the alphabet")
  {
    THEN("it is refused")
    {
      REQUIRE_THROWS_AS(arude::config::base64_decode("Zm9$"), arude::exception_base);
      REQUIRE_THROWS_AS(arude::config::base64_decode("Zm9\n"), arude::exception_base);
      REQUIRE_THROWS_AS(arude::config::base64_decode("Zm9 "), arude::exception_base);
    }
  }

  GIVEN("padding somewhere other than the end")
  {
    THEN("it is refused")
    {
      REQUIRE_THROWS_AS(arude::config::base64_decode("Zg==Zg=="), arude::exception_base);
      REQUIRE_THROWS_AS(arude::config::base64_decode("Z=g="), arude::exception_base);
      REQUIRE_THROWS_AS(arude::config::base64_decode("=Zg="), arude::exception_base);
    }
  }
}

SCENARIO("the base64 size helpers agree with the codec", "[config][base64]")
{
  GIVEN("byte counts either side of a group boundary")
  {
    THEN("the encoded length is what encoding produces")
    {
      STATIC_REQUIRE(arude::config::base64_encoded_size(0) == 0);
      STATIC_REQUIRE(arude::config::base64_encoded_size(1) == 4);
      STATIC_REQUIRE(arude::config::base64_encoded_size(3) == 4);
      STATIC_REQUIRE(arude::config::base64_encoded_size(4) == 8);

      REQUIRE(base64_test::encode_text("f").size() == arude::config::base64_encoded_size(1));
      REQUIRE(base64_test::encode_text("foobar").size() == arude::config::base64_encoded_size(6));
    }
  }

  GIVEN("encoded text with and without padding")
  {
    THEN("the decoded length is what decoding produces")
    {
      STATIC_REQUIRE(arude::config::base64_decoded_size("") == 0);
      STATIC_REQUIRE(arude::config::base64_decoded_size("Zg==") == 1);
      STATIC_REQUIRE(arude::config::base64_decoded_size("Zm8=") == 2);
      STATIC_REQUIRE(arude::config::base64_decoded_size("Zm9v") == 3);
      STATIC_REQUIRE(arude::config::base64_decoded_size("Zm9vYmFy") == 6);
    }
  }
}
