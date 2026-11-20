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
#include <rfl/Flatten.hpp>
#include <rfl/Rename.hpp>
#include <rfl/Skip.hpp>
#include <rfl/toml/read.hpp>
#include <rfl/toml/write.hpp>
#include <toml++/toml.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <variant>
#include <vector>

namespace toml_test
{
namespace
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

///
/// A configuration with a field narrower than the 64 bits a tree hands over.
///
struct narrow
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
  /// A count that holds 0 to 255.
  ///
  std::uint8_t count = 0;
};

///
/// The root configuration of the file-level scenarios.
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
/// A configuration with a time-typed field of each kind reflect-cpp offers.
///
struct schedule
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
  /// A calendar day, in the pattern the type names. No initialiser, so a
  /// default-constructed schedule value-initialises it rather than parsing
  /// a string that may not match the pattern.
  ///
  rfl::Timestamp<"%Y-%m-%d"> day;

  ///
  /// How long to wait.
  ///
  std::chrono::seconds timeout{30};

  ///
  /// The day the schedule takes effect, a native TOML date.
  ///
  std::chrono::year_month_day due{std::chrono::year{2026} / 8 / 17};
};

///
/// A configuration with a field that may be absent.
///
struct proxy
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
  /// Port to reach it on, where it is not the default one.
  ///
  std::optional<std::uint32_t> port;
};

///
/// One host of a map keyed by host name.
///
struct host
{
  ///
  /// Address the host is reached at.
  ///
  std::string address = "192.0.2.1";

  ///
  /// Port it listens on.
  ///
  std::int64_t port = 443;
};

///
/// A configuration whose map keys are host names, which hold dots.
///
struct web
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
  /// The hosts, by name.
  ///
  std::map<std::string, host> hosts;
};

///
/// A colour, written by name. Of the default underlying type: reflect-cpp
/// cannot list the names of an enumeration with a narrower one.
///
enum class colour // NOLINT(performance-enum-size)
{
  red,
  green
};

///
/// Fields flattened into the type that holds them.
///
struct flat_part
{
  ///
  /// A flattened integer.
  ///
  int a = 1;

  ///
  /// A flattened float.
  ///
  double b = 2.0;
};

///
/// One element of an array of tables.
///
struct item
{
  ///
  /// Identifier.
  ///
  std::string id = "x";

  ///
  /// Count.
  ///
  std::uint16_t n = 3;
};

///
/// A configuration using as much of reflect-cpp as a configuration may, for
/// comparing libarude's conversion with reflect-cpp's own.
///
struct rich
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
  /// A renamed field.
  ///
  rfl::Rename<"host-name", std::string> host = "h";

  ///
  /// Neither written nor read.
  ///
  rfl::Skip<std::string> cache;

  ///
  /// Read, never written.
  ///
  rfl::SkipSerialization<std::string> derived;

  ///
  /// Written, never read.
  ///
  rfl::SkipDeserialization<std::string> computed = "c";

  ///
  /// Fields of another type, at this one's level.
  ///
  rfl::Flatten<flat_part> flat;

  ///
  /// A payload.
  ///
  arude::config::binary secret{
    std::array{std::byte{'f'}, std::byte{'o'}, std::byte{'o'}}
  };

  ///
  /// An optional left disengaged.
  ///
  std::optional<std::uint32_t> port;

  ///
  /// An optional engaged.
  ///
  std::optional<std::string> label = "label";

  ///
  /// An array of tables.
  ///
  std::vector<item> items{
    item{},
    item{.id = "y", .n = 4}
  };

  ///
  /// Nested arrays.
  ///
  std::vector<std::vector<int>> grid{
    {1, 2},
    {3}
  };

  ///
  /// A map.
  ///
  std::map<std::string, int> weights{
    {"a", 1},
    {"b", 2}
  };

  ///
  /// A nested table.
  ///
  flat_part nested;

  ///
  /// An enumeration.
  ///
  colour hue = colour::green;

  ///
  /// A flag.
  ///
  bool flag = true;

  ///
  /// A single-precision float.
  ///
  float ratio = 0.5F;

  ///
  /// A negative 64-bit integer.
  ///
  std::int64_t big = -5;

  ///
  /// A variant.
  ///
  std::variant<int, std::string> either = std::string{"s"};
};

