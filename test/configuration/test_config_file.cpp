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

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <string_view>
#include <system_error>

namespace config_file_test
{

///
/// A path in the temporary directory that is removed again when it goes away.
/// Catch2 runs the scenarios of one binary in one process, so a fixed name
/// would have two of them writing the same file; the name is taken from the
/// test rather than shared.
///
class temp_file final
{
public: // Structors / Operators
  ///
  /// Names a file in the temporary directory, without creating it.
  /// \param name Name to use, unique within this test binary.
  ///
  explicit temp_file(std::string_view name);

  ///
  /// Removes the file if it was created.
  ///
  ~temp_file();

  temp_file(const temp_file&) = delete;
  temp_file(temp_file&&) = delete;
  auto operator=(const temp_file&) -> temp_file& = delete;
  auto operator=(temp_file&&) -> temp_file& = delete;

public: // Accessors
  ///
  /// Returns the path named.
  /// \return The path.
  ///
  [[nodiscard]] auto path() const -> const std::filesystem::path&;

private: // Variables
  std::filesystem::path path_;
};

///
///
temp_file::temp_file(const std::string_view name)
  : path_{std::filesystem::temp_directory_path() / std::string{name}}
{
}

///
///
temp_file::~temp_file()
{
  // A destructor throws nothing, and a leftover file in the temporary
  // directory is not worth reporting anyway.
  auto error = std::error_code{};
  std::filesystem::remove(path_, error);
}

///
///
auto temp_file::path() const -> const std::filesystem::path&
{
  return path_;
}

///
/// Writes text to a file, for the cases that need a file no writer produces.
///
/// \param path File to write.
/// \param text Text to write.
///
auto write_text(const std::filesystem::path& path, const std::string_view text) -> void
{
  auto stream = std::ofstream{path, std::ios::binary | std::ios::trunc};
  stream.write(text.data(), static_cast<std::streamsize>(text.size()));
}

///
/// A current-version configuration with nothing left at its default.
/// \return The configuration.
///
[[nodiscard]] auto make_current() -> arude::config::config_test_t
{
  constexpr auto secret = std::array{std::byte{'f'}, std::byte{'o'}, std::byte{'o'}};

  return arude::config::config_test_t{
    .name = "example", .retries = 7, .endpoint = "example.invalid", .secret = arude::config::binary{secret}};
}

} // namespace config_file_test

SCENARIO("a configuration survives being written and read back", "[config][file]")
{
  GIVEN("a configuration and a file to put it in")
  {
    const auto file = config_file_test::temp_file{"arude_config_round_trip.toml"};
    const auto written = config_file_test::make_current();

    WHEN("it is stored and loaded again")
    {
      arude::config::store(file.path(), written);
      const auto read = arude::config::load<arude::config::config_test_t>(file.path());

      THEN("every member comes back as it went in")
      {
        REQUIRE(read.name == written.name);
        REQUIRE(read.retries == written.retries);
        REQUIRE(read.endpoint == written.endpoint);
        REQUIRE(read.secret == written.secret);
        REQUIRE(read.version == arude::config::version_of<arude::config::config_test_t>());
      }
    }
  }
}

SCENARIO("bytes are stored as base64 text", "[config][file]")
{
  GIVEN("a configuration carrying a payload")
  {
    const auto written = config_file_test::make_current();

    WHEN("it is rendered as TOML")
    {
      const auto text = arude::config::to_toml(written);

      THEN("the payload appears as the base64 of its bytes, and not as its bytes")
      {
        REQUIRE(text.contains("Zm9v"));
      }

      THEN("the version is in the file, so a reader can tell what it is holding")
      {
        REQUIRE(text.contains("version"));
      }
    }
  }
}

