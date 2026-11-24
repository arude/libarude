///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// The reader's range checks: an integer that does not fit the type it is
/// read into, and the two places that bypass the reader's to_basic_type(),
/// map keys and enums.
///

#include "arude/config.hpp"
#include "arude/config/toml.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <map>
#include <optional>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace toml_reader_test
{
namespace
{

///
/// A colour, which reflect-cpp reads by name.
///
enum class colour // NOLINT(performance-enum-size): kept on int, the default, beside the narrower small_colour.
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
enum class flags // NOLINT(performance-enum-size): kept signed, so a negative flag value stays negative.
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
/// Combines two flags, which is what makes reflect-cpp treat the enum as flags.
///
/// \param left One flag.
/// \param right Another.
/// \return The two flags together.
///
constexpr auto operator|(flags left, flags right) -> flags
{
  return static_cast<flags>(std::to_underlying(left) | std::to_underlying(right));
}

///
/// A colour on a narrower underlying type, which reflect-cpp reflects with no
/// rfl::config::enum_range specialised for it.
///
enum class small_colour : std::uint8_t
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
/// The configuration the reader checks are read into.
///
struct checks
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
  /// An unsigned byte field.
  ///
  std::uint8_t level = 0;

  ///
  /// Unsigned bytes in an array.
  ///
  std::vector<std::uint8_t> bytes;

  ///
  /// An optional signed byte.
  ///
  std::optional<std::int8_t> maybe;

  ///
  /// A string-keyed map whose values are unsigned bytes.
  ///
  std::map<std::string, std::uint8_t> counts;

  ///
  /// An unsigned-byte-keyed map.
  ///
  std::map<std::uint8_t, std::string> levels;

  ///
  /// A signed-byte-keyed map.
  ///
  std::map<std::int8_t, std::string> signed_levels;

  ///
  /// A colour field.
  ///
  colour hue = colour::red;

  ///
  /// A flag field.
  ///
  flags mode = flags::a;

  ///
  /// A colour on a narrower underlying type.
  ///
  small_colour small_hue = small_colour::red;

  ///
  /// An opaque payload, read from base64 text.
  ///
  arude::config::binary secret;
};

///
/// Reads text as a configuration, over the fields of a default one: the
/// checks pin how one field fails, and every field is required, so each key
/// the text holds replaces that of `checks{}` as written, and the rest stay.
/// \param text The text. Not retained.
/// \return The configuration.
/// \throws arude::config::exception_t As detail::read_root does.
///
[[nodiscard]] auto read(std::string_view text) -> checks
{
  auto document =
    arude::config::detail::parse_toml(arude::config::to_toml(checks{}), "<defaults>", std::source_location::current());

  for(auto&& [key, value] : arude::config::detail::parse_toml(text, "<text>", std::source_location::current()))
  {
    value.visit(
      [&document, &key](auto&& node) -> void { document.insert_or_assign(key, std::forward<decltype(node)>(node)); });
  }

  return arude::config::detail::read_root<checks>(
    document,
    arude::config::load_policy::upgrade_to_current,
    arude::config::toml_format{},
    "<text>",
    std::source_location::current());
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

} // namespace toml_reader_test

SCENARIO("an integer that does not fit the field's type is refused", "[config][toml_reader]")
{
  GIVEN("an unsigned byte field holding 300")
  {
    constexpr auto text = std::string_view{"version = 1\nlevel = 300\n"};

    WHEN("it is read")
    {
      const auto failure = toml_reader_test::failure_of<arude::config::exception_t>(
        [&text] -> void { std::ignore = toml_reader_test::read(text); });

      THEN("it is an invalid_payload naming the field, the value and the type's range")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
        REQUIRE(failure.str().contains("'level'"));
        REQUIRE(failure.str().contains("the integer 300 is out of range, as unsigned char holds 0 to 255."));
      }
    }
  }

  GIVEN("an unsigned byte field holding -1")
  {
    constexpr auto text = std::string_view{"version = 1\nlevel = -1\n"};

    WHEN("it is read")
    {
      const auto failure = toml_reader_test::failure_of<arude::config::exception_t>(
        [&text] -> void { std::ignore = toml_reader_test::read(text); });

      THEN("it is an invalid_payload saying the value is negative, not one that wraps")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
        REQUIRE(failure.str().contains("the integer -1 is negative, where an unsigned one is expected."));
      }
    }
  }

  GIVEN("an array of unsigned bytes whose second element holds 256")
  {
    constexpr auto text = std::string_view{"version = 1\nbytes = [1, 256]\n"};

    WHEN("it is read")
    {
      const auto failure = toml_reader_test::failure_of<arude::config::exception_t>(
        [&text] -> void { std::ignore = toml_reader_test::read(text); });

      THEN("the element is refused, as a field would be")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
        REQUIRE(failure.str().contains("'bytes'"));
        REQUIRE(failure.str().contains("the integer 256 is out of range, as unsigned char holds 0 to 255."));
      }
    }
  }

  GIVEN("an optional signed byte holding 200")
  {
    constexpr auto text = std::string_view{"version = 1\nmaybe = 200\n"};

    WHEN("it is read")
    {
      const auto failure = toml_reader_test::failure_of<arude::config::exception_t>(
        [&text] -> void { std::ignore = toml_reader_test::read(text); });

      THEN("it is refused rather than narrowed")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
        REQUIRE(failure.str().contains("'maybe'"));
        REQUIRE(failure.str().contains("out of range"));
      }
    }
  }

  GIVEN("a map value holding -3 under a string key")
  {
    constexpr auto text = std::string_view{"version = 1\ncounts = {a = -3}\n"};

    WHEN("it is read")
    {
      const auto failure = toml_reader_test::failure_of<arude::config::exception_t>(
        [&text] -> void { std::ignore = toml_reader_test::read(text); });

      THEN("the value is refused, as a field would be")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
        REQUIRE(failure.str().contains("'counts'"));
        REQUIRE(failure.str().contains("the integer -3 is negative, where an unsigned one is expected."));
      }
    }
  }
}