///
/// Returns the whole file as text.
/// \param path File to read.
/// \return The text.
///
auto text_of(const std::filesystem::path& path) -> std::string
{
  auto stream = std::ifstream{path};
  return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

} // namespace

} // namespace toml_test

SCENARIO("a configuration survives being rendered and parsed back", "[config][toml]")
{
  GIVEN("a configuration")
  {
    const auto written = toml_test::make_current();

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
    auto written = toml_test::make_current();
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

    THEN("reading it as the current version without migrating is an invalid_version")
    {
      try
      {
        std::ignore = arude::config::from_toml<test_helpers::config_test_t>(text);
        FAIL("older text read without migrating should have been refused");
      }
      catch(const arude::config::exception_t& ex)
      {
        REQUIRE(ex.data() == arude::config::config_error::invalid_version);
      }
    }
  }
}

SCENARIO("text of the wrong version is refused rather than reinterpreted", "[config][toml]")
{
  GIVEN("text written by the current version")
  {
    const auto text = arude::config::to_toml(toml_test::make_current());

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

    THEN("migrating it, or reading it as it is, is refused as a version too new, not guessed at")
    {
      const auto require_too_new = [](const std::function<void()>& read) -> void
      {
        try
        {
          read();
          FAIL("text newer than this build should have been refused");
        }
        catch(const arude::config::exception_t& ex)
        {
          INFO(std::format("The message is: {}", ex.str()));
          REQUIRE(ex.data() == arude::config::config_error::invalid_version);
          REQUIRE(ex.str().contains("version 99"));
          REQUIRE(ex.str().contains("newer"));
        }
      };

      require_too_new(
        [] -> void { std::ignore = arude::config::from_toml_migrated<test_helpers::config_test_t>(text); });
      require_too_new([] -> void { std::ignore = arude::config::from_toml<test_helpers::config_test_t>(text); });
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

    THEN("reading it as a configuration is an invalid_payload that names the type once")
    {
      try
      {
        std::ignore = arude::config::from_toml<test_helpers::config_test_t>(text);
        FAIL("text without a version should have been refused");
      }
      catch(const arude::config::exception_t& ex)
      {
        const auto message = std::string{ex.str()};
        const auto name = std::string{arude::type_name<test_helpers::config_test_t>()};

        REQUIRE(ex.data() == arude::config::config_error::invalid_payload);
        REQUIRE(message.contains("declares no configuration version."));

        auto count = std::size_t{0};

        for(auto at = message.find(name); at != std::string::npos; at = message.find(name, at + name.size()))
        {
          ++count;
        }

        REQUIRE(count == 1);
      }
    }
  }

  GIVEN("TOML whose unsigned field holds a negative number")
  {
    static constexpr auto text = std::string_view{"version = 2\nname = \"negative\"\nretries = -1\n"};

    THEN("it is refused rather than wrapped round to a very large number")
    {
      try
      {
        std::ignore = arude::config::from_toml<test_helpers::config_test_t>(text);
        FAIL("a negative unsigned field should have been refused");
      }
      catch(const arude::config::exception_t& ex)
      {
        INFO(std::format("The message is: {}", ex.str()));
        REQUIRE(ex.data() == arude::config::config_error::invalid_payload);
        REQUIRE(ex.str().contains("-1"));
      }
    }
  }

  GIVEN("TOML whose field holds a number too large for the field's type")
  {
    static constexpr auto text = std::string_view{"version = 1\ncount = 300\n"};

    THEN("it is refused rather than cut down to what fits")
    {
      try
      {
        std::ignore = arude::config::from_toml<toml_test::narrow>(text);
        FAIL("a value out of the field's range should have been refused");
      }
      catch(const arude::config::exception_t& ex)
      {
        INFO(std::format("The message is: {}", ex.str()));
        REQUIRE(ex.data() == arude::config::config_error::invalid_payload);
        REQUIRE(ex.str().contains("300"));
        REQUIRE(ex.str().contains("out of range"));
      }
    }
  }

  GIVEN("TOML whose version is negative")
  {
    static constexpr auto text = std::string_view{"version = -1\nname = \"negative\"\n"};

    THEN("it is refused as malformed, not mistaken for a version from a newer build")
    {
      const auto require_malformed = [](const std::function<void()>& read) -> void
      {
        try
        {
          read();
          FAIL("a negative version should have been refused");
        }
        catch(const arude::config::exception_t& ex)
        {
          INFO(std::format("The message is: {}", ex.str()));
          REQUIRE(ex.data() == arude::config::config_error::invalid_payload);
          REQUIRE(ex.str().contains("-1"));
          REQUIRE(!ex.str().contains("newer"));
        }
      };

      require_malformed([] -> void { std::ignore = arude::config::toml_version(text); });
      require_malformed([] -> void { std::ignore = arude::config::from_toml<test_helpers::config_test_t>(text); });
      require_malformed(
        [] -> void { std::ignore = arude::config::from_toml_migrated<test_helpers::config_test_t>(text); });
    }
  }
}

SCENARIO("a failure message carries one prefix and quotes what was written as written", "[config][toml]")
{
  GIVEN("a store below a directory whose name spells the library's prefix, where a file blocks the way")
  {
    const auto root = test_helpers::temp_directory{"arude_toml_prefix_path"};
    const auto spelled = root.path() / "arude::config: d";
    std::filesystem::create_directories(spelled);
    test_helpers::write_text(spelled / "blocker", "x");
    const auto blocked = spelled / "blocker" / "sub";

    auto file = arude::config::config_file<test_helpers::config_test_t, arude::config::transport_file>{
      arude::config::transport_file{blocked / "app.toml"}};
    file.set(test_helpers::config_test_t{});

    THEN("the nested filesystem error still quotes the path as it was written")
    {
      try
      {
        file.store();
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

// reflect-cpp maps no type onto a TOML date or time: its TOML reader and
// writer know strings, booleans, integers, floats, tables and arrays and
// nothing else. A timestamp is written as a string in its pattern, and a
// duration as a table of a count and a unit, which is what the duration
// Parser in toml_parsers.hpp reads and writes too. The date and time types
// of step C build on that.
SCENARIO("a time-typed field is stored as reflect-cpp writes it", "[config][toml]")
{
  GIVEN("a file whose root holds a timestamp and a duration")
  {
    const auto file = test_helpers::temp_file{"arude_toml_times.toml"};

    auto config = arude::config::config_file<toml_test::schedule, arude::config::transport_file>{
      arude::config::transport_file{file.path()}};

    WHEN("it is set and stored")
    {
      config.set(toml_test::schedule{.day = "2001-02-03", .timeout = std::chrono::seconds{90}});
      config.store();

      const auto stored = toml::parse(toml_test::text_of(file.path()));

      THEN("the timestamp is a string in its pattern, not a TOML date")
      {
        REQUIRE(stored["day"].is_string());
        REQUIRE(stored["day"].value<std::string>() == "2001-02-03");
      }

      THEN("the duration is a table of a count and a unit")
      {
        REQUIRE(stored["timeout"].is_table());
        REQUIRE(stored["timeout"]["count"].value<std::int64_t>() == 90);
        REQUIRE(stored["timeout"]["unit"].value<std::string>() == "seconds");
      }

      THEN("the calendar date is a native TOML date")
      {
        REQUIRE(stored["due"].is_date());
        REQUIRE(stored["due"].as_date()->get().year == 2026);
      }

      THEN("both read back as they were")
      {
        config.load();

        REQUIRE(config.get().day.reflection() == "2001-02-03");
        REQUIRE(config.get().timeout == std::chrono::seconds{90});
      }
    }
  }

  GIVEN("a file with a native TOML date where the root has a timestamp")
  {
    const auto file = test_helpers::temp_file{"arude_toml_native_date.toml"};
    test_helpers::write_text(
      file.path(),
      "version = 1\n"
      "day = 2001-02-03\n"
      "timeout = { count = 30, unit = \"seconds\" }\n"
      "due = 2026-08-17\n");

    auto config = arude::config::config_file<toml_test::schedule, arude::config::transport_file>{
      arude::config::transport_file{file.path()}};

    THEN("the load is an invalid_payload")
    {
      try
      {
        config.load();
        FAIL("loading a native date as a timestamp should have thrown");
      }
      catch(const arude::config::exception_t& ex)
      {
        INFO(std::format("The message is: {}", ex.str()));
        REQUIRE(ex.data() == arude::config::config_error::invalid_payload);
      }

      REQUIRE(!config.loaded());
    }
  }

  GIVEN("text whose duration cannot be represented without loss")
  {
    static constexpr auto text = std::string_view{"version = 1\n[timeout]\ncount = 1\nunit = \"nanoseconds\"\n"};

    THEN("from_toml is an invalid_payload saying the value does not fit")
    {
      try
      {
        std::ignore = arude::config::from_toml<toml_test::schedule>(text);
        FAIL("a lossy duration should have been refused");
      }
      catch(const arude::config::exception_t& ex)
      {
        INFO(std::format("The message is: {}", ex.str()));
        REQUIRE(ex.data() == arude::config::config_error::invalid_payload);
        REQUIRE(ex.str().contains("cannot be represented as"));
        REQUIRE(ex.str().contains("without loss"));
      }
    }
  }
}

SCENARIO("a disengaged optional is left out of the file and reads back disengaged", "[config][toml]")
{
  GIVEN("a file whose root has a disengaged optional field")
  {
    const auto file = test_helpers::temp_file{"arude_toml_optional.toml"};

    auto config = arude::config::config_file<toml_test::proxy, arude::config::transport_file>{
      arude::config::transport_file{file.path()}};

    WHEN("it is stored and loaded again")
    {
      config.set(toml_test::proxy{});
      config.store();

      const auto stored = toml::parse(toml_test::text_of(file.path()));
      config.load();

      THEN("the field is not in the file, and the root has it disengaged")
      {
        REQUIRE(!stored["port"]);
        REQUIRE(!config.get().port.has_value());
      }
    }
  }
}

SCENARIO("libarude's conversion of a configuration matches reflect-cpp's own", "[config][toml]")
{
  GIVEN("a configuration using as much of reflect-cpp as a configuration may")
  {
    const auto value = toml_test::rich{};

    THEN("to_toml renders it exactly as rfl::toml::write does")
    {
      REQUIRE(arude::config::to_toml(value) == rfl::toml::write(value));
    }

    THEN("from_toml reads back exactly what rfl::toml::read does")
    {
      const auto text = rfl::toml::write(value);
      const auto ours = arude::config::from_toml<toml_test::rich>(text);
      const auto theirs = rfl::toml::read<toml_test::rich>(text);

      REQUIRE(theirs.has_value());
      REQUIRE(rfl::toml::write(ours) == rfl::toml::write(theirs.value()));
    }
  }
}

// toml++ reads a dot in a path as a separator, so a key holding one is looked
// up by key and never by path here.
SCENARIO("keys holding dots survive a load and a store", "[config][toml]")
{
  GIVEN("a file keyed by host names, which hold dots")
  {
    const auto file = test_helpers::temp_file{"arude_toml_dotted.toml"};
    test_helpers::write_text(
      file.path(),
      "version = 1\n"
      "\n"
      "[hosts.\"example.com\"]\n"
      "address = \"192.0.2.7\"\n"
      "port = 443\n");

    auto config = arude::config::config_file<toml_test::web, arude::config::transport_file>{
      arude::config::transport_file{file.path()}};

    WHEN("it is loaded, changed and stored")
    {
      config.load();

      REQUIRE(config.get().hosts.at("example.com").address == "192.0.2.7");

      auto changed = config.get();
      changed.hosts.at("example.com").port = 8443;
      changed.hosts.insert_or_assign("www.example.com", toml_test::host{});
      config.set(changed);
      config.store();

      const auto stored = toml::parse(toml_test::text_of(file.path()));

      THEN("the hosts are tables under their own names")
      {
        const auto& hosts = *stored["hosts"].as_table();

        REQUIRE(hosts.get("example.com") != nullptr);
        REQUIRE(hosts.get("example.com")->as_table()->get("port")->value<std::int64_t>() == 8443);
        REQUIRE(hosts.get("www.example.com") != nullptr);
      }
    }
  }
}

// The root is written from the struct, so keys and tables its type does not
// declare are gone after a store. The loss is accepted, and pinned so that a
// change to it is noticed.
SCENARIO("keys and tables the root does not declare are gone after a store", "[config][toml]")
{
  GIVEN("a file with a table and a key the root's type does not declare")
  {
    const auto file = test_helpers::temp_file{"arude_toml_undeclared.toml"};
    test_helpers::write_text(
      file.path(),
      "version = 1\n"
      "name = \"kept\"\n"
      "stray = 7\n"
      "\n"
      "[foreign]\n"
      "flag = true\n");

    auto config = arude::config::config_file<toml_test::app, arude::config::transport_file>{
      arude::config::transport_file{file.path()}};

    WHEN("it is loaded and stored")
    {
      config.load();
      config.store();

      const auto stored = toml::parse(toml_test::text_of(file.path()));

      THEN("what the root declares is kept, and the rest is gone")
      {
        REQUIRE(stored["name"].value<std::string>() == "kept");
        REQUIRE(!stored["stray"]);
        REQUIRE(!stored["foreign"]);
      }
    }
  }
}

// toml++ does not keep comments, and nothing else here does either. The loss
// is accepted, and pinned so that a change to it is noticed.
SCENARIO("a comment in the file does not survive a store", "[config][toml]")
{
  GIVEN("a file with a comment in it")
  {
    const auto file = test_helpers::temp_file{"arude_toml_comment.toml"};
    test_helpers::write_text(
      file.path(),
      "# Written by hand.\n"
      "version = 1\n"
      "name = \"commented\"\n");

    auto config = arude::config::config_file<toml_test::app, arude::config::transport_file>{
      arude::config::transport_file{file.path()}};

    WHEN("it is loaded and stored")
    {
      config.load();
      config.store();

      THEN("the values are kept and the comment is gone")
      {
        const auto text = toml_test::text_of(file.path());

        REQUIRE(text.contains("commented"));
        REQUIRE(!text.contains("Written by hand"));
      }
    }
  }
}

SCENARIO("a duration's count is converted exactly or refused", "[config][toml]")
{
  GIVEN("text holding a duration in the root's own unit, at the extremes of int64")
  {
    const auto max_text = std::format(
      "version = 1\nday = \"2024-01-02\"\ndue = 2026-08-17\n[timeout]\ncount = {}\nunit = \"seconds\"\n",
      std::numeric_limits<std::int64_t>::max());
    const auto min_text = std::format(
      "version = 1\nday = \"2024-01-02\"\ndue = 2026-08-17\n[timeout]\ncount = {}\nunit = \"seconds\"\n",
      std::numeric_limits<std::int64_t>::min());

    THEN("the maximum and the minimum count read back as themselves")
    {
      REQUIRE(
        arude::config::from_toml<toml_test::schedule>(max_text).timeout.count() ==
        std::numeric_limits<std::int64_t>::max());
      REQUIRE(
        arude::config::from_toml<toml_test::schedule>(min_text).timeout.count() ==
        std::numeric_limits<std::int64_t>::min());
    }
  }

  GIVEN("text whose count in a finer unit divides the target exactly")
  {
    static constexpr auto text = std::string_view{
      "version = 1\nday = \"2024-01-02\"\ndue = 2026-08-17\n[timeout]\ncount = 2000\nunit = \"milliseconds\"\n"};

    THEN("it reads as the exact value")
    {
      REQUIRE(arude::config::from_toml<toml_test::schedule>(text).timeout == std::chrono::seconds{2});
    }
  }

  GIVEN("text whose count in a finer unit does not divide the target")
  {
    static constexpr auto text = std::string_view{
      "version = 1\nday = \"2024-01-02\"\ndue = 2026-08-17\n[timeout]\ncount = 1500\nunit = \"milliseconds\"\n"};

    THEN("it is refused rather than truncated")
    {
      try
      {
        std::ignore = arude::config::from_toml<toml_test::schedule>(text);
        FAIL("a lossy duration should have been refused");
      }
      catch(const arude::config::exception_t& ex)
      {
        REQUIRE(ex.data() == arude::config::config_error::invalid_payload);
        REQUIRE(ex.str().contains("cannot be represented as"));
        REQUIRE(ex.str().contains("without loss"));
      }
    }
  }

  GIVEN("text whose count overflows the target's unit")
  {
    const auto text = std::format(
      "version = 1\nday = \"2024-01-02\"\ndue = 2026-08-17\n[timeout]\ncount = {}\nunit = \"years\"\n",
      std::numeric_limits<std::int64_t>::max());

    THEN("it is refused rather than wrapped")
    {
      try
      {
        std::ignore = arude::config::from_toml<toml_test::schedule>(text);
        FAIL("an overflowing duration should have been refused");
      }
      catch(const arude::config::exception_t& ex)
      {
        REQUIRE(ex.data() == arude::config::config_error::invalid_payload);
        REQUIRE(ex.str().contains("cannot be represented as"));
        REQUIRE(ex.str().contains("without loss"));
      }
    }
  }
}
