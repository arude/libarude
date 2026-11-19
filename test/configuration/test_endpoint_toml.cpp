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
#include "test/helpers/config_test_v1.hpp"
#include "test/helpers/config_test_v2.hpp"
#include "test/helpers/temp_directory.hpp"
#include "test/helpers/temp_file.hpp"
#include "test/helpers/write_text.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>

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

SCENARIO("a failure message carries one prefix and quotes what was written as written", "[config][toml]")
{
  GIVEN("a store below a directory whose name spells the library's prefix, where a file blocks the way")
  {
    const auto root = test_helpers::temp_directory{"arude_endpoint_toml_prefix_path"};
    const auto spelled = root.path() / "arude::config: d";
    std::filesystem::create_directories(spelled);
    test_helpers::write_text(spelled / "blocker", "x");
    const auto blocked = spelled / "blocker" / "sub";

    auto manager = arude::config::config_manager{};
    auto document =
      manager.register_document(arude::config::endpoint_toml{}, arude::config::transport_file{blocked / "app.toml"});
    auto section = document.add_root<test_helpers::config_test_t>();
    section.set(test_helpers::config_test_t{});

    THEN("the nested filesystem error still quotes the path as it was written")
    {
      try
      {
        document.store();
        FAIL("a store below a regular file should have been refused");
      }
      catch(const arude::config::exception_t& ex)
      {
        const auto message = std::string{ex.str()};
        INFO(std::format("The message is: {}", message));
        REQUIRE(ex.data() == arude::config::config_error::io_error);
        REQUIRE(message.starts_with("arude::config: "));
        REQUIRE(!message.substr(1).starts_with("arude::config: "));
        REQUIRE(message.contains(std::format("[{}]", blocked.string())));
      }
    }
  }

  GIVEN("a configuration whose binary field is not base64, quoted by reflect-cpp")
  {
    static constexpr auto text = std::string_view{"version = 2\nname = \"bad\"\nsecret = \"@@@@\"\n"};

    THEN("the prefix appears once, though the message passed through reflect-cpp and back")
    {
      try
      {
        std::ignore = arude::config::from_toml<test_helpers::config_test_t>(text);
        FAIL("a malformed payload should have been refused");
      }
      catch(const arude::config::exception_t& ex)
      {
        const auto message = std::string{ex.str()};
        INFO(std::format("The message is: {}", message));
        REQUIRE(ex.data() == arude::config::config_error::invalid_payload);

        auto count = std::size_t{0};

        for(auto at = message.find("arude::config: "); at != std::string::npos;
            at = message.find("arude::config: ", at + 1))
        {
          ++count;
        }

        REQUIRE(count == 1);
        REQUIRE(message.contains("base64"));
      }
    }
  }
}

// toml_version() is compiled into the library, so the exception it throws is
// built with the library's exception base. Arriving as the type this file was
// compiled to expect, in a build with either base, shows the two agree.
SCENARIO("a failure inside the library is the exception the caller compiled", "[config][toml]")
{
  GIVEN("text that is not TOML")
  {
    static constexpr auto text = std::string_view{"{ this is not toml"};

    WHEN("the library fails to read it")
    {
      auto caught_as_std = false;
      auto caught_as_arude = false;

      try
      {
        std::ignore = arude::config::toml_version(text);
      }
      catch(const std::exception&)
      {
        caught_as_std = true;
      }
      catch(...)
      {
        caught_as_std = false;
      }

      try
      {
        std::ignore = arude::config::toml_version(text);
      }
      catch(const arude::config::exception_t& ex)
      {
        caught_as_arude = ex.data() == arude::config::config_error::invalid_payload;
      }

      THEN("it is the configuration's own exception")
      {
        REQUIRE(caught_as_arude);
      }

      THEN("a std::exception handler sees it exactly when the exception base is std::runtime_error")
      {
        REQUIRE(caught_as_std == std::is_base_of_v<std::exception, arude::exception_base>);
      }
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
      arude::config::write_section(format, document, {}, written);
      format.emit(carrier, document);

      const auto parsed = format.parse(carrier);

      THEN("the round trip needs no manager, no document object and no cache")
      {
        REQUIRE(parsed.has_value());

        // The REQUIRE above checks it; the analyser cannot see through the macro.
        // NOLINTBEGIN(bugprone-unchecked-optional-access)
        const auto read = arude::config::read_section<test_helpers::config_test_t>(
          format, parsed.value(), {}, arude::config::load_policy::none);
        // NOLINTEND(bugprone-unchecked-optional-access)

        REQUIRE(read.name == written.name);
        REQUIRE(read.secret == written.secret);
      }
    }
  }

  GIVEN("a path with no file at it, and the pair over it")
  {
    const auto file = test_helpers::temp_file{"arude_endpoint_toml_absent.toml"};
    const auto carrier = arude::config::transport_file{file.path()};
    const auto format = arude::config::endpoint_toml{};

    THEN("parsing it says there is nothing there, rather than failing")
    {
      REQUIRE(!format.parse(carrier).has_value());
    }
  }
}
