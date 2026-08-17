///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///

#include "arude/config.hpp"
#include "test/helpers/temp_file.hpp"
#include "test/helpers/write_text.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>

namespace document_test
{

///
/// The root configuration of these tests. One version only: what matters is
/// that the root has its own version, independent of every section's.
///
struct app
{
  ///
  /// The version this type is.
  ///
  static constexpr auto config_version = arude::config::version_t{1};

  ///
  /// The version carried in the file.
  ///
  arude::config::version_t version = config_version;

  ///
  /// Name this configuration is known by.
  ///
  std::string name = "example";
};

///
/// Version 1 of a section: a host, nothing else.
///
struct network_v1
{
  ///
  /// The version this type is.
  ///
  static constexpr auto config_version = arude::config::version_t{1};

  ///
  /// The version carried in the file.
  ///
  arude::config::version_t version = config_version;

  ///
  /// Host the configured service is reached at.
  ///
  std::string host = "localhost";
};

///
/// Version 2 of the same section: a port appears.
///
struct network_v2
{
  ///
  /// The version before this one, which migration walks back through.
  ///
  using previous_t = network_v1;

  ///
  /// The version this type is.
  ///
  static constexpr auto config_version = arude::config::version_t{2};

  ///
  /// The version carried in the file.
  ///
  arude::config::version_t version = config_version;

  ///
  /// \see network_v1::host
  ///
  std::string host = "localhost";

  ///
  /// Port to reach it on. New in version 2, so an upgraded section takes the
  /// default.
  ///
  std::uint32_t port = 8443;
};

///
/// Produces a version 2 section from a version 1 one.
/// \param val Section to upgrade. Not retained.
/// \return The same section as version 2.
///
[[nodiscard]] constexpr auto upgrade(const network_v1& val) -> network_v2;

///
/// Produces a version 1 section from a version 2 one.
/// \param val Section to downgrade. Not retained.
/// \return The same section as version 1, less what version 1 cannot hold.
///
[[nodiscard]] constexpr auto downgrade(const network_v2& val) -> network_v1;

///
/// A second section type, so a document can hold two distinct sections.
///
struct logging
{
  ///
  /// The version this type is.
  ///
  static constexpr auto config_version = arude::config::version_t{1};

  ///
  /// The version carried in the file.
  ///
  arude::config::version_t version = config_version;

