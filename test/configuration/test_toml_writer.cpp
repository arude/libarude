///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// The writer's refusals: a value TOML cannot hold is an invalid_payload
/// before any byte is produced, rather than a wrapped or empty value that
/// would not read back.
///

#include "arude/config.hpp"
#include "arude/config/toml.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace toml_writer_test
{
namespace
{

///
/// A colour, which reflect-cpp writes by name.
///
enum class colour // NOLINT(performance-enum-size): reflect-cpp cannot list the names of a narrower enumeration.
{
  ///
  /// Red.
  ///
  red,

  ///
  /// Green.
  ///
  green
};

///
/// A flag enum, whose value reflect-cpp writes as '|'-separated names.
///
enum class flags // NOLINT(performance-enum-size): reflect-cpp cannot list the names of a narrower enumeration.
{
  ///
  /// The first flag.
  ///
  a = 1,

  ///
  /// The second flag.
  ///
  b = 2
};

///
/// Combines two flags, as rfl's is_flag_enum concept requires.
/// \param left One flag.
/// \param right Another.
/// \return The two flags together.
///
constexpr auto operator|(flags left, flags right) -> flags
{
  return static_cast<flags>(std::to_underlying(left) | std::to_underlying(right));
}

///
/// A nested struct, for a table a map's dotted key holds.
///
struct inner
{
  ///
  /// A depth.
  ///
  std::int32_t depth = 7;

  ///
  /// A label.
  ///
  std::string label = "x";
};

///
/// The configuration the writer checks are written from.
///
struct writer_config
{
  ///
  /// The version this type is.
  ///
  static constexpr auto config_version = arude::config::version_t{1};

  ///
  /// The version carried in the text.
  ///
  arude::config::version_t version = config_version;

  ///
  /// An unsigned field, as large as TOML allows.
  ///
  std::uint64_t big = 0;

  ///
  /// Unsigned values in an array.
  ///
  std::vector<std::uint64_t> bigs;

  ///
  /// Optional values in an array, which may hold a null.
  ///
  std::vector<std::optional<std::uint64_t>> optionals;

  ///
  /// A string-keyed map whose values may hold a null.
  ///
  std::map<std::string, std::optional<std::uint64_t>> entries;

  ///
  /// An optional field, left out of the file when disengaged.
  ///
  std::optional<std::uint64_t> maybe;

  ///
  /// A colour field.
  ///
  colour hue = colour::red;

  ///
  /// A flag field.
  ///
  flags mode = flags::a;

  ///
  /// A text field.
  ///
  std::string text = "plain";

  ///
  /// Text values in an array.
  ///
  std::vector<std::string> texts;

  ///
  /// A string-keyed map whose keys may hold dots.
  ///
  std::map<std::string, std::string> dotted;

  ///
  /// A string-keyed map of tables, whose keys may hold dots.
  ///
  std::map<std::string, inner> tables;

  ///
  /// A string-keyed map of arrays, whose keys may hold dots.
  ///
  std::map<std::string, std::vector<std::int32_t>> arrays;
};

///
/// Renders a configuration, or throws what write_root throws.
/// \param value The configuration. Not retained.
/// \return The text.
/// \throws arude::config::exception_t With invalid_payload if the value
///         cannot be written.
///
[[nodiscard]] auto render(const writer_config& value) -> std::string
{
  return arude::config::detail::render_toml(arude::config::detail::write_root(value, std::source_location::current()));
}

///
/// Runs a call that is expected to throw, and returns what it threw.
/// \tparam E The exception type expected.
/// \param call The call. Not retained.
/// \return The exception the call threw.
/// \throws std::logic_error If the call returns instead, which fails the scenario.
///
template<typename E>
[[nodiscard]] auto failure_of(const std::invocable auto& call) -> E
{
  try
  {
    call();
  }
  catch(const E& ex)
  {
    return ex;
  }

  throw std::logic_error{"The call was expected to throw, and returned."};
}

} // namespace

} // namespace toml_writer_test