SCENARIO("the version written is the type's own", "[config][file]")
{
  GIVEN("a configuration whose version member has been tampered with")
  {
    auto written = config_file_test::make_current();
    written.version = 99;

    WHEN("it is stored and loaded again")
    {
      const auto file = config_file_test::temp_file{"arude_config_stamped.toml"};
      arude::config::store(file.path(), written);

      THEN("the file claims the version of the type that wrote it")
      {
        const auto read = arude::config::load<arude::config::config_test_t>(file.path());

        REQUIRE(read.version == arude::config::version_of<arude::config::config_test_t>());
      }
    }
  }
}

SCENARIO("an older file is migrated on load", "[config][file]")
{
  GIVEN("a file written by version 1")
  {
    const auto file = config_file_test::temp_file{"arude_config_migrated.toml"};
    const auto written = arude::config::config_test_v1{.name = "old", .retries = 2};

    arude::config::store(file.path(), written);

    WHEN("it is loaded as the current version")
    {
      const auto read = arude::config::load_migrated<arude::config::config_test_t>(file.path());

      THEN("it arrives upgraded, with what version 1 held")
      {
        REQUIRE(read.version == 2);
        REQUIRE(read.name == "old");
        REQUIRE(read.retries == 2);
      }

      THEN("what version 1 could not express takes its default")
      {
        REQUIRE(read.endpoint == "localhost");
      }
    }

    WHEN("it is loaded as version 1 directly")
    {
      const auto read = arude::config::load<arude::config::config_test_v1>(file.path());

      THEN("that works too, because the file is version 1")
      {
        REQUIRE(read.name == "old");
      }
    }
  }
}

SCENARIO("a file of the wrong version is refused rather than reinterpreted", "[config][file]")
{
  GIVEN("a file written by the current version")
  {
    const auto file = config_file_test::temp_file{"arude_config_wrong_version.toml"};
    arude::config::store(file.path(), config_file_test::make_current());

    WHEN("it is loaded as version 1")
    {
      THEN("it throws, rather than handing back a configuration that lost a member")
      {
        REQUIRE_THROWS_AS(arude::config::load<arude::config::config_test_v1>(file.path()), arude::exception_base);
      }
    }
  }

  GIVEN("a file written by a build newer than this one")
  {
    const auto file = config_file_test::temp_file{"arude_config_future.toml"};

    config_file_test::write_text(file.path(), "version = 99\nname = \"future\"\n");

    WHEN("it is loaded as the current version")
    {
      THEN("it throws, because nothing here knows what version 99 means")
      {
        REQUIRE_THROWS_AS(
          arude::config::load_migrated<arude::config::config_test_t>(file.path()), arude::exception_base);
      }
    }
  }
}

SCENARIO("a file that cannot be read says so", "[config][file]")
{
  GIVEN("a path with no file at it")
  {
    const auto path = std::filesystem::temp_directory_path() / "arude_config_absent.toml";

    std::filesystem::remove(path);

    THEN("loading throws rather than returning a default configuration")
    {
      REQUIRE_THROWS_AS(arude::config::load<arude::config::config_test_t>(path), arude::exception_base);
      REQUIRE_THROWS_AS(arude::config::load_migrated<arude::config::config_test_t>(path), arude::exception_base);
    }
  }

  GIVEN("a file holding something that is not TOML")
  {
    const auto file = config_file_test::temp_file{"arude_config_invalid.toml"};

    config_file_test::write_text(file.path(), "this is not = = TOML\n");

    THEN("loading throws")
    {
      REQUIRE_THROWS_AS(arude::config::load<arude::config::config_test_t>(file.path()), arude::exception_base);
    }
  }
}

SCENARIO("TOML text can be parsed without a file", "[config][file]")
{
  GIVEN("a configuration rendered as text")
  {
    const auto written = config_file_test::make_current();
    const auto text = arude::config::to_toml(written);

    WHEN("the text is parsed back")
    {
      const auto read = arude::config::from_toml<arude::config::config_test_t>(text);

      THEN("it holds what was rendered")
      {
        REQUIRE(read.name == written.name);
        REQUIRE(read.secret == written.secret);
      }
    }
  }
}