SCENARIO("the version key is range checked like any other integer", "[config][toml_reader]")
{
  GIVEN("text whose version is beyond version_t's range")
  {
    constexpr auto text = std::string_view{"version = 4294967296\n"};

    WHEN("it is read")
    {
      const auto failure = toml_reader_test::failure_of<arude::config::exception_t>(
        [&text] -> void { std::ignore = toml_reader_test::read(text); });

      THEN("it is an invalid_payload, never a newer version")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
        REQUIRE(
          failure.str().contains("the integer 4294967296 is out of range, as unsigned int holds 0 to 4294967295."));
      }
    }
  }

  GIVEN("text whose version is negative")
  {
    constexpr auto text = std::string_view{"version = -1\n"};

    WHEN("it is read")
    {
      const auto failure = toml_reader_test::failure_of<arude::config::exception_t>(
        [&text] -> void { std::ignore = toml_reader_test::read(text); });

      THEN("it is an invalid_payload, never an older version")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
        REQUIRE(failure.str().contains("the integer -1 is negative, where an unsigned one is expected."));
      }
    }
  }

  GIVEN("text whose version is not an integer")
  {
    constexpr auto text = std::string_view{"version = \"two\"\n"};

    WHEN("it is read")
    {
      const auto failure = toml_reader_test::failure_of<arude::config::exception_t>(
        [&text] -> void { std::ignore = toml_reader_test::read(text); });

      THEN("it is an invalid_payload saying so")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
        REQUIRE(failure.str().contains("the value is not an integer."));
      }
    }
  }
}

SCENARIO("an integral map key is parsed whole and range checked", "[config][toml_reader]")
{
  GIVEN("an unsigned-byte-keyed map whose key holds 300")
  {
    constexpr auto text = std::string_view{"version = 1\nlevels = {300 = \"x\"}\n"};

    WHEN("it is read")
    {
      const auto failure = toml_reader_test::failure_of<arude::config::exception_t>(
        [&text] -> void { std::ignore = toml_reader_test::read(text); });

      THEN("the key is refused rather than cut down by a cast")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
        REQUIRE(failure.str().contains("the key '300' is out of range, as unsigned char holds 0 to 255."));
      }
    }
  }

  GIVEN("an unsigned-byte-keyed map whose key holds -1")
  {
    constexpr auto text = std::string_view{"version = 1\nlevels = {-1 = \"x\"}\n"};

    WHEN("it is read")
    {
      const auto failure = toml_reader_test::failure_of<arude::config::exception_t>(
        [&text] -> void { std::ignore = toml_reader_test::read(text); });

      THEN("the key is refused rather than wrapped, as a negative value would be")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
        REQUIRE(failure.str().contains("the key '-1' is negative, where an unsigned one is expected."));
      }
    }
  }

  GIVEN("a map whose key carries trailing characters")
  {
    constexpr auto text = std::string_view{"version = 1\nlevels = {12abc = \"x\"}\n"};

    WHEN("it is read")
    {
      const auto failure = toml_reader_test::failure_of<arude::config::exception_t>(
        [&text] -> void { std::ignore = toml_reader_test::read(text); });

      THEN("the whole key must be the integer, so it is refused")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
        REQUIRE(failure.str().contains("the key '12abc' is not an integer."));
      }
    }
  }

  GIVEN("a map whose key fits")
  {
    constexpr auto text = std::string_view{"version = 1\nlevels = {12 = \"x\"}\nsigned_levels = {-3 = \"y\"}\n"};

    WHEN("it is read")
    {
      const auto loaded = toml_reader_test::read(text);

      THEN("the key comes across as itself, on both sides of zero")
      {
        REQUIRE(loaded.levels.at(12) == "x");
        REQUIRE(loaded.signed_levels.at(-3) == "y");
      }
    }
  }
}