SCENARIO("an unsigned value beyond INT64_MAX is refused rather than wrapped", "[config][toml_writer]")
{
  GIVEN("a configuration whose field holds the largest unsigned value")
  {
    auto value = toml_writer_test::writer_config{};
    value.big = std::numeric_limits<std::uint64_t>::max();

    WHEN("it is written")
    {
      const auto failure = toml_writer_test::failure_of<arude::config::exception_t>(
        [&value] -> void { std::ignore = toml_writer_test::render(value); });

      THEN("it is an invalid_payload naming the field and the value")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
        REQUIRE(failure.str().contains("the value is not a valid"));
        REQUIRE(failure.str().contains("'big' holds 18446744073709551615, beyond the largest integer TOML holds."));
      }
    }
  }

  GIVEN("a configuration whose array holds the largest unsigned value")
  {
    auto value = toml_writer_test::writer_config{};
    value.bigs.push_back(std::numeric_limits<std::uint64_t>::max());

    WHEN("it is written")
    {
      const auto failure = toml_writer_test::failure_of<arude::config::exception_t>(
        [&value] -> void { std::ignore = toml_writer_test::render(value); });

      THEN("the element is refused, as a field would be")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
        REQUIRE(failure.str().contains(
          "an array element holds 18446744073709551615, beyond the largest integer TOML holds."));
      }
    }
  }
}

SCENARIO("a null in an array or a map entry is refused rather than written as an empty string", "[config][toml_writer]")
{
  GIVEN("a configuration whose array holds a disengaged optional")
  {
    auto value = toml_writer_test::writer_config{};
    value.optionals = {1, std::nullopt, 2};

    WHEN("it is written")
    {
      const auto failure = toml_writer_test::failure_of<arude::config::exception_t>(
        [&value] -> void { std::ignore = toml_writer_test::render(value); });

      THEN("it is an invalid_payload saying TOML has no null")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
        REQUIRE(failure.str().contains("TOML has no null, and an empty value in an array cannot be written."));
      }
    }
  }

  GIVEN("a configuration whose map holds a disengaged optional")
  {
    auto value = toml_writer_test::writer_config{};
    value.entries["key"] = std::nullopt;

    WHEN("it is written")
    {
      const auto failure = toml_writer_test::failure_of<arude::config::exception_t>(
        [&value] -> void { std::ignore = toml_writer_test::render(value); });

      THEN("it is an invalid_payload saying TOML has no null")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
        REQUIRE(failure.str().contains("TOML has no null, and an empty value in a map entry cannot be written."));
      }
    }
  }

  GIVEN("a configuration whose optional field is disengaged")
  {
    auto value = toml_writer_test::writer_config{};
    value.maybe = std::nullopt;

    WHEN("it is written")
    {
      const auto text = toml_writer_test::render(value);

      THEN("the field is left out, as reflect-cpp leaves it")
      {
        REQUIRE(!text.contains("maybe"));
      }
    }
  }
}

SCENARIO("an enum value naming no enumerator is refused rather than written as a number", "[config][toml_writer]")
{
  GIVEN("a configuration whose colour names no enumerator")
  {
    auto value = toml_writer_test::writer_config{};
    value.hue = static_cast<toml_writer_test::colour>(300);

    WHEN("it is written")
    {
      const auto failure = toml_writer_test::failure_of<arude::config::exception_t>(
        [&value] -> void { std::ignore = toml_writer_test::render(value); });

      THEN("it is an invalid_payload naming the field, the value and the type")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
        REQUIRE(failure.str().contains("'hue' holds 300, which names no enumerator of toml_writer_test::"));
      }
    }
  }
}

SCENARIO("a key holding dots survives a write", "[config][toml_writer]")
{
  GIVEN("a configuration whose maps hold keys with dots, as a value, a table and an array")
  {
    auto value = toml_writer_test::writer_config{};
    value.dotted["example.com"] = "value";
    value.tables["example.com"] = toml_writer_test::inner{.depth = 3, .label = "table"};
    value.arrays["a.b"] = {1, 2};

    WHEN("it is written and read back")
    {
      const auto text = toml_writer_test::render(value);

      auto document = arude::config::detail::parse_toml(text, "<text>", std::source_location::current());

      using arude::bitwise_operators::operator|;

      const auto loaded = arude::config::detail::read_root<toml_writer_test::writer_config>(
        document,
        arude::config::load_policy::create | arude::config::load_policy::upgrade_to_current,
        arude::config::toml_format{},
        "<text>",
        std::source_location::current());

      THEN("each key is one key, not a path through nested tables")
      {
        REQUIRE(loaded.dotted.at("example.com") == "value");
        REQUIRE(loaded.tables.at("example.com").depth == 3);
        REQUIRE(loaded.tables.at("example.com").label == "table");
        REQUIRE(loaded.arrays.at("a.b") == std::vector<std::int32_t>{1, 2});
      }
    }
  }
}

SCENARIO("a flag enum with a negative value is refused rather than hung on", "[config][toml_writer]")
{
  GIVEN("a configuration whose flags hold a negative value")
  {
    auto value = toml_writer_test::writer_config{};
    value.mode = static_cast<toml_writer_test::flags>(-2); // ~flags::a, which no name spells.

    WHEN("it is written")
    {
      const auto failure = toml_writer_test::failure_of<arude::config::exception_t>(
        [&value] -> void { std::ignore = toml_writer_test::render(value); });

      THEN("it is an invalid_payload naming the field, the value and the type")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
        REQUIRE(failure.str().contains("'mode' holds -2, which names no enumerator of toml_writer_test::"));
      }
    }
  }
}

