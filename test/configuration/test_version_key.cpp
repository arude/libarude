///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// The version key: written by arude::config from config_version, and taken
/// out of the table before reflect-cpp reads the rest, so that a
/// configuration type declares no member for it.
///

#include "arude/config.hpp"
#include "arude/config/toml.hpp"

#include <catch2/catch_test_macros.hpp>
#include <rfl/Rename.hpp>
#include <toml++/toml.hpp>

#include <cstdint>
#include <format>
#include <map>
#include <optional>
#include <source_location>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace version_key_test
{
namespace
{

///
/// Version 1 of a tile, the versioned member of the board below.
///
struct tile_v1
{
  ///
  /// The version this type is.
  ///
  static constexpr auto config_version = arude::config::version_t{1};

  ///
  /// The tile's colour.
  ///
  std::string colour = "red";
};

///
/// Version 2 of a tile, adding a size.
///
struct tile_v2
{
  ///
  /// The version before this one.
  ///
  using previous_t = tile_v1;

  ///
  /// \see tile_v1::config_version
  ///
  static constexpr auto config_version = arude::config::version_t{2};

  ///
  /// Compares two tiles member by member.
  /// \param other Tile to compare against.
  /// \return true if both are equal.
  ///
  [[nodiscard]] auto operator==(const tile_v2& other) const -> bool = default;

  ///
  /// \see tile_v1::colour
  ///
  std::string colour = "red";

  ///
  /// The tile's edge, in millimetres. New in version 2.
  ///
  std::uint32_t size = 1;
};

///
/// Upgrades a tile from version 1 to version 2.
/// \param val The version 1 tile. Not retained.
/// \return The same tile as version 2, at the default size.
///
[[nodiscard]] auto upgrade(const tile_v1& val) -> tile_v2
{
  return tile_v2{.colour = val.colour};
}

///
/// A nested table that is not versioned, and so carries no version key.
///
struct limits
{
  ///
  /// The lowest value allowed.
  ///
  std::int32_t lower = 0;

  ///
  /// The highest value allowed.
  ///
  std::int32_t upper = 10;
};

///
/// A root holding a versioned member in each place reflect-cpp writes a table:
/// under a key, as an array element, and as a map value whose key holds a dot.
/// Its scalars sort either side of version, so the key's place in the text is
/// tested too.
///
struct board
{
  ///
  /// The version this type is. Not 1, so a root version cannot be mistaken
  /// for a member's.
  ///
  static constexpr auto config_version = arude::config::version_t{3};

  ///
  /// Sorts before version.
  ///
  std::string alpha = "first";

  ///
  /// Sorts after version.
  ///
  std::string zeta = "last";

  ///
  /// A table with no version of its own.
  ///
  limits bounds;

  ///
  /// A versioned member under a key.
  ///
  arude::config::versioned<tile_v2> tile;

  ///
  /// Versioned members as array elements.
  ///
  std::vector<arude::config::versioned<tile_v2>> tiles;

  ///
  /// Versioned members as map values.
  ///
  std::map<std::string, arude::config::versioned<tile_v2>> named;
};

///
/// A configuration whose member is renamed to the key the version is written
/// under, which the concept cannot see and the write refuses.
///
struct renamed_onto_version
{
  ///
  /// The version this type is.
  ///
  static constexpr auto config_version = arude::config::version_t{1};

  ///
  /// The member that takes the version key.
  ///
  rfl::Rename<"version", std::string> label;
};

///
/// A board with every part set away from its default.
/// \return The board.
///
[[nodiscard]] auto make_board() -> board
{
  return board{
    .alpha = "a",
    .zeta = "z",
    .bounds = limits{.lower = -5, .upper = 5},
    .tile = tile_v2{.colour = "blue", .size = 4},
    .tiles = {tile_v2{.colour = "green", .size = 2}, tile_v2{.colour = "white", .size = 3}},
    .named = {{"corner.left", tile_v2{.colour = "black", .size = 9}}}
  };
}

///
/// The text the code before this change, which carried the version as a
/// data member of every type, wrote for make_board(), byte for byte.
/// Produced by that build and checked in, so the format is pinned to what
/// files on disk already hold rather than to what this build happens to
/// write.
///
constexpr auto board_text = std::string_view{"alpha = 'a'\n"
                                             "version = 3\n"
                                             "zeta = 'z'\n"
                                             "\n"
                                             "[bounds]\n"
                                             "lower = -5\n"
                                             "upper = 5\n"
                                             "\n"
                                             "[named.'corner.left']\n"
                                             "colour = 'black'\n"
                                             "size = 9\n"
                                             "version = 2\n"
                                             "\n"
                                             "[tile]\n"
                                             "colour = 'blue'\n"
                                             "size = 4\n"
                                             "version = 2\n"
                                             "\n"
                                             "[[tiles]]\n"
                                             "colour = 'green'\n"
                                             "size = 2\n"
                                             "version = 2\n"
                                             "\n"
                                             "[[tiles]]\n"
                                             "colour = 'white'\n"
                                             "size = 3\n"
                                             "version = 2"};

///
/// Reads text as a configuration under a policy and a format.
/// \tparam T The configuration type.
/// \param document The parsed text. The read hides and restores its version
///                 keys, so it comes back as it went in.
/// \param policy The load policy.
/// \param format The format's options.
/// \return The configuration.
///
template<arude::config::configuration_c T>
[[nodiscard]] auto
read(toml::table& document, const arude::config::load_policy policy, const arude::config::toml_format& format) -> T
{
  return arude::config::detail::read_root<T>(document, policy, format, "<text>", std::source_location::current());
}

///
/// Reads text as a board, migrating, and returns the error code it is refused
/// with.
/// \param text The text. Not retained.
/// \return The code, or std::nullopt where the text is read.
///
[[nodiscard]] auto refusal_of(const std::string_view text) -> std::optional<arude::config::config_error>
{
  try
  {
    std::ignore = arude::config::from_toml_migrated<board>(text);
  }
  catch(const arude::config::exception_t& ex)
  {
    return ex.data();
  }

  return std::nullopt;
}

} // namespace

} // namespace version_key_test

