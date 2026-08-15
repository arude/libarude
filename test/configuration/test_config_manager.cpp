///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///

#include "arude/config.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>

namespace config_manager_test
{

///
/// A directory in the temporary directory, removed again when it goes away.
/// A directory rather than a file, because the manager creates the directories
/// leading to a source and that has to be tested somewhere it can do no harm.
///
class temp_directory final
{
public: // Structors / Operators
  ///
  /// Creates the directory.
  /// \param name Name to use, unique within this test binary.
  ///
  explicit temp_directory(std::string_view name);

  ///
  /// Removes the directory and everything in it.
  ///
  ~temp_directory();

  temp_directory(const temp_directory&) = delete;
  temp_directory(temp_directory&&) = delete;
  auto operator=(const temp_directory&) -> temp_directory& = delete;
  auto operator=(temp_directory&&) -> temp_directory& = delete;

public: // Accessors
  ///
  /// Returns the path of a file in the directory.
  /// \param name File name.
  /// \return The path, whether or not anything is there.
  ///
  [[nodiscard]] auto file(std::string_view name) const -> std::filesystem::path;

private: // Variables
  std::filesystem::path path_;
};

///
///
temp_directory::temp_directory(const std::string_view name)
  : path_{std::filesystem::temp_directory_path() / std::string{name}}
{
  std::filesystem::remove_all(path_);
  std::filesystem::create_directories(path_);
}

///
///
temp_directory::~temp_directory()
{
  // A destructor throws nothing, and a leftover directory under the temporary
  // directory is not worth reporting.
  auto error = std::error_code{};
  std::filesystem::remove_all(path_, error);
}

///
///
auto temp_directory::file(const std::string_view name) const -> std::filesystem::path
{
  return path_ / std::string{name};
}

///
/// Writes text to a file, for the cases that need one no writer produces.
///
/// \param path File to write.
/// \param text Text to write.
///
auto write_text(const std::filesystem::path& path, const std::string_view text) -> void
{
  auto stream = std::ofstream{path, std::ios::binary | std::ios::trunc};
  stream.write(text.data(), static_cast<std::streamsize>(text.size()));
}

} // namespace config_manager_test

SCENARIO("create_uri names a configuration file", "[config][manager]")
{
  GIVEN("a path")
  {
    const auto uri = arude::config::create_uri("config.toml");

    THEN("the scheme names the endpoint and the transport")
    {
      REQUIRE(std::string{uri.scheme()} == "toml+file");
      REQUIRE(std::string{uri.buffer()}.starts_with("toml+file:"));
    }

    THEN("the path is absolute, so the same file always produces the same URI")
    {
      REQUIRE(std::filesystem::path{std::string{uri.path()}}.is_absolute());
      REQUIRE(std::string{uri.buffer()} == std::string{arude::config::create_uri("./config.toml").buffer()});
    }
  }

  GIVEN("the scheme an endpoint contributes")
  {
    THEN("it is the endpoint's name and the transport, at compile time")
    {
      REQUIRE(arude::config::uri_scheme<arude::config::endpoint_toml>() == "toml+file");
      STATIC_REQUIRE(arude::config::endpoint_c<arude::config::endpoint_toml>);
      STATIC_REQUIRE(arude::config::endpoint_toml::name == "toml");
    }
  }
}

SCENARIO("the manager reads and writes a configuration", "[config][manager]")
{
  GIVEN("a manager and a URI in a directory of its own")
  {
    const auto directory = config_manager_test::temp_directory{"arude_manager_round_trip"};
    const auto uri = arude::config::create_uri(directory.file("app.toml"));
    auto manager = arude::config::config_manager{};

    WHEN("a configuration is stored")
    {
      auto written = arude::config::config_test_t{};
      written.name = "stored";
      written.retries = 9;

      manager.store(uri, written);

      THEN("the file is there and holds it")
      {
        REQUIRE(std::filesystem::exists(directory.file("app.toml")));
        REQUIRE(manager.load<arude::config::config_test_t>(uri).name == "stored");
      }

      THEN("it is in the cache as well, without the file being read again")
      {
        REQUIRE(manager.contains(uri));
        REQUIRE(manager.size() == 1);
        REQUIRE(manager.get<arude::config::config_test_t>(uri).retries == 9);
      }
    }
  }
}