SCENARIO("text that is not valid UTF-8 is refused rather than silently changed", "[config][toml_writer]")
{
  GIVEN("a configuration whose text field is not valid UTF-8")
  {
    auto value = toml_writer_test::writer_config{};
    value.text = std::string{"\xff\xfe"};

    WHEN("it is written")
    {
      const auto failure = toml_writer_test::failure_of<arude::config::exception_t>(
        [&value] -> void { std::ignore = toml_writer_test::render(value); });

      THEN("it is an invalid_payload naming the field")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
        REQUIRE(failure.str().contains("'text' holds text that is not valid UTF-8, which TOML cannot hold."));
      }
    }
  }

  GIVEN("a configuration whose array holds text that is not valid UTF-8")
  {
    auto value = toml_writer_test::writer_config{};
    value.texts = {"plain", std::string{"\xff\xfe"}};

    WHEN("it is written")
    {
      const auto failure = toml_writer_test::failure_of<arude::config::exception_t>(
        [&value] -> void { std::ignore = toml_writer_test::render(value); });

      THEN("the element is refused, as a field would be")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
        REQUIRE(failure.str().contains("an array element holds text that is not valid UTF-8, which TOML cannot hold."));
      }
    }
  }

  GIVEN("a configuration whose map key is not valid UTF-8, whatever the value's kind")
  {
    auto scalar = toml_writer_test::writer_config{};
    scalar.dotted[std::string{"\xff"}] = "value";

    auto table = toml_writer_test::writer_config{};
    table.tables[std::string{"\xff"}] = toml_writer_test::inner{};

    auto array = toml_writer_test::writer_config{};
    array.arrays[std::string{"\xff"}] = {1};

    WHEN("each is written")
    {
      THEN("a scalar value's key is refused")
      {
        const auto failure = toml_writer_test::failure_of<arude::config::exception_t>(
          [&scalar] -> void { std::ignore = toml_writer_test::render(scalar); });

        REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
        REQUIRE(failure.str().contains("is not valid UTF-8, which TOML cannot hold."));
      }

      THEN("a table's key is refused")
      {
        const auto failure = toml_writer_test::failure_of<arude::config::exception_t>(
          [&table] -> void { std::ignore = toml_writer_test::render(table); });

        REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
        REQUIRE(failure.str().contains("is not valid UTF-8, which TOML cannot hold."));
      }

      THEN("an array's key is refused")
      {
        const auto failure = toml_writer_test::failure_of<arude::config::exception_t>(
          [&array] -> void { std::ignore = toml_writer_test::render(array); });

        REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
        REQUIRE(failure.str().contains("is not valid UTF-8, which TOML cannot hold."));
      }
    }
  }

  GIVEN("a configuration whose text holds valid multibyte text")
  {
    auto value = toml_writer_test::writer_config{};
    value.text = "\u00e9\u20ac\U0001F600\uffff\U0010FFFF";

    WHEN("it is written and read back")
    {
      const auto rendered = toml_writer_test::render(value);

      auto document = arude::config::detail::parse_toml(rendered, "<text>", std::source_location::current());

      using arude::bitwise_operators::operator|;

      const auto loaded = arude::config::detail::read_root<toml_writer_test::writer_config>(
        document,
        arude::config::load_policy::create | arude::config::load_policy::upgrade_to_current,
        arude::config::toml_format{},
        "<text>",
        std::source_location::current());

      THEN("the text survives unchanged, the boundary code points included")
      {
        REQUIRE(loaded.text == value.text);
      }
    }
  }
}

SCENARIO("of two unwritable fields, the first one met is the one reported", "[config][toml_writer]")
{
  GIVEN("a configuration whose unsigned field and whose colour both cannot be written")
  {
    auto value = toml_writer_test::writer_config{};
    value.big = std::numeric_limits<std::uint64_t>::max();
    value.hue = static_cast<toml_writer_test::colour>(300);

    WHEN("it is written")
    {
      const auto failure = toml_writer_test::failure_of<arude::config::exception_t>(
        [&value] -> void { std::ignore = toml_writer_test::render(value); });

      THEN("the message names the first field, big, and not the second")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
        REQUIRE(failure.str().contains("'big' holds 18446744073709551615, beyond the largest integer TOML holds."));
        REQUIRE(!failure.str().contains("'hue'"));
      }
    }
  }
}