SCENARIO("a configuration type declares its version once", "[config][version_key]")
{
  THEN("the types above are configurations without a version member")
  {
    STATIC_REQUIRE(arude::config::configuration_c<version_key_test::board>);
    STATIC_REQUIRE(arude::config::configuration_c<version_key_test::tile_v2>);
  }
}

SCENARIO("a write puts the version key into every versioned table", "[config][version_key]")
{
  GIVEN("a board")
  {
    const auto value = version_key_test::make_board();

    WHEN("it is written")
    {
      const auto document = arude::config::detail::write_root(value, std::source_location::current());

      THEN("the root carries its own version")
      {
        REQUIRE(document["version"].value<std::int64_t>() == 3);
      }

      THEN("a versioned member under a key, in an array and in a map carries the member's version")
      {
        REQUIRE(document["tile"]["version"].value<std::int64_t>() == 2);
        REQUIRE(document["tiles"][0]["version"].value<std::int64_t>() == 2);
        REQUIRE(document["tiles"][1]["version"].value<std::int64_t>() == 2);
        REQUIRE(document["named"]["corner.left"]["version"].value<std::int64_t>() == 2);
      }

      THEN("a table that is not versioned carries none")
      {
        REQUIRE(!document["bounds"]["version"]);
      }
    }
  }

  GIVEN("a configuration with a member renamed to the version key")
  {
    const auto value = version_key_test::renamed_onto_version{.label = std::string{"clash"}};

    THEN("the write is refused rather than writing either value over the other")
    {
      try
      {
        std::ignore = arude::config::to_toml(value);
        FAIL("a member written as the version key should have been refused");
      }
      catch(const arude::config::exception_t& ex)
      {
        INFO(std::format("The message is: {}", ex.str()));
        REQUIRE(ex.data() == arude::config::config_error::invalid_payload);
        REQUIRE(ex.str().contains("the key its configuration version is written under"));
      }
    }
  }
}

SCENARIO("a read takes the version key out before the rest is read", "[config][version_key]")
{
  GIVEN("a board written as text")
  {
    const auto text = arude::config::to_toml(version_key_test::make_board());

    WHEN("it is read with unknown keys refused")
    {
      auto document = arude::config::detail::parse_toml(text, "<text>", std::source_location::current());
      const auto loaded = version_key_test::read<version_key_test::board>(
        document, arude::config::load_policy::none, arude::config::toml_format{.reject_unknown_fields = true});

      THEN("no version key counts as unknown, at the root or in a member")
      {
        REQUIRE(loaded.alpha == "a");
        REQUIRE(loaded.tile.value().colour == "blue");
        REQUIRE(loaded.tiles.size() == 2);
        REQUIRE(loaded.named.at("corner.left").value().size == 9);
      }

      THEN("the document is handed back with every version key it had")
      {
        REQUIRE(arude::config::detail::render_toml(document) == text);
      }
    }
  }

  GIVEN("text whose root declares no version")
  {
    THEN("it is refused as a payload, not read as a default version")
    {
      REQUIRE(
        version_key_test::refusal_of("alpha = 'a'\nzeta = 'z'\n") == arude::config::config_error::invalid_payload);
    }
  }
}