  ///
  /// How much to log.
  ///
  std::string level = "info";
};

///
///
constexpr auto upgrade(const network_v1& val) -> network_v2
{
  return network_v2{.host = val.host};
}

///
///
constexpr auto downgrade(const network_v2& val) -> network_v1
{
  return network_v1{.host = val.host};
}

///
/// Returns the whole file as text, for asserting on what a store produced.
/// \param path File to read.
/// \return The text.
///
auto text_of(const std::filesystem::path& path) -> std::string;

///
///
auto text_of(const std::filesystem::path& path) -> std::string
{
  auto stream = std::ifstream{path};
  return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

} // namespace document_test

SCENARIO("several sections in one file are loaded and stored together", "[config][document]")
{
  GIVEN("a document holding a root and two named sections")
  {
    const auto file = test_helpers::temp_file{"arude_document_sections.toml"};
    auto manager = arude::config::config_manager{};
    auto document =
      manager.register_document(arude::config::endpoint_toml{}, arude::config::transport_file{file.path()});
    auto root = document.add_root<document_test::app>();
    auto network = document.add_section<document_test::network_v2>("network");
    auto logging = document.add_section<document_test::logging>("logging");

    WHEN("the sections are set and the document stored")
    {
      auto app_value = document_test::app{};
      app_value.name = "example";

      auto network_value = document_test::network_v2{};
      network_value.host = "example.invalid";
      network_value.port = 9443;

      auto logging_value = document_test::logging{};
      logging_value.level = "debug";

      root.set(app_value);
      network.set(network_value);
      logging.set(logging_value);
      document.store();

      THEN("one write put all three into the one file")
      {
        const auto text = document_test::text_of(file.path());
        REQUIRE(text.contains("name"));
        REQUIRE(text.contains("example.invalid"));
        REQUIRE(text.contains("debug"));
        REQUIRE(text.contains("[network]"));
        REQUIRE(text.contains("[logging]"));
      }

      THEN("one read brings all three back, through a fresh manager over the same file")
      {
        auto reader = arude::config::config_manager{};
        auto read_document =
          reader.register_document(arude::config::endpoint_toml{}, arude::config::transport_file{file.path()});
        auto read_root = read_document.add_root<document_test::app>();
        auto read_network = read_document.add_section<document_test::network_v2>("network");
        auto read_logging = read_document.add_section<document_test::logging>("logging");

        read_document.load();

        REQUIRE(read_root.get().name == "example");
        REQUIRE(read_network.get().host == "example.invalid");
        REQUIRE(read_network.get().port == 9443);
        REQUIRE(read_logging.get().level == "debug");
      }
    }
  }
}

// A table nobody registered is not anyone's section, but it is someone's data:
// the retained tree is what store() starts from, so it survives untouched.
SCENARIO("an unregistered table survives a store", "[config][document]")
{
  GIVEN("a file holding a table nobody registers")
  {
    const auto file = test_helpers::temp_file{"arude_document_experimental.toml"};
    test_helpers::write_text(
      file.path(),
      "version = 1\n"
      "name = \"example\"\n"
      "\n"
      "[experimental]\n"
      "flag = true\n");

    auto manager = arude::config::config_manager{};
    auto document =
      manager.register_document(arude::config::endpoint_toml{}, arude::config::transport_file{file.path()});
    auto root = document.add_root<document_test::app>();

    WHEN("the document is loaded and stored again")
    {
      document.load();

      auto updated = root.get();
      updated.name = "renamed";
      root.set(updated);
      document.store();

      THEN("the table is still there, exactly as parsed")
      {
        const auto text = document_test::text_of(file.path());
        REQUIRE(text.contains("[experimental]"));
        REQUIRE(text.contains("flag"));
        REQUIRE(text.contains("renamed"));
      }
    }
  }
}

// A section added after a load is disengaged: its subtree is left exactly as
// parsed, so it cannot clobber a value already in the file.
SCENARIO("a section added after a load does not clobber the file", "[config][document]")
{
  GIVEN("a file holding a network table, and a document that did not know it at load time")
  {
    const auto file = test_helpers::temp_file{"arude_document_late_section.toml"};
    test_helpers::write_text(
      file.path(),
      "version = 1\n"
      "name = \"example\"\n"
      "\n"
      "[network]\n"
      "version = 2\n"
      "host = \"original\"\n");

    auto manager = arude::config::config_manager{};
    auto document =
      manager.register_document(arude::config::endpoint_toml{}, arude::config::transport_file{file.path()});
    auto root = document.add_root<document_test::app>();

    document.load();

    WHEN("the network section is added afterwards and the document stored")
    {
      auto network = document.add_section<document_test::network_v2>("network");

      THEN("it is disengaged, not filled with defaults")
      {
        REQUIRE(!network.loaded());
      }

      document.store();

      THEN("the value in the file is the original, not the defaults")
      {
        const auto text = document_test::text_of(file.path());
        REQUIRE(text.contains("original"));
        REQUIRE(!text.contains("8443"));
      }

      THEN("the root it was added next to is untouched")
      {
        REQUIRE(root.get().name == "example");
      }
    }
  }
}

SCENARIO("per-section versions migrate independently", "[config][document]")
{
  GIVEN("a file whose root is current and whose network section is one version behind")
  {
    const auto file = test_helpers::temp_file{"arude_document_section_migration.toml"};
    test_helpers::write_text(
      file.path(),
      "version = 1\n"
      "name = \"example\"\n"
      "\n"
      "[network]\n"
      "version = 1\n"
      "host = \"old-host\"\n");

    auto manager = arude::config::config_manager{};
    auto document =
      manager.register_document(arude::config::endpoint_toml{}, arude::config::transport_file{file.path()});
    auto root = document.add_root<document_test::app>();
    auto network = document.add_section<document_test::network_v2>("network");

    WHEN("the document is loaded")
    {
      document.load();

      THEN("the root stays as it was")
      {
        REQUIRE(root.get().version == 1);
        REQUIRE(root.get().name == "example");
      }

      THEN("the network section was brought forward, value and all")
      {
        REQUIRE(network.get().version == 2);
        REQUIRE(network.get().host == "old-host");
        REQUIRE(network.get().port == 8443);
      }

      THEN(
        "the version decision is per section, so strict_version refuses the behind section even though the root is "
        "current")
      {
        auto strict_manager = arude::config::config_manager{};
        auto strict_document =
          strict_manager.register_document(arude::config::endpoint_toml{}, arude::config::transport_file{file.path()});
        std::ignore = strict_document.add_root<document_test::app>();
        std::ignore = strict_document.add_section<document_test::network_v2>("network");

        try
        {
          strict_document.load(arude::config::load_policy::strict_version);
          FAIL("a strict load of a behind section should have thrown");
        }
        catch(const arude::config::exception_t& ex)
        {
          REQUIRE(ex.data() == arude::config::config_error::invalid_version);
        }
      }
    }
  }
}
