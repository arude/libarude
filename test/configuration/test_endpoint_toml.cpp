///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///

#include "arude/config.hpp"
#include "arude/exception.hpp"
#include "test/helpers/config_test_v2.hpp"
#include "test/helpers/temp_file.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <string_view>

namespace
{
namespace endpoint_toml_test
{

///
/// A current-version configuration with nothing left at its default.
/// \return The configuration.
///
[[nodiscard]] auto make_current() -> test_helpers::config_test_t
{
  constexpr auto secret = std::array{std::byte{'f'}, std::byte{'o'}, std::byte{'o'}};

  return test_helpers::config_test_t{
    .name = "example", .retries = 7, .endpoint = "example.invalid", .secret = arude::config::binary{secret}};
}

} // namespace endpoint_toml_test

} // namespace

SCENARIO("a configuration survives being rendered and parsed back", "[config][toml]")
{
  GIVEN("a configuration")
  {
    const auto written = endpoint_toml_test::make_current();

    WHEN("it is rendered as text and parsed again")
    {
      const auto text = arude::config::to_toml(written);
      const auto read = arude::config::from_toml<test_helpers::config_test_t>(text);

      THEN("every member comes back as it went in")
      {
        REQUIRE(read.name == written.name);
        REQUIRE(read.retries == written.retries);
        REQUIRE(read.endpoint == written.endpoint);
        REQUIRE(read.secret == written.secret);
        REQUIRE(read.version == arude::config::version_of<test_helpers::config_test_t>());
      }
    }
  }
}

SCENARIO("bytes are rendered as base64 text", "[config][toml]")
{
  GIVEN("a configuration carrying a payload")
  {
    constexpr auto payload = std::array{std::byte{0x00}, std::byte{0xFF}, std::byte{0x10}};

    WHEN("it is rendered")
    {
      const auto text = arude::config::to_toml(test_helpers::config_test_t{.secret = arude::config::binary{payload}});

      THEN("the payload is text a TOML file can hold, not raw bytes")
      {
        REQUIRE(text.contains("AP8Q"));
      }
    }
  }
}

SCENARIO("the version rendered is the type's own", "[config][toml]")
{
  GIVEN("a configuration whose version member has been tampered with")
  {
    auto written = endpoint_toml_test::make_current();
    written.version = 99;

    WHEN("it is rendered and parsed again")
    {
      const auto read = arude::config::from_toml<test_helpers::config_test_t>(arude::config::to_toml(written));

      THEN("the tampered version did not survive")
      {
        REQUIRE(read.version == arude::config::version_of<test_helpers::config_test_t>());
      }
    }
  }
}

SCENARIO("older text is migrated where the caller asks for it", "[config][toml]")
{
  GIVEN("text written by version 1")
  {
    const auto text = arude::config::to_toml(test_helpers::config_test_v1{.name = "old", .retries = 3});

    THEN("the version is readable without parsing the rest")
    {
      REQUIRE(arude::config::toml_version(text) == arude::config::version_of<test_helpers::config_test_v1>());
    }

    WHEN("it is parsed as the current version, migrating")
    {
      const auto read = arude::config::from_toml_migrated<test_helpers::config_test_t>(text);

      THEN("what version 1 carried came forward")
      {
        REQUIRE(read.name == "old");
        REQUIRE(read.retries == 3);
        REQUIRE(read.version == arude::config::version_of<test_helpers::config_test_t>());
      }
    }

    THEN("parsing it as its own version still works")
    {
      REQUIRE(arude::config::from_toml<test_helpers::config_test_v1>(text).name == "old");
    }
  }
}

SCENARIO("text of the wrong version is refused rather than reinterpreted", "[config][toml]")
{
  GIVEN("text written by the current version")
  {
    const auto text = arude::config::to_toml(endpoint_toml_test::make_current());

    THEN("reading it as the older type is refused, not silently truncated")
    {
      REQUIRE_THROWS_AS(arude::config::from_toml<test_helpers::config_test_v1>(text), arude::exception_base);
    }
  }

  GIVEN("text written by a build newer than this one")
  {
    static constexpr auto text = std::string_view{"version = 99\nname = \"from the future\"\n"};

    THEN("the version still reads, so a caller can say what it found")
    {
      REQUIRE(arude::config::toml_version(text) == 99);
    }

    THEN("migrating it is refused rather than guessed at")
    {
      REQUIRE_THROWS_AS(arude::config::from_toml_migrated<test_helpers::config_test_t>(text), arude::exception_base);
    }
  }
}

SCENARIO("text that is not a configuration says so", "[config][toml]")
{
  GIVEN("text that is not TOML at all")
  {
    static constexpr auto text = std::string_view{"{ this is not toml"};

    THEN("every entry point refuses it")
    {
      REQUIRE_THROWS_AS(arude::config::from_toml<test_helpers::config_test_t>(text), arude::exception_base);
      REQUIRE_THROWS_AS(arude::config::toml_version(text), arude::exception_base);
    }
  }

  GIVEN("TOML that carries no version")
  {
    static constexpr auto text = std::string_view{"name = \"nameless\"\n"};

    THEN("it is refused rather than assumed to be version zero")
    {
      REQUIRE_THROWS_AS(arude::config::toml_version(text), arude::exception_base);
    }
  }
}

SCENARIO("an endpoint and a transport read a file without a manager", "[config][toml]")
{
  GIVEN("a file and the pair a document is built from")
  {
    const auto file = test_helpers::temp_file{"arude_endpoint_toml_direct.toml"};
    const auto carrier = arude::config::transport_file{file.path()};
    const auto format = arude::config::endpoint_toml{};

    WHEN("a configuration is written and read straight back through them")
    {
      const auto written = endpoint_toml_test::make_current();

      auto document = arude::config::endpoint_toml::document_t{};
      format.write_section(document, {}, written);
      format.emit(carrier, document);

      const auto read =
        format.read_section<test_helpers::config_test_t>(format.parse(carrier), {}, arude::config::load_policy::none);

      THEN("the round trip needs no manager, no document object and no cache")
      {
        REQUIRE(read.name == written.name);
        REQUIRE(read.secret == written.secret);
      }
    }
  }
}