SCENARIO("an enum reads only enumerator names", "[config][toml_reader]")
{
  GIVEN("a colour spelt as its numeric value")
  {
    constexpr auto text = std::string_view{"version = 1\nhue = \"300\"\n"};

    WHEN("it is read")
    {
      const auto failure = toml_reader_test::failure_of<arude::config::exception_t>(
        [&text] -> void { std::ignore = toml_reader_test::read(text); });

      THEN("it is refused, naming the value and the type")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
        REQUIRE(failure.str().contains("'300' names no enumerator of toml_reader_test::"));
      }
    }
  }

  GIVEN("a colour spelt as a name with trailing characters")
  {
    constexpr auto text = std::string_view{"version = 1\nhue = \"1junk\"\n"};

    WHEN("it is read")
    {
      const auto failure = toml_reader_test::failure_of<arude::config::exception_t>(
        [&text] -> void { std::ignore = toml_reader_test::read(text); });

      THEN("it is refused rather than cut down by std::stoi")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
        REQUIRE(failure.str().contains("'1junk' names no enumerator of toml_reader_test::"));
      }
    }
  }

  GIVEN("a colour spelt as a TOML integer")
  {
    constexpr auto text = std::string_view{"version = 1\nhue = 1\n"};

    WHEN("it is read")
    {
      const auto failure = toml_reader_test::failure_of<arude::config::exception_t>(
        [&text] -> void { std::ignore = toml_reader_test::read(text); });

      THEN("it is refused: an enumerator is a string, not a number")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
      }
    }
  }

  GIVEN("a colour spelt by its name")
  {
    constexpr auto text = std::string_view{"version = 1\nhue = \"green\"\n"};

    WHEN("it is read")
    {
      const auto loaded = toml_reader_test::read(text);

      THEN("it reads as that enumerator")
      {
        REQUIRE(loaded.hue == toml_reader_test::colour::green);
      }
    }
  }

  GIVEN("a flag field spelt as two '|'-separated names")
  {
    constexpr auto text = std::string_view{"version = 1\nmode = \"a|b\"\n"};

    WHEN("it is read")
    {
      const auto loaded = toml_reader_test::read(text);

      THEN("it reads as the two flags together")
      {
        REQUIRE(loaded.mode == (toml_reader_test::flags::a | toml_reader_test::flags::b));
      }
    }
  }

  GIVEN("a flag field spelt with a part that names no flag")
  {
    constexpr auto text = std::string_view{"version = 1\nmode = \"a|4\"\n"};

    WHEN("it is read")
    {
      const auto failure = toml_reader_test::failure_of<arude::config::exception_t>(
        [&text] -> void { std::ignore = toml_reader_test::read(text); });

      THEN("it is refused, naming the whole value")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
        REQUIRE(failure.str().contains("'a|4' names no enumerator of toml_reader_test::"));
      }
    }
  }
}

SCENARIO("a binary field that is not base64 is an invalid_payload, and does not terminate", "[config][toml_reader]")
{
  GIVEN("text whose binary field is not base64")
  {
    constexpr auto text = std::string_view{"version = 1\nsecret = \"!!!!\"\n"};

    WHEN("it is read")
    {
      const auto failure = toml_reader_test::failure_of<arude::config::exception_t>(
        [&text] -> void { std::ignore = toml_reader_test::read(text); });

      THEN("it is an invalid_payload naming the field and the base64 failure")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
        REQUIRE(failure.str().contains("'secret'"));
        REQUIRE(failure.str().contains("is not a base64 character"));
      }
    }
  }
}

SCENARIO("two map keys spelling one integer are refused rather than silently merged", "[config][toml_reader]")
{
  GIVEN("a map whose keys 007 and 7 are distinct TOML keys spelling one integer")
  {
    constexpr auto text = std::string_view{"version = 1\nlevels = { 007 = \"x\", 7 = \"y\" }\n"};

    WHEN("it is read")
    {
      const auto failure = toml_reader_test::failure_of<arude::config::exception_t>(
        [&text] -> void { std::ignore = toml_reader_test::read(text); });

      THEN("the second entry is refused rather than dropped in silence")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
        REQUIRE(failure.str().contains("the key '7' names the same entry as another key."));
      }
    }
  }
}

SCENARIO("a non-int enum reads with no range specialised", "[config][toml_reader]")
{
  GIVEN("text whose narrow colour is spelt by name")
  {
    constexpr auto text = std::string_view{"version = 1\nsmall_hue = \"green\"\n"};

    WHEN("it is read")
    {
      const auto loaded = toml_reader_test::read(text);

      THEN("it reads as that enumerator")
      {
        REQUIRE(loaded.small_hue == toml_reader_test::small_colour::green);
      }
    }
  }

  GIVEN("text whose narrow colour is spelt as a number")
  {
    constexpr auto text = std::string_view{"version = 1\nsmall_hue = \"300\"\n"};

    WHEN("it is read")
    {
      const auto failure = toml_reader_test::failure_of<arude::config::exception_t>(
        [&text] -> void { std::ignore = toml_reader_test::read(text); });

      THEN("it is refused, as any enum's numeric spelling is")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
        REQUIRE(failure.str().contains("'300' names no enumerator of "));
      }
    }
  }
}