SCENARIO("the manager creates a configuration that is not there yet", "[config][manager]")
{
  GIVEN("a URI under a directory that does not exist")
  {
    const auto directory = config_manager_test::temp_directory{"arude_manager_create"};
    const auto path = directory.file("nested") / "app.toml";
    const auto uri = arude::config::create_uri(path);
    auto manager = arude::config::config_manager{};

    WHEN("it is loaded without the create policy")
    {
      THEN("it is not found, and nothing is written")
      {
        REQUIRE_THROWS_AS(
          manager.load<arude::config::config_test_t>(uri, arude::config::config_manager::load_policy::none),
          arude::config::config_manager::exception_t);
        REQUIRE(!std::filesystem::exists(path));
      }
    }

    WHEN("it is loaded with the create policy")
    {
      constexpr auto policy = arude::config::config_manager::load_policy::create |
                              arude::config::config_manager::load_policy::upgrade_to_current;

      const auto created = manager.load<arude::config::config_test_t>(uri, policy);

      THEN("the defaults come back")
      {
        REQUIRE(created.name == arude::config::config_test_t{}.name);
      }

      THEN("the file is written, directories and all, so it can be edited")
      {
        REQUIRE(std::filesystem::exists(path));
        REQUIRE(manager.load<arude::config::config_test_t>(uri).name == created.name);
      }
    }
  }
}

SCENARIO("the manager migrates an older file according to the policy", "[config][manager]")
{
  GIVEN("a file written by version 1")
  {
    const auto directory = config_manager_test::temp_directory{"arude_manager_migrate"};
    const auto uri = arude::config::create_uri(directory.file("app.toml"));
    auto manager = arude::config::config_manager{};

    manager.store(uri, arude::config::config_test_v1{.name = "old", .retries = 2});

    WHEN("it is loaded as the current version, upgrading")
    {
      const auto loaded = manager.load<arude::config::config_test_t>(uri);

      THEN("it arrives upgraded")
      {
        REQUIRE(loaded.version == 2);
        REQUIRE(loaded.name == "old");
        REQUIRE(loaded.endpoint == "localhost");
      }
    }

    WHEN("it is loaded with strict_version")
    {
      constexpr auto policy = arude::config::config_manager::load_policy::strict_version;

      THEN("it is refused as the wrong version")
      {
        REQUIRE_THROWS_AS(
          manager.load<arude::config::config_test_t>(uri, policy), arude::config::config_manager::exception_t);
      }
    }

    WHEN("it is loaded as the version it actually is")
    {
      constexpr auto policy = arude::config::config_manager::load_policy::strict_version;

      const auto loaded = manager.load<arude::config::config_test_v1>(uri, policy);

      THEN("that works, because no migration is needed")
      {
        REQUIRE(loaded.name == "old");
      }
    }
  }
}

SCENARIO("the manager reports what went wrong", "[config][manager]")
{
  GIVEN("a manager")
  {
    const auto directory = config_manager_test::temp_directory{"arude_manager_errors"};
    auto manager = arude::config::config_manager{};

    WHEN("the URI names an endpoint the call does not")
    {
      const auto uri = arude::config::config_manager::parse_uri("other+file:///app.toml");

      THEN("it is an invalid_uri")
      {
        try
        {
          std::ignore = manager.load<arude::config::config_test_t>(uri);
          FAIL("loading a foreign scheme should have thrown");
        }
        catch(const arude::config::config_manager::exception_t& ex)
        {
          REQUIRE(ex.data() == arude::config::config_manager::errors::invalid_uri);
        }
      }
    }

    WHEN("the text is not a URI at all")
    {
      THEN("parsing it is an invalid_uri")
      {
        REQUIRE_THROWS_AS(
          arude::config::config_manager::parse_uri("not a uri"), arude::config::config_manager::exception_t);
      }
    }

    WHEN("the file is not there")
    {
      const auto uri = arude::config::create_uri(directory.file("absent.toml"));

      THEN("it is a not_found")
      {
        try
        {
          std::ignore = manager.load<arude::config::config_test_t>(uri);
          FAIL("loading an absent file should have thrown");
        }
        catch(const arude::config::config_manager::exception_t& ex)
        {
          REQUIRE(ex.data() == arude::config::config_manager::errors::not_found);
        }
      }
    }

    WHEN("the file holds a version nothing here knows")
    {
      const auto path = directory.file("future.toml");
      const auto uri = arude::config::create_uri(path);

      config_manager_test::write_text(path, "version = 99\nname = \"future\"\n");

      THEN("it is an invalid_version")
      {
        try
        {
          std::ignore = manager.load<arude::config::config_test_t>(uri);
          FAIL("loading a future version should have thrown");
        }
        catch(const arude::config::config_manager::exception_t& ex)
        {
          REQUIRE(ex.data() == arude::config::config_manager::errors::invalid_version);
        }
      }
    }

    WHEN("the file is not a configuration")
    {
      const auto path = directory.file("nonsense.toml");
      const auto uri = arude::config::create_uri(path);

      config_manager_test::write_text(path, "this is not = = TOML\n");

      THEN("it is an invalid_payload")
      {
        try
        {
          std::ignore = manager.load<arude::config::config_test_t>(uri);
          FAIL("loading nonsense should have thrown");
        }
        catch(const arude::config::config_manager::exception_t& ex)
        {
          REQUIRE(ex.data() == arude::config::config_manager::errors::invalid_payload);
        }
      }
    }
  }
}