SCENARIO("a version newer than this build is still refused", "[config][version_key]")
{
  GIVEN("text whose root is newer")
  {
    const auto text = std::string{version_key_test::board_text}.replace(
      std::string_view{version_key_test::board_text}.find("version = 3"), 11, "version = 4");

    THEN("it is an invalid_version")
    {
      REQUIRE(version_key_test::refusal_of(text) == arude::config::config_error::invalid_version);
    }
  }

  GIVEN("text whose member is newer")
  {
    constexpr auto text = std::string_view{"alpha = 'a'\ntiles = []\nversion = 3\nzeta = 'z'\n"
                                           "[bounds]\nlower = 0\nupper = 1\n"
                                           "[named]\n"
                                           "[tile]\ncolour = 'red'\nsize = 1\nversion = 3\n"};

    THEN("it is an invalid_version")
    {
      REQUIRE(version_key_test::refusal_of(text) == arude::config::config_error::invalid_version);
    }
  }
}

SCENARIO("members of an older version still migrate", "[config][version_key]")
{
  GIVEN("text whose members are version 1 wherever they are")
  {
    constexpr auto text = std::string_view{"alpha = 'a'\nversion = 3\nzeta = 'z'\n"
                                           "[bounds]\nlower = 0\nupper = 1\n"
                                           "[named.'corner.left']\ncolour = 'black'\nversion = 1\n"
                                           "[tile]\ncolour = 'blue'\nversion = 1\n"
                                           "[[tiles]]\ncolour = 'green'\nversion = 1\n"};

    WHEN("it is read, migrating")
    {
      const auto loaded = arude::config::from_toml_migrated<version_key_test::board>(text);

      THEN("each member came forward, taking version 2's default for what version 1 lacks")
      {
        REQUIRE(loaded.tile.value().colour == "blue");
        REQUIRE(loaded.tile.value().size == 1);
        REQUIRE(loaded.tiles.at(0).value().colour == "green");
        REQUIRE(loaded.named.at("corner.left").value().colour == "black");
      }

      THEN("writing it again gives every member the current version")
      {
        const auto document = arude::config::detail::write_root(loaded, std::source_location::current());

        REQUIRE(document["tile"]["version"].value<std::int64_t>() == 2);
        REQUIRE(document["tiles"][0]["version"].value<std::int64_t>() == 2);
      }
    }

    THEN("reading it without migrating is an invalid_version")
    {
      try
      {
        std::ignore = arude::config::from_toml<version_key_test::board>(text);
        FAIL("older members read without migrating should have been refused");
      }
      catch(const arude::config::exception_t& ex)
      {
        REQUIRE(ex.data() == arude::config::config_error::invalid_version);
      }
    }
  }
}

SCENARIO("the file format is the one a version member produced", "[config][version_key]")
{
  GIVEN("text written by the build that still carried a version member")
  {
    WHEN("it is read")
    {
      const auto loaded = arude::config::from_toml<version_key_test::board>(version_key_test::board_text);

      THEN("every part of it reads")
      {
        const auto expected = version_key_test::make_board();

        REQUIRE(loaded.alpha == expected.alpha);
        REQUIRE(loaded.zeta == expected.zeta);
        REQUIRE(loaded.bounds.lower == expected.bounds.lower);
        REQUIRE(loaded.bounds.upper == expected.bounds.upper);
        REQUIRE(loaded.tile == expected.tile);
        REQUIRE(loaded.tiles == expected.tiles);
        REQUIRE(loaded.named == expected.named);
      }
    }
  }

  GIVEN("the same value")
  {
    WHEN("it is written by this build")
    {
      const auto text = arude::config::to_toml(version_key_test::make_board());

      THEN("the text is byte for byte what that build wrote, key order included")
      {
        REQUIRE(text == version_key_test::board_text);
      }
    }
  }
}