SCENARIO("the cache is separate from the sources", "[config][manager]")
{
  GIVEN("a stored configuration")
  {
    const auto directory = config_manager_test::temp_directory{"arude_manager_cache"};
    const auto uri = arude::config::create_uri(directory.file("app.toml"));
    auto manager = arude::config::config_manager{};

    manager.store(uri, arude::config::config_test_t{});

    WHEN("the cached value is changed but not stored")
    {
      auto changed = manager.get<arude::config::config_test_t>(uri);
      changed.name = "changed";
      manager.set(uri, changed);

      THEN("the cache has it and the file does not")
      {
        REQUIRE(manager.get<arude::config::config_test_t>(uri).name == "changed");
        REQUIRE(arude::config::load<arude::config::config_test_t>(directory.file("app.toml")).name != "changed");
      }

      THEN("storing from the cache commits it")
      {
        manager.store<arude::config::config_test_t>(uri);

        REQUIRE(arude::config::load<arude::config::config_test_t>(directory.file("app.toml")).name == "changed");
      }
    }

    WHEN("the entry is evicted")
    {
      const auto evicted = manager.evict(uri);

      THEN("it is gone from the cache, and the file is untouched")
      {
        REQUIRE(evicted);
        REQUIRE(!manager.contains(uri));
        REQUIRE(std::filesystem::exists(directory.file("app.toml")));
      }

      THEN("reading it from the cache is a not_found")
      {
        try
        {
          std::ignore = manager.get<arude::config::config_test_t>(uri);
          FAIL("getting an evicted entry should have thrown");
        }
        catch(const arude::config::config_manager::exception_t& ex)
        {
          REQUIRE(ex.data() == arude::config::config_manager::errors::not_found);
        }
      }
    }

    WHEN("the cache holds another type under the URI")
    {
      manager.set(uri, arude::config::config_test_v1{});

      THEN("asking for the wrong one is an invalid_payload rather than a bad cast")
      {
        try
        {
          std::ignore = manager.get<arude::config::config_test_v2>(uri);
          FAIL("getting the wrong type should have thrown");
        }
        catch(const arude::config::config_manager::exception_t& ex)
        {
          REQUIRE(ex.data() == arude::config::config_manager::errors::invalid_payload);
        }
      }
    }

    WHEN("everything is evicted")
    {
      manager.evict_all();

      THEN("the cache is empty")
      {
        REQUIRE(manager.size() == 0);
      }
    }
  }
}

SCENARIO("load policies combine as flags", "[config][manager]")
{
  GIVEN("two flags")
  {
    constexpr auto policy =
      arude::config::config_manager::load_policy::create | arude::config::config_manager::load_policy::strict_version;

    THEN("both are set and the third is not")
    {
      STATIC_REQUIRE(arude::config::is_set(policy, arude::config::config_manager::load_policy::create));
      STATIC_REQUIRE(arude::config::is_set(policy, arude::config::config_manager::load_policy::strict_version));
      STATIC_REQUIRE(!arude::config::is_set(policy, arude::config::config_manager::load_policy::upgrade_to_current));
    }
  }

  GIVEN("an error value")
  {
    THEN("it has a name, so a report says which one it was")
    {
      STATIC_REQUIRE(arude::enum_name(arude::config::config_manager::errors::io_error) == "io_error");
      REQUIRE(std::format("{}", arude::config::config_manager::errors::io_error) == "io_error");
    }
  }
}
