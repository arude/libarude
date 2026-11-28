///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// The date and time types of a configuration: std::chrono::sys_time<D> as
/// a TOML offset date-time, std::chrono::local_time<D> as a local date-time,
/// std::chrono::year_month_day as a local date, and std::chrono::hh_mm_ss<D>
/// as a local time. Each reads its native form and a quoted string in TOML's
/// own syntax, writes only the native form, and refuses rather than losing
/// information.
///

#include "arude/config.hpp"
#include "test/configuration/helpers/memory_transport.hpp"
#include "test/helpers/temp_file.hpp"

#include <catch2/catch_test_macros.hpp>
#include <toml++/toml.hpp>

#include <chrono>
#include <concepts>
#include <cstdint>
#include <format>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace toml_time_test
{
namespace
{

using arude::config::version_t;

///
/// A configuration with a member of each date and time type, at two
/// durations each where the exactness rules differ.
///
struct times
{
  ///
  /// The version this type is.
  ///
  static constexpr auto config_version = version_t{1};

  ///
  /// A time point at nanosecond precision.
  ///
  std::chrono::sys_time<std::chrono::nanoseconds> created;

  ///
  /// A time point at second precision.
  ///
  std::chrono::sys_time<std::chrono::seconds> created_coarse;

  ///
  /// A time point at day precision.
  ///
  std::chrono::sys_time<std::chrono::days> created_days;

  ///
  /// A time point at minute precision.
  ///
  std::chrono::sys_time<std::chrono::minutes> created_minutes;

  ///
  /// A local time point at second precision.
  ///
  std::chrono::local_time<std::chrono::seconds> updated;

  ///
  /// A local time point at millisecond precision.
  ///
  std::chrono::local_time<std::chrono::milliseconds> updated_ms;

  ///
  /// A calendar date.
  ///
  std::chrono::year_month_day due;

  ///
  /// A time of day at millisecond precision.
  ///
  std::chrono::hh_mm_ss<std::chrono::milliseconds> at;

  ///
  /// A time of day at second precision.
  ///
  std::chrono::hh_mm_ss<std::chrono::seconds> at_coarse;
};

///
/// The periods at the edges of what TOML holds: whole weeks, hh_mm_ss at
/// minutes, hours and days, an exact decimal fraction of a second, and a
/// representation narrower than int64, as MSVC gives minutes and days.
///
struct coarse_times
{
  ///
  /// The version this type is.
  ///
  static constexpr auto config_version = version_t{1};

  ///
  /// A time point at whole weeks.
  ///
  std::chrono::sys_time<std::chrono::weeks> week;

  ///
  /// A time of day at minute precision.
  ///
  std::chrono::hh_mm_ss<std::chrono::minutes> at_minutes;

  ///
  /// A time of day at hour precision.
  ///
  std::chrono::hh_mm_ss<std::chrono::hours> at_hours;

  ///
  /// A time of day at day precision.
  ///
  std::chrono::hh_mm_ss<std::chrono::days> at_days;

  ///
  /// A time point in units of 1/100 of a second.
  ///
  std::chrono::sys_time<std::chrono::duration<std::int64_t, std::ratio<1, 100>>> in_centiseconds;

  ///
  /// A time point whose representation is a 32-bit int, as MSVC gives
  /// minutes, hours and days.
  ///
  std::chrono::sys_time<std::chrono::duration<std::int32_t>> narrow;
};

///
/// Members whose representation is a 32-bit int at minute precision, as
/// MSVC defines std::chrono::minutes.
///
struct narrow_times
{
  ///
  /// The version this type is.
  ///
  static constexpr auto config_version = version_t{1};

  ///
  /// A time point in 32-bit minutes, which end about 4083 years from the
  /// epoch.
  ///
  std::chrono::sys_time<std::chrono::duration<std::int32_t, std::ratio<60>>> stamp;

  ///
  /// A local time point in 32-bit minutes.
  ///
  std::chrono::local_time<std::chrono::duration<std::int32_t, std::ratio<60>>> local;

  ///
  /// A time of day in 32-bit minutes.
  ///
  std::chrono::hh_mm_ss<std::chrono::duration<std::int32_t, std::ratio<60>>> at;
};

///
/// The same members, each behind an optional.
///
struct optional_times
{
  ///
  /// The version this type is.
  ///
  static constexpr auto config_version = version_t{1};

  ///
  /// An optional time point.
  ///
  std::optional<std::chrono::sys_time<std::chrono::nanoseconds>> created;

  ///
  /// An optional local time point.
  ///
  std::optional<std::chrono::local_time<std::chrono::seconds>> updated;

  ///
  /// An optional calendar date.
  ///
  std::optional<std::chrono::year_month_day> due;

  ///
  /// An optional time of day.
  ///
  std::optional<std::chrono::hh_mm_ss<std::chrono::milliseconds>> at;
};

///
/// Time members in vectors.
///
struct vector_times
{
  ///
  /// The version this type is.
  ///
  static constexpr auto config_version = version_t{1};

  ///
  /// Time points in a vector.
  ///
  std::vector<std::chrono::sys_time<std::chrono::seconds>> stamps;

  ///
  /// Calendar dates in a vector.
  ///
  std::vector<std::chrono::year_month_day> dues;

  ///
  /// Local time points in a vector.
  ///
  std::vector<std::chrono::local_time<std::chrono::seconds>> revisions;

  ///
  /// Times of day in a vector.
  ///
  std::vector<std::chrono::hh_mm_ss<std::chrono::milliseconds>> ticks;
};

///
/// Version 1 of an event a versioned member holds.
///
struct event_v1
{
  ///
  /// The version this type is.
  ///
  static constexpr auto config_version = version_t{1};

  ///
  /// The event's name.
  ///
  std::string name = "event";
};

///
/// Version 2 of the event, which gains a time point.
///
struct event_v2
{
  ///
  /// The version before this one.
  ///
  using previous_t = event_v1;

  ///
  /// \see event_v1::config_version
  ///
  static constexpr auto config_version = version_t{2};

  ///
  /// \see event_v1::name
  ///
  std::string name = "event";

  ///
  /// When the event happens.
  ///
  std::chrono::sys_time<std::chrono::seconds> happened;
};

///
/// Upgrades an event from version 1 to version 2.
/// \param val The version 1 event. Not retained.
/// \return The same event as version 2.
///
[[nodiscard]] constexpr auto upgrade(const event_v1& val) -> event_v2;

///
/// Downgrades an event from version 2 to version 1.
/// \param val The version 2 event. Not retained.
/// \return The same event as version 1.
///
[[nodiscard]] constexpr auto downgrade(const event_v2& val) -> event_v1;

///
///
constexpr auto upgrade(const event_v1& val) -> event_v2
{
  return event_v2{.name = val.name};
}

///
///
constexpr auto downgrade(const event_v2& val) -> event_v1
{
  return event_v1{.name = val.name};
}

///
/// A root holding the event in a versioned member.
///
struct schedule_root
{
  ///
  /// The version this type is.
  ///
  static constexpr auto config_version = version_t{1};

  ///
  /// The event, carrying its own version.
  ///
  arude::config::versioned<event_v2> event;
};

///
/// A complete current text for times, with every member at a known value.
///
constexpr auto times_text =
  std::string_view{"version = 1\n"
                   "created = 2026-01-02T03:04:05.25+02:00\n"
                   "created_coarse = 2026-01-02T01:02:03Z\n"
                   "created_days = 2026-01-02T00:00:00Z\ncreated_minutes = 2026-01-02T01:02:00Z\n"
                   "updated = 2026-01-02T01:02:03\n"
                   "updated_ms = 2026-01-02T01:02:03.5\n"
                   "due = 2026-08-17\n"
                   "at = 07:08:09.5\n"
                   "at_coarse = 07:08:09\n"};

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

///
/// A time point whose period is spelled unreduced, ten hundredths of a second.
///
struct unreduced_times
{
  ///
  /// The version this type is.
  ///
  static constexpr auto config_version = version_t{1};

  ///
  /// A time point in tenths of a second, spelled std::ratio<10, 100>.
  ///
  std::chrono::sys_time<std::chrono::duration<std::int64_t, std::ratio<10, 100>>> at;
};

///
/// A time of day at minute precision, for values far beyond a day.
///
struct minute_of_day
{
  ///
  /// The version this type is.
  ///
  static constexpr auto config_version = version_t{1};

  ///
  /// A time of day at minute precision.
  ///
  std::chrono::hh_mm_ss<std::chrono::minutes> at;
};

} // namespace

} // namespace toml_time_test

SCENARIO("each time type reads its native and its quoted form", "[config][toml_time]")
{
  GIVEN("text holding every member natively")
  {
    const auto native = arude::config::from_toml<toml_time_test::times>(toml_time_test::times_text);

    GIVEN("the same text with every member quoted")
    {
      constexpr auto quoted_text = std::string_view{"version = 1\n"
                                                    "created = \"2026-01-02T03:04:05.25+02:00\"\n"
                                                    "created_coarse = \"2026-01-02T01:02:03Z\"\n"
                                                    "created_days = \"2026-01-02T00:00:00Z\"\n"
                                                    "created_minutes = \"2026-01-02T01:02:00Z\"\n"
                                                    "updated = \"2026-01-02 01:02:03\"\n"
                                                    "updated_ms = \"2026-01-02t01:02:03.5\"\n"
                                                    "due = \"2026-08-17\"\n"
                                                    "at = \"07:08:09.5\"\n"
                                                    "at_coarse = \"07:08:09\"\n"};

      WHEN("both are read")
      {
        const auto quoted = arude::config::from_toml<toml_time_test::times>(quoted_text);

        THEN("they agree")
        {
          REQUIRE(quoted.created == native.created);
          REQUIRE(quoted.created_coarse == native.created_coarse);
          REQUIRE(quoted.created_days == native.created_days);
          REQUIRE(quoted.created_minutes == native.created_minutes);
          REQUIRE(quoted.updated == native.updated);
          REQUIRE(quoted.updated_ms == native.updated_ms);
          REQUIRE(quoted.due == native.due);
          REQUIRE(quoted.at.to_duration() == native.at.to_duration());
          REQUIRE(quoted.at_coarse.to_duration() == native.at_coarse.to_duration());
        }
      }
    }

    THEN("the offset is converted to UTC")
    {
      REQUIRE(
        native.created == std::chrono::sys_time<std::chrono::nanoseconds>{
                            std::chrono::sys_days{std::chrono::year{2026} / 1 / 2} +
                            std::chrono::hours{1} +
                            std::chrono::minutes{4} +
                            std::chrono::seconds{5} +
                            std::chrono::milliseconds{250}});
      REQUIRE(
        native.at.to_duration() ==
        std::chrono::hours{7} + std::chrono::minutes{8} + std::chrono::seconds{9} + std::chrono::milliseconds{500});
    }
  }
}

SCENARIO("each time type writes its native form and round-trips", "[config][toml_time]")
{
  GIVEN("a configuration whose members are set")
  {
    auto value = toml_time_test::times{};
    value.created = std::chrono::sys_days{std::chrono::year{2026} / 1 / 2} +
                    std::chrono::hours{1} +
                    std::chrono::minutes{2} +
                    std::chrono::seconds{3} +
                    std::chrono::milliseconds{250};
    value.created_coarse = std::chrono::sys_days{std::chrono::year{2026} / 1 / 2} +
                           std::chrono::hours{1} +
                           std::chrono::minutes{2} +
                           std::chrono::seconds{3};
    value.created_days = std::chrono::sys_days{std::chrono::year{2026} / 1 / 2};
    value.created_minutes =
      std::chrono::sys_days{std::chrono::year{2026} / 1 / 2} + std::chrono::hours{1} + std::chrono::minutes{2};
    value.updated = std::chrono::local_days{std::chrono::year{2026} / 1 / 2} +
                    std::chrono::hours{1} +
                    std::chrono::minutes{2} +
                    std::chrono::seconds{3};
    value.updated_ms = std::chrono::local_days{std::chrono::year{2026} / 1 / 2} +
                       std::chrono::hours{1} +
                       std::chrono::minutes{2} +
                       std::chrono::seconds{3} +
                       std::chrono::milliseconds{500};
    value.due = std::chrono::year{2026} / 8 / 17;
    value.at = std::chrono::hh_mm_ss<std::chrono::milliseconds>{
      std::chrono::hours{7} + std::chrono::minutes{8} + std::chrono::seconds{9} + std::chrono::milliseconds{500}};
    value.at_coarse = std::chrono::hh_mm_ss<std::chrono::seconds>{
      std::chrono::hours{7} + std::chrono::minutes{8} + std::chrono::seconds{9}};

    WHEN("it is rendered")
    {
      const auto text = arude::config::to_toml(value);

      THEN("every member is a native TOML value, not a string")
      {
        const auto document = toml::parse(text);

        REQUIRE(document["created"].is_date_time());
        REQUIRE(document["created_coarse"].is_date_time());
        REQUIRE(document["created_days"].is_date_time());
        REQUIRE(document["created_minutes"].is_date_time());
        REQUIRE(document["updated"].is_date_time());
        REQUIRE(!document["updated"].as_date_time()->get().offset.has_value());
        REQUIRE(document["updated_ms"].is_date_time());
        REQUIRE(document["due"].is_date());
        REQUIRE(document["at"].is_time());
        REQUIRE(document["at_coarse"].is_time());

        const auto& created_offset = document["created"].as_date_time()->get().offset;

        if(created_offset.has_value())
        {
          REQUIRE(created_offset->minutes == 0);
        }
        else
        {
          FAIL("the offset date-time was expected to carry an offset");
        }
      }

      THEN("the offset form is UTC, with Z")
      {
        REQUIRE(text.contains("created = 2026-01-02T01:02:03.25Z"));
      }

      THEN("the local form carries no offset")
      {
        REQUIRE(text.contains("updated = 2026-01-02T01:02:03"));
      }

      THEN("it reads back as it went in")
      {
        const auto back = arude::config::from_toml<toml_time_test::times>(text);

        REQUIRE(back.created == value.created);
        REQUIRE(back.created_coarse == value.created_coarse);
        REQUIRE(back.created_days == value.created_days);
        REQUIRE(back.created_minutes == value.created_minutes);
        REQUIRE(back.updated == value.updated);
        REQUIRE(back.updated_ms == value.updated_ms);
        REQUIRE(back.due == value.due);
        REQUIRE(back.at.to_duration() == value.at.to_duration());
        REQUIRE(back.at_coarse.to_duration() == value.at_coarse.to_duration());
      }
    }
  }
}

SCENARIO("a value of one kind read as another is refused, naming the type that fits", "[config][toml_time]")
{
  GIVEN("text whose sys_time member holds a local date-time")
  {
    const auto text = std::format(
      "version = 1\ncreated = 2026-01-02T01:02:03\n{}\n",
      std::string_view{"created_coarse = 2026-01-02T01:02:03Z\ncreated_days = 2026-01-02T00:00:00Z\n"
                       "created_minutes = 2026-01-02T01:02:00Z\n"
                       "updated = 2026-01-02T01:02:03\nupdated_ms = 2026-01-02T01:02:03.5\n"
                       "due = 2026-08-17\nat = 07:08:09.5\nat_coarse = 07:08:09\n"});

    THEN("it is refused, and the message says a local_time fits")
    {
      const auto failure = toml_time_test::failure_of<arude::config::exception_t>(
        [&text] -> void { std::ignore = arude::config::from_toml<toml_time_test::times>(text); });

      REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
      REQUIRE(failure.str().contains("the value is 2026-01-02T01:02:03, which fits a std::chrono::local_time."));
    }
  }

  GIVEN("text whose local_time member holds an offset date-time")
  {
    const auto text = std::format(
      "version = 1\nupdated = 2026-01-02T01:02:03+01:00\n{}\n",
      std::string_view{"created = 2026-01-02T03:04:05.25+02:00\ncreated_coarse = 2026-01-02T01:02:03Z\n"
                       "created_days = 2026-01-02T00:00:00Z\ncreated_minutes = 2026-01-02T01:02:00Z\n"
                       "updated_ms = 2026-01-02T01:02:03.5\n"
                       "due = 2026-08-17\nat = 07:08:09.5\nat_coarse = 07:08:09\n"});

    THEN("it is refused, and the message says a sys_time fits")
    {
      const auto failure = toml_time_test::failure_of<arude::config::exception_t>(
        [&text] -> void { std::ignore = arude::config::from_toml<toml_time_test::times>(text); });

      REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
      REQUIRE(failure.str().contains("the value is 2026-01-02T01:02:03+01:00, which fits a std::chrono::sys_time."));
    }
  }

  GIVEN("text whose year_month_day member holds a time")
  {
    const auto text = std::format(
      "version = 1\ndue = 07:08:09\n{}\n",
      std::string_view{"created = 2026-01-02T03:04:05.25+02:00\ncreated_coarse = 2026-01-02T01:02:03Z\n"
                       "created_days = 2026-01-02T00:00:00Z\ncreated_minutes = 2026-01-02T01:02:00Z\n"
                       "updated = 2026-01-02T01:02:03\n"
                       "updated_ms = 2026-01-02T01:02:03.5\nat = 07:08:09.5\nat_coarse = 07:08:09\n"});

    THEN("it is refused, and the message says an hh_mm_ss fits")
    {
      const auto failure = toml_time_test::failure_of<arude::config::exception_t>(
        [&text] -> void { std::ignore = arude::config::from_toml<toml_time_test::times>(text); });

      REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
      REQUIRE(failure.str().contains("the value is 07:08:09, which fits a std::chrono::hh_mm_ss."));
    }
  }

  GIVEN("text whose hh_mm_ss member holds a date")
  {
    const auto text = std::format(
      "version = 1\nat = 2026-01-02\n{}\n",
      std::string_view{"created = 2026-01-02T03:04:05.25+02:00\ncreated_coarse = 2026-01-02T01:02:03Z\n"
                       "created_days = 2026-01-02T00:00:00Z\ncreated_minutes = 2026-01-02T01:02:00Z\n"
                       "updated = 2026-01-02T01:02:03\n"
                       "updated_ms = 2026-01-02T01:02:03.5\ndue = 2026-08-17\nat_coarse = 07:08:09\n"});

    THEN("it is refused, and the message says a year_month_day fits")
    {
      const auto failure = toml_time_test::failure_of<arude::config::exception_t>(
        [&text] -> void { std::ignore = arude::config::from_toml<toml_time_test::times>(text); });

      REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
      REQUIRE(failure.str().contains("the value is 2026-01-02, which fits a std::chrono::year_month_day."));
    }
  }
}

SCENARIO("a value finer than the member's duration is refused", "[config][toml_time]")
{
  GIVEN("text whose second-precision members hold a fraction")
  {
    const auto text = std::format(
      "version = 1\nupdated = 2026-01-02T01:02:03.5\nat_coarse = 07:08:09.5\n{}\n",
      std::string_view{"created = 2026-01-02T03:04:05.25+02:00\ncreated_coarse = 2026-01-02T01:02:03Z\n"
                       "created_days = 2026-01-02T00:00:00Z\ncreated_minutes = 2026-01-02T01:02:00Z\n"
                       "updated_ms = 2026-01-02T01:02:03.5\n"
                       "due = 2026-08-17\nat = 07:08:09.5\n"});

    THEN("each is refused, saying the value is finer than the duration holds")
    {
      const auto failure = toml_time_test::failure_of<arude::config::exception_t>(
        [&text] -> void { std::ignore = arude::config::from_toml<toml_time_test::times>(text); });

      REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
      REQUIRE(failure.str().contains("2026-01-02T01:02:03.5 is finer than seconds holds."));
      REQUIRE(failure.str().contains("07:08:09.5 is finer than seconds holds."));
    }
  }

  GIVEN("text whose millisecond-precision time holds half a millisecond")
  {
    const auto text = std::format(
      "version = 1\nat = 07:08:09.0005\n{}\n",
      std::string_view{"created = 2026-01-02T03:04:05.25+02:00\ncreated_coarse = 2026-01-02T01:02:03Z\n"
                       "created_days = 2026-01-02T00:00:00Z\ncreated_minutes = 2026-01-02T01:02:00Z\n"
                       "updated = 2026-01-02T01:02:03\n"
                       "updated_ms = 2026-01-02T01:02:03.5\ndue = 2026-08-17\nat_coarse = 07:08:09\n"});

    THEN("it is refused rather than truncated")
    {
      const auto failure = toml_time_test::failure_of<arude::config::exception_t>(
        [&text] -> void { std::ignore = arude::config::from_toml<toml_time_test::times>(text); });

      REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
      REQUIRE(failure.str().contains("07:08:09.0005 is finer than milliseconds holds."));
    }
  }

  GIVEN("text whose day-precision time point holds a time of day")
  {
    const auto text = std::format(
      "version = 1\ncreated_days = 2026-01-02T03:04:05Z\n{}\n",
      std::string_view{"created = 2026-01-02T03:04:05.25+02:00\ncreated_coarse = 2026-01-02T01:02:03Z\n"
                       "created_minutes = 2026-01-02T01:02:00Z\n"
                       "updated = 2026-01-02T01:02:03\nupdated_ms = 2026-01-02T01:02:03.5\n"
                       "due = 2026-08-17\nat = 07:08:09.5\nat_coarse = 07:08:09\n"});

    THEN("it is refused: the seconds do not divide a day")
    {
      const auto failure = toml_time_test::failure_of<arude::config::exception_t>(
        [&text] -> void { std::ignore = arude::config::from_toml<toml_time_test::times>(text); });

      REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
      REQUIRE(failure.str().contains("cannot be represented as days exactly."));
    }
  }

  GIVEN("text whose minute-precision time point holds a second")
  {
    const auto text = std::format(
      "version = 1\ncreated_minutes = 2026-01-02T01:02:03Z\n{}\n",
      std::string_view{"created = 2026-01-02T03:04:05.25+02:00\ncreated_coarse = 2026-01-02T01:02:03Z\n"
                       "created_days = 2026-01-02T00:00:00Z\n"
                       "updated = 2026-01-02T01:02:03\nupdated_ms = 2026-01-02T01:02:03.5\n"
                       "due = 2026-08-17\nat = 07:08:09.5\nat_coarse = 07:08:09\n"});

    THEN("it is refused: the seconds do not divide a minute")
    {
      const auto failure = toml_time_test::failure_of<arude::config::exception_t>(
        [&text] -> void { std::ignore = arude::config::from_toml<toml_time_test::times>(text); });

      REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
      REQUIRE(failure.str().contains("cannot be represented as minutes exactly."));
    }
  }
}

SCENARIO("an invalid date is refused", "[config][toml_time]")
{
  GIVEN("text holding the 30th of February natively")
  {
    constexpr auto text = std::string_view{"version = 1\ndue = 2026-02-30\n"};

    THEN("it is refused as not valid TOML")
    {
      const auto failure = toml_time_test::failure_of<arude::config::exception_t>(
        [&text] -> void { std::ignore = arude::config::from_toml<toml_time_test::times>(text); });

      REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
      REQUIRE(failure.str().contains("is not valid TOML"));
    }
  }

  GIVEN("text holding the 30th of February quoted")
  {
    constexpr auto text = std::string_view{"version = 1\ndue = \"2026-02-30\"\n"};

    THEN("it is refused, as the quoted string is parsed by toml++ too, with toml++'s reason")
    {
      const auto failure = toml_time_test::failure_of<arude::config::exception_t>(
        [&text] -> void { std::ignore = arude::config::from_toml<toml_time_test::times>(text); });

      REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
      REQUIRE(failure.str().contains("the text is not a date or a time: "));
      REQUIRE(failure.str().contains("saw 30"));
    }
  }
}

SCENARIO("a leap second is refused", "[config][toml_time]")
{
  // toml++ rejects a second of 60 when it parses: natively the document is
  // refused as not valid TOML before libarude's reader sees it, and quoted
  // the reader's own toml++ parse of the string refuses it. Either way the
  // leap second is invalid_payload and never silently dropped.
  GIVEN("text holding a leap second natively")
  {
    constexpr auto text = std::string_view{"version = 1\nat = 23:59:60\n"};

    THEN("it is refused as not valid TOML")
    {
      const auto failure = toml_time_test::failure_of<arude::config::exception_t>(
        [&text] -> void { std::ignore = arude::config::from_toml<toml_time_test::times>(text); });

      REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
      REQUIRE(failure.str().contains("is not valid TOML"));
    }
  }

  GIVEN("text holding a leap second quoted")
  {
    constexpr auto text = std::string_view{"version = 1\nat = \"23:59:60\"\n"};

    THEN("it is refused, as the quoted string is parsed by toml++ too, with toml++'s reason")
    {
      const auto failure = toml_time_test::failure_of<arude::config::exception_t>(
        [&text] -> void { std::ignore = arude::config::from_toml<toml_time_test::times>(text); });

      REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
      REQUIRE(failure.str().contains("the text is not a date or a time: "));
      REQUIRE(failure.str().contains("saw 60"));
    }
  }
}

SCENARIO("a sys_time of nanoseconds refuses a year beyond its range", "[config][toml_time]")
{
  GIVEN("text holding year 9999 in a nanosecond member")
  {
    constexpr auto text = std::string_view{"version = 1\ncreated = 9999-01-01T00:00:00Z\n"};

    THEN("it is refused: the value ends beyond what nanoseconds hold")
    {
      const auto failure = toml_time_test::failure_of<arude::config::exception_t>(
        [&text] -> void { std::ignore = arude::config::from_toml<toml_time_test::times>(text); });

      REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
      REQUIRE(failure.str().contains("9999-01-01T00:00:00+00:00 is outside the range of nanoseconds."));
    }
  }

  GIVEN("text holding year 9999 in a second-precision member")
  {
    const auto text = std::format(
      "version = 1\ncreated_coarse = 9999-01-01T00:00:00Z\n{}\n",
      std::string_view{"created = 2026-01-02T03:04:05.25+02:00\ncreated_days = 2026-01-02T00:00:00Z\n"
                       "created_minutes = 2026-01-02T01:02:00Z\n"
                       "updated = 2026-01-02T01:02:03\nupdated_ms = 2026-01-02T01:02:03.5\n"
                       "due = 2026-08-17\nat = 07:08:09.5\nat_coarse = 07:08:09\n"});

    THEN("it reads: seconds hold the year")
    {
      const auto value = arude::config::from_toml<toml_time_test::times>(text);

      REQUIRE(
        value.created_coarse ==
        std::chrono::sys_time<std::chrono::seconds>{std::chrono::sys_days{std::chrono::year{9999} / 1 / 1}});
    }
  }
}

SCENARIO("a quoted string refuses injection, comments and garbage", "[config][toml_time]")
{
  GIVEN("quoted strings that are not one date or time")
  {
    const auto texts = {
      std::string_view{"version = 1\nat = \"07:08:09\\nx = 1\"\n"},
      std::string_view{"version = 1\nat = \"07:08:09 # comment\"\n"},
      std::string_view{"version = 1\nat = \"garbage\"\n"},
      std::string_view{"version = 1\nat = \"07:08:09Z trailing\"\n"}};

    THEN("each is refused")
    {
      for(const auto text : texts)
      {
        const auto failure = toml_time_test::failure_of<arude::config::exception_t>(
          [&text] -> void { std::ignore = arude::config::from_toml<toml_time_test::times>(text); });

        REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
        REQUIRE(failure.str().contains("the text is not a date or a time."));
      }
    }
  }
}

SCENARIO("time members may sit in optionals and vectors", "[config][toml_time]")
{
  GIVEN("text holding some optional members and some vectors")
  {
    constexpr auto text = std::string_view{"version = 1\n"
                                           "created = 2026-01-02T03:04:05.25+02:00\n"
                                           "due = 2026-08-17\n"
                                           "stamps = [2026-01-02T03:04:05Z, 2027-03-04T05:06:07Z]\n"
                                           "dues = [2026-08-17, 2027-09-18]\n"
                                           "revisions = [2026-01-02T01:02:03, 2027-03-04T05:06:07]\n"
                                           "ticks = [07:08:09.5, 08:09:10]\n"};

    WHEN("it is read")
    {
      const auto optionals = arude::config::from_toml<toml_time_test::optional_times>(text);
      const auto vectors = arude::config::from_toml<toml_time_test::vector_times>(text);

      THEN("the engaged optionals hold their values, and the rest stay disengaged")
      {
        REQUIRE(optionals.created.has_value());

        if(optionals.due.has_value())
        {
          REQUIRE(optionals.due.value() == std::chrono::year{2026} / 8 / 17);
        }
        else
        {
          FAIL("the optional date was expected to be engaged");
        }

        REQUIRE(!optionals.updated.has_value());
        REQUIRE(!optionals.at.has_value());
      }

      THEN("the vectors hold theirs")
      {
        REQUIRE(vectors.stamps.size() == 2);
        REQUIRE(
          vectors.stamps.at(0) == std::chrono::sys_time<std::chrono::seconds>{
                                    std::chrono::sys_days{std::chrono::year{2026} / 1 / 2} +
                                    std::chrono::hours{3} +
                                    std::chrono::minutes{4} +
                                    std::chrono::seconds{5}});
        REQUIRE(vectors.dues.at(1) == std::chrono::year{2027} / 9 / 18);
        REQUIRE(
          vectors.revisions.at(1) == std::chrono::local_time<std::chrono::seconds>{
                                       std::chrono::local_days{std::chrono::year{2027} / 3 / 4} +
                                       std::chrono::hours{5} +
                                       std::chrono::minutes{6} +
                                       std::chrono::seconds{7}});
        REQUIRE(
          vectors.ticks.at(1).to_duration() ==
          std::chrono::hours{8} + std::chrono::minutes{9} + std::chrono::seconds{10});
      }
    }
  }

  GIVEN("an engaged optional and a full vector, to be rendered")
  {
    auto optionals = toml_time_test::optional_times{};
    optionals.created = std::chrono::sys_time<std::chrono::nanoseconds>{
      std::chrono::sys_days{std::chrono::year{2026} / 1 / 2} +
      std::chrono::hours{1} +
      std::chrono::minutes{2} +
      std::chrono::seconds{3} +
      std::chrono::milliseconds{250}};
    optionals.due = std::chrono::year{2026} / 8 / 17;
    optionals.updated = std::chrono::local_time<std::chrono::seconds>{
      std::chrono::local_days{std::chrono::year{2027} / 3 / 4} + std::chrono::hours{5}};
    optionals.at = std::chrono::hh_mm_ss<std::chrono::milliseconds>{
      std::chrono::hours{7} + std::chrono::minutes{8} + std::chrono::milliseconds{9500}};

    auto vectors = toml_time_test::vector_times{};
    vectors.stamps = {
      std::chrono::sys_time<std::chrono::seconds>{
        std::chrono::sys_days{std::chrono::year{2026} / 1 / 2} +
        std::chrono::hours{3} +
        std::chrono::minutes{4} +
        std::chrono::seconds{5}},
      std::chrono::sys_time<std::chrono::seconds>{
        std::chrono::sys_days{std::chrono::year{2027} / 3 / 4} +
        std::chrono::hours{5} +
        std::chrono::minutes{6} +
        std::chrono::seconds{7}}};
    vectors.dues = {std::chrono::year{2026} / 8 / 17, std::chrono::year{2027} / 9 / 18};
    vectors.revisions = {
      std::chrono::local_time<std::chrono::seconds>{
        std::chrono::local_days{std::chrono::year{2026} / 1 / 2} +
        std::chrono::hours{1} +
        std::chrono::minutes{2} +
        std::chrono::seconds{3}},
      std::chrono::local_time<std::chrono::seconds>{
        std::chrono::local_days{std::chrono::year{2027} / 3 / 4} +
        std::chrono::hours{5} +
        std::chrono::minutes{6} +
        std::chrono::seconds{7}}};
    vectors.ticks = {
      std::chrono::hh_mm_ss<std::chrono::milliseconds>{
        std::chrono::hours{7} + std::chrono::minutes{8} + std::chrono::seconds{9} + std::chrono::milliseconds{500}},
      std::chrono::hh_mm_ss<std::chrono::milliseconds>{
        std::chrono::hours{8} + std::chrono::minutes{9} + std::chrono::seconds{10}}};

    WHEN("both are rendered with the optionals disengaged again")
    {
      const auto empty_text = arude::config::to_toml(toml_time_test::optional_times{});

      THEN("the disengaged optionals are left out")
      {
        const auto empty_doc = toml::parse(empty_text);

        REQUIRE(!empty_doc.contains("created"));
        REQUIRE(!empty_doc.contains("due"));
        REQUIRE(!empty_doc.contains("updated"));
        REQUIRE(!empty_doc.contains("at"));
      }
    }

    WHEN("both are rendered")
    {
      const auto optional_text = arude::config::to_toml(optionals);
      const auto vector_text = arude::config::to_toml(vectors);
      const auto optional_doc = toml::parse(optional_text);
      const auto vector_doc = toml::parse(vector_text);

      THEN("every member is a native TOML value")
      {
        REQUIRE(optional_doc["created"].is_date_time());
        REQUIRE(optional_doc["due"].is_date());
        REQUIRE(optional_doc["updated"].is_date_time());
        REQUIRE(optional_doc["at"].is_time());
        REQUIRE(vector_doc["stamps"][0].is_date_time());
        REQUIRE(vector_doc["dues"][0].is_date());
        REQUIRE(vector_doc["revisions"][0].is_date_time());
        REQUIRE(vector_doc["ticks"][0].is_time());
      }

      THEN("both read back as they went in")
      {
        const auto optional_back = arude::config::from_toml<toml_time_test::optional_times>(optional_text);
        const auto vector_back = arude::config::from_toml<toml_time_test::vector_times>(vector_text);

        REQUIRE(optional_back.created == optionals.created);
        REQUIRE(optional_back.due == optionals.due);
        REQUIRE(optional_back.updated == optionals.updated);
        REQUIRE(
          optional_back.at.transform([](const auto& at) -> std::chrono::milliseconds { return at.to_duration(); }) ==
          optionals.at.transform([](const auto& at) -> std::chrono::milliseconds { return at.to_duration(); }));
        REQUIRE(vector_back.stamps == vectors.stamps);
        REQUIRE(vector_back.dues == vectors.dues);
        REQUIRE(vector_back.revisions == vectors.revisions);
        REQUIRE(vector_back.ticks.at(0).to_duration() == vectors.ticks.at(0).to_duration());
        REQUIRE(vector_back.ticks.at(1).to_duration() == vectors.ticks.at(1).to_duration());
      }
    }
  }
}

SCENARIO("a versioned member holds a time member", "[config][toml_time]")
{
  GIVEN("text whose versioned member is at version 2, holding a time point")
  {
    constexpr auto text = std::string_view{"version = 1\n"
                                           "[event]\n"
                                           "version = 2\n"
                                           "name = \"launch\"\n"
                                           "happened = 2026-01-02T03:04:05Z\n"};

    WHEN("it is read and written again")
    {
      const auto value = arude::config::from_toml<toml_time_test::schedule_root>(text);
      const auto rendered = arude::config::to_toml(value);
      const auto back = arude::config::from_toml<toml_time_test::schedule_root>(rendered);

      THEN("the time member round-trips inside the versioned member")
      {
        REQUIRE(
          value.event.value().happened == std::chrono::sys_time<std::chrono::seconds>{
                                            std::chrono::sys_days{std::chrono::year{2026} / 1 / 2} +
                                            std::chrono::hours{3} +
                                            std::chrono::minutes{4} +
                                            std::chrono::seconds{5}});
        REQUIRE(back.event.value().happened == value.event.value().happened);
      }
    }
  }
}

SCENARIO("a config_file loads and stores times", "[config][toml_time]")
{
  GIVEN("a file whose source holds times")
  {
    auto store = config_test::memory_transport::store_t{};
    store["app"] = arude::config::as_bytes(std::string{toml_time_test::times_text});

    auto file = arude::config::config_file<toml_time_test::times, config_test::memory_transport>{
      config_test::memory_transport{"app", store}
    };

    WHEN("it is loaded, changed and stored")
    {
      file.load();

      auto value = file.get();
      value.due = std::chrono::year{2027} / 9 / 18;
      file.set(value);
      file.store();

      THEN("a second file over the same source loads the changed times back")
      {
        auto second = arude::config::config_file<toml_time_test::times, config_test::memory_transport>{
          config_test::memory_transport{"app", store}
        };
        second.load();

        REQUIRE(second.get().due == std::chrono::year{2027} / 9 / 18);
        REQUIRE(second.get().created == file.get().created);
      }
    }
  }
}

SCENARIO("a write of a value TOML cannot read back leaves the source untouched", "[config][toml_time]")
{
  GIVEN("a loaded file, whose cached value is then given an unreadable year")
  {
    auto store = config_test::memory_transport::store_t{};
    store["app"] = arude::config::as_bytes(std::string{toml_time_test::times_text});

    auto file = arude::config::config_file<toml_time_test::times, config_test::memory_transport>{
      config_test::memory_transport{"app", store}
    };
    file.load();

    const auto before = store.at("app");

    auto value = file.get();
    value.due = std::chrono::year{12000} / 1 / 1;
    file.set(value);

    WHEN("it is stored")
    {
      const auto failure = toml_time_test::failure_of<arude::config::exception_t>([&file] -> void { file.store(); });

      THEN("it is an invalid_payload, and the source is untouched")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
        REQUIRE(failure.str().contains("year 12000, outside 0 to 9999"));
        REQUIRE(store.at("app") == before);
      }
    }
  }

  GIVEN("a loaded file, whose cached time point is then given an unreadable year")
  {
    auto store = config_test::memory_transport::store_t{};
    store["app"] = arude::config::as_bytes(std::string{toml_time_test::times_text});

    auto file = arude::config::config_file<toml_time_test::times, config_test::memory_transport>{
      config_test::memory_transport{"app", store}
    };
    file.load();

    const auto before = store.at("app");

    auto value = file.get();
    value.created_coarse =
      std::chrono::sys_time<std::chrono::seconds>{std::chrono::sys_days{std::chrono::year{12000} / 1 / 1}};
    file.set(value);

    WHEN("it is stored")
    {
      const auto failure = toml_time_test::failure_of<arude::config::exception_t>([&file] -> void { file.store(); });

      THEN("it is an invalid_payload, and the source is untouched")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
        REQUIRE(failure.str().contains("year 12000, outside 0 to 9999"));
        REQUIRE(store.at("app") == before);
      }
    }
  }

  GIVEN("a cached value whose calendar date is default-constructed, 0/0/0")
  {
    auto store = config_test::memory_transport::store_t{};
    store["app"] = arude::config::as_bytes(std::string{toml_time_test::times_text});

    auto file = arude::config::config_file<toml_time_test::times, config_test::memory_transport>{
      config_test::memory_transport{"app", store}
    };
    file.load();

    const auto before = store.at("app");

    auto value = file.get();
    value.due = std::chrono::year_month_day{};
    file.set(value);

    WHEN("it is stored")
    {
      const auto failure = toml_time_test::failure_of<arude::config::exception_t>([&file] -> void { file.store(); });

      THEN("it is an invalid_payload, and the source is untouched")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
        REQUIRE(failure.str().contains("a date that is not valid"));
        REQUIRE(store.at("app") == before);
      }
    }
  }

  GIVEN("a loaded file, whose cached value is then given a time before midnight")
  {
    auto store = config_test::memory_transport::store_t{};
    store["app"] = arude::config::as_bytes(std::string{toml_time_test::times_text});

    auto file = arude::config::config_file<toml_time_test::times, config_test::memory_transport>{
      config_test::memory_transport{"app", store}
    };
    file.load();

    const auto before = store.at("app");

    auto value = file.get();
    value.at = std::chrono::hh_mm_ss<std::chrono::milliseconds>{std::chrono::hours{-1}};
    file.set(value);

    WHEN("it is stored")
    {
      const auto failure = toml_time_test::failure_of<arude::config::exception_t>([&file] -> void { file.store(); });

      THEN("it is an invalid_payload, and the source is untouched")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
        REQUIRE(failure.str().contains("a time before midnight"));
        REQUIRE(store.at("app") == before);
      }
    }
  }

  GIVEN("a loaded file, whose cached value is then given a time of 24 hours or more")
  {
    auto store = config_test::memory_transport::store_t{};
    store["app"] = arude::config::as_bytes(std::string{toml_time_test::times_text});

    auto file = arude::config::config_file<toml_time_test::times, config_test::memory_transport>{
      config_test::memory_transport{"app", store}
    };
    file.load();

    const auto before = store.at("app");

    auto value = file.get();
    value.at = std::chrono::hh_mm_ss<std::chrono::milliseconds>{std::chrono::hours{25}};
    file.set(value);

    WHEN("it is stored")
    {
      const auto failure = toml_time_test::failure_of<arude::config::exception_t>([&file] -> void { file.store(); });

      THEN("it is an invalid_payload, and the source is untouched")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
        REQUIRE(failure.str().contains("a time of 24 hours or more"));
        REQUIRE(store.at("app") == before);
      }
    }
  }

  GIVEN("a loaded file, one of whose vector elements is then given an unreadable year")
  {
    auto store = config_test::memory_transport::store_t{};
    store["app"] = arude::config::as_bytes(
      std::string{"version = 1\n"
                  "stamps = [2026-01-02T03:04:05Z]\n"
                  "dues = [2026-08-17, 2027-09-18]\n"
                  "revisions = [2026-01-02T01:02:03]\n"
                  "ticks = [07:08:09.5]\n"});

    auto file = arude::config::config_file<toml_time_test::vector_times, config_test::memory_transport>{
      config_test::memory_transport{"app", store}
    };
    file.load();

    const auto before = store.at("app");

    auto value = file.get();
    value.dues.at(1) = std::chrono::year{12000} / 1 / 1;
    file.set(value);

    WHEN("it is stored")
    {
      const auto failure = toml_time_test::failure_of<arude::config::exception_t>([&file] -> void { file.store(); });

      THEN("it is an invalid_payload, and the source is untouched")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
        REQUIRE(failure.str().contains("year 12000, outside 0 to 9999"));
        REQUIRE(store.at("app") == before);
      }
    }
  }
}

SCENARIO("offsets at the edges of the TOML range read, and year edges round-trip", "[config][toml_time]")
{
  GIVEN("text with the widest and the narrowest offset")
  {
    const auto text = std::format(
      "version = 1\ncreated = 2026-01-02T00:00:00+14:00\n{}\n",
      std::string_view{"created_coarse = 2026-01-01T10:00:00-12:00\ncreated_days = 2026-01-02T00:00:00Z\n"
                       "created_minutes = 2026-01-02T01:02:00Z\n"
                       "updated = 2026-01-02T01:02:03\nupdated_ms = 2026-01-02T01:02:03.5\n"
                       "due = 2026-08-17\nat = 07:08:09.5\nat_coarse = 07:08:09\n"});

    THEN("both read in UTC, across the date line")
    {
      const auto value = arude::config::from_toml<toml_time_test::times>(text);

      REQUIRE(
        value.created == std::chrono::sys_time<std::chrono::nanoseconds>{
                           std::chrono::sys_days{std::chrono::year{2026} / 1 / 1} + std::chrono::hours{10}});
      REQUIRE(
        value.created_coarse == std::chrono::sys_time<std::chrono::seconds>{
                                  std::chrono::sys_days{std::chrono::year{2026} / 1 / 1} + std::chrono::hours{22}});
    }
  }

  GIVEN("calendar dates at year 0 and year 9999")
  {
    WHEN("they are rendered and read back")
    {
      const auto zero = arude::config::from_toml<toml_time_test::times>(arude::config::to_toml(
        [] -> toml_time_test::times
        {
          auto value = toml_time_test::times{};
          value.due = std::chrono::year{0} / 1 / 1;
          return value;
        }()));
      const auto last = arude::config::from_toml<toml_time_test::times>(arude::config::to_toml(
        [] -> toml_time_test::times
        {
          auto value = toml_time_test::times{};
          value.due = std::chrono::year{9999} / 12 / 31;
          return value;
        }()));

      THEN("they survive, the edges included")
      {
        REQUIRE(zero.due == std::chrono::year{0} / 1 / 1);
        REQUIRE(last.due == std::chrono::year{9999} / 12 / 31);
      }
    }
  }
}

SCENARIO("a nanosecond fraction reads and writes at full precision", "[config][toml_time]")
{
  GIVEN("text holding 123456789 nanoseconds past the second")
  {
    const auto text = std::format(
      "version = 1\ncreated = 2026-01-02T01:02:03.123456789Z\n{}\n",
      std::string_view{"created_coarse = 2026-01-02T01:02:03Z\ncreated_days = 2026-01-02T00:00:00Z\n"
                       "created_minutes = 2026-01-02T01:02:00Z\n"
                       "updated = 2026-01-02T01:02:03\nupdated_ms = 2026-01-02T01:02:03.5\n"
                       "due = 2026-08-17\nat = 07:08:09.5\nat_coarse = 07:08:09\n"});

    WHEN("it is read and written back")
    {
      const auto value = arude::config::from_toml<toml_time_test::times>(text);
      const auto rendered = arude::config::to_toml(value);

      THEN("the fraction survives, to the nanosecond")
      {
        REQUIRE(
          value.created == std::chrono::sys_time<std::chrono::nanoseconds>{
                             std::chrono::sys_days{std::chrono::year{2026} / 1 / 2} +
                             std::chrono::hours{1} +
                             std::chrono::minutes{2} +
                             std::chrono::seconds{3} +
                             std::chrono::nanoseconds{123456789}});
        REQUIRE(rendered.contains("created = 2026-01-02T01:02:03.123456789Z"));
      }
    }
  }
}

SCENARIO("whole-second and decimal periods read, write and refuse exactly", "[config][toml_time]")
{
  GIVEN("text holding the widest accepted periods, exactly")
  {
    constexpr auto text = std::string_view{"version = 1\n"
                                           "week = 2026-01-01T00:00:00Z\n"
                                           "at_minutes = 07:08:00\n"
                                           "at_hours = 07:00:00\n"
                                           "at_days = 00:00:00\n"
                                           "in_centiseconds = 2026-01-01T00:00:00.25Z\n"
                                           "narrow = 2026-01-01T00:00:01Z\n"};

    WHEN("it is read, written and read back")
    {
      const auto value = arude::config::from_toml<toml_time_test::coarse_times>(text);
      const auto rendered = arude::config::to_toml(value);
      const auto back = arude::config::from_toml<toml_time_test::coarse_times>(rendered);

      THEN("every member holds its value exactly, and round-trips")
      {
        REQUIRE(value.week == std::chrono::sys_time<std::chrono::weeks>{std::chrono::weeks{2922}});
        REQUIRE(value.at_minutes.to_duration() == std::chrono::hours{7} + std::chrono::minutes{8});
        REQUIRE(value.at_hours.to_duration() == std::chrono::hours{7});
        REQUIRE(value.at_days.to_duration() == std::chrono::days{0});
        REQUIRE(
          value.in_centiseconds.time_since_epoch().count() ==
          std::chrono::duration_cast<std::chrono::duration<std::int64_t, std::ratio<1, 100>>>(
            std::chrono::sys_days{std::chrono::year{2026} / 1 / 1}.time_since_epoch())
              .count() +
            25);
        REQUIRE(value.narrow.time_since_epoch().count() == (20454 * 86400) + 1);
        REQUIRE(back.week == value.week);
        REQUIRE(back.at_minutes.to_duration() == value.at_minutes.to_duration());
        REQUIRE(back.at_hours.to_duration() == value.at_hours.to_duration());
        REQUIRE(back.at_days.to_duration() == value.at_days.to_duration());
        REQUIRE(back.in_centiseconds == value.in_centiseconds);
        REQUIRE(back.narrow == value.narrow);
        REQUIRE(rendered.contains("week = 2026-01-01T00:00:00Z"));
        REQUIRE(rendered.contains("at_minutes = 07:08:00"));
        REQUIRE(rendered.contains("in_centiseconds = 2026-01-01T00:00:00.25Z"));
      }
    }
  }

  GIVEN("text whose minute-precision time of day holds a second")
  {
    const auto text = std::format(
      "version = 1\nat_minutes = 07:08:30\n{}\n",
      std::string_view{"week = 2026-01-01T00:00:00Z\nat_hours = 07:00:00\nat_days = 00:00:00\n"
                       "in_centiseconds = 2026-01-01T00:00:00.25Z\nnarrow = 2026-01-01T00:00:01Z\n"});

    THEN("it is refused, as the seconds do not divide a minute")
    {
      const auto failure = toml_time_test::failure_of<arude::config::exception_t>(
        [&text] -> void { std::ignore = arude::config::from_toml<toml_time_test::coarse_times>(text); });

      REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
      REQUIRE(failure.str().contains("07:08:30 cannot be represented as minutes exactly."));
    }
  }

  GIVEN("text whose centisecond member holds a fraction finer than it")
  {
    const auto text = std::format(
      "version = 1\nin_centiseconds = 2026-01-01T00:00:00.005Z\n{}\n",
      std::string_view{"week = 2026-01-01T00:00:00Z\nat_minutes = 07:08:00\nat_hours = 07:00:00\n"
                       "at_days = 00:00:00\nnarrow = 2026-01-01T00:00:01Z\n"});

    THEN("it is refused, naming the period independently of the library")
    {
      const auto failure = toml_time_test::failure_of<arude::config::exception_t>(
        [&text] -> void { std::ignore = arude::config::from_toml<toml_time_test::coarse_times>(text); });

      REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
      REQUIRE(failure.str().contains("2026-01-01T00:00:00.005+00:00 is finer than units of 1/100 s holds."));
    }
  }

  GIVEN("text whose 32-bit member holds year 2100")
  {
    const auto text = std::format(
      "version = 1\nnarrow = 2100-01-01T00:00:00Z\n{}\n",
      std::string_view{"week = 2026-01-01T00:00:00Z\nat_minutes = 07:08:00\nat_hours = 07:00:00\n"
                       "at_days = 00:00:00\nin_centiseconds = 2026-01-01T00:00:00.25Z\n"});

    THEN("it is refused rather than wrapped into a wrong year")
    {
      const auto failure = toml_time_test::failure_of<arude::config::exception_t>(
        [&text] -> void { std::ignore = arude::config::from_toml<toml_time_test::coarse_times>(text); });

      REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
      REQUIRE(failure.str().contains("2100-01-01T00:00:00+00:00 is outside the range of seconds."));
    }
  }

  GIVEN("a nanosecond time point at its minimum")
  {
    const auto minimum = std::chrono::sys_time<std::chrono::nanoseconds>::min();

    WHEN("it is written and read back")
    {
      auto value = toml_time_test::times{};
      value.created = minimum;
      value.due = std::chrono::year{2026} / 8 / 17;

      const auto rendered = arude::config::to_toml(value);
      const auto back = arude::config::from_toml<toml_time_test::times>(rendered);

      THEN("it survives, to the nanosecond, without overflowing")
      {
        REQUIRE(rendered.contains("created = 1677-09-21T00:12:43.145224192Z"));
        REQUIRE(back.created == minimum);
      }
    }
  }

  GIVEN("an offset date-time whose UTC year leaves 0 to 9999, in a second-precision member that holds it")
  {
    constexpr auto before =
      std::string_view{"version = 1\nstamps = [0000-01-01T00:00:00+01:00]\ndues = []\nrevisions = []\nticks = []\n"};
    constexpr auto after =
      std::string_view{"version = 1\nstamps = [9999-12-31T23:30:00-01:00]\ndues = []\nrevisions = []\nticks = []\n"};

    THEN("both are refused on read, so what loads can always be stored")
    {
      const auto first = toml_time_test::failure_of<arude::config::exception_t>(
        [&before] -> void { std::ignore = arude::config::from_toml<toml_time_test::vector_times>(before); });
      const auto second = toml_time_test::failure_of<arude::config::exception_t>(
        [&after] -> void { std::ignore = arude::config::from_toml<toml_time_test::vector_times>(after); });

      REQUIRE(first.data() == arude::config::config_error::invalid_payload);
      REQUIRE(first.str().contains("0000-01-01T00:00:00+01:00 is outside the years 0 to 9999 as UTC."));
      REQUIRE(second.data() == arude::config::config_error::invalid_payload);
      REQUIRE(second.str().contains("9999-12-31T23:30:00-01:00 is outside the years 0 to 9999 as UTC."));
    }
  }
}

SCENARIO("a 32-bit minute representation, as MSVC's, is range-checked and never wraps", "[config][toml_time]")
{
  GIVEN("text holding values the 32-bit minutes hold")
  {
    constexpr auto text = std::string_view{"version = 1\n"
                                           "stamp = 4000-01-01T00:00:00Z\n"
                                           "local = 0001-01-01T00:00:00\n"
                                           "at = 23:59:00\n"};

    WHEN("it is read, written and read back")
    {
      const auto value = arude::config::from_toml<toml_time_test::narrow_times>(text);
      const auto rendered = arude::config::to_toml(value);
      const auto back = arude::config::from_toml<toml_time_test::narrow_times>(rendered);

      THEN("each holds its value, and round-trips")
      {
        REQUIRE(
          value.stamp.time_since_epoch() == std::chrono::sys_days{std::chrono::year{4000} / 1 / 1}.time_since_epoch());
        REQUIRE(
          value.local.time_since_epoch() == std::chrono::local_days{std::chrono::year{1} / 1 / 1}.time_since_epoch());
        REQUIRE(value.at.to_duration() == std::chrono::hours{23} + std::chrono::minutes{59});
        REQUIRE(rendered.contains("stamp = 4000-01-01T00:00:00Z"));
        REQUIRE(rendered.contains("local = 0001-01-01T00:00:00"));
        REQUIRE(rendered.contains("at = 23:59:00"));
        REQUIRE(back.stamp == value.stamp);
        REQUIRE(back.local == value.local);
        REQUIRE(back.at.to_duration() == value.at.to_duration());
      }
    }
  }

  GIVEN("text holding a year beyond what 32-bit minutes hold")
  {
    constexpr auto text = std::string_view{"version = 1\n"
                                           "stamp = 6100-01-01T00:00:00Z\n"
                                           "local = 2026-01-01T00:00:00\n"
                                           "at = 23:59:00\n"};

    THEN("it is refused rather than wrapped into a wrong year")
    {
      const auto failure = toml_time_test::failure_of<arude::config::exception_t>(
        [&text] -> void { std::ignore = arude::config::from_toml<toml_time_test::narrow_times>(text); });

      REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
      REQUIRE(failure.str().contains("6100-01-01T00:00:00+00:00 is outside the range of minutes."));
    }
  }

  GIVEN("the largest value 32-bit minutes hold, and a large negative one")
  {
    auto value = toml_time_test::narrow_times{};
    value.stamp = std::chrono::sys_time<std::chrono::duration<std::int32_t, std::ratio<60>>>::max();
    value.local = std::chrono::local_time<std::chrono::duration<std::int32_t, std::ratio<60>>>{
      std::chrono::duration<std::int32_t, std::ratio<60>>{-1000000000}};

    WHEN("it is written and read back")
    {
      const auto rendered = arude::config::to_toml(value);
      const auto back = arude::config::from_toml<toml_time_test::narrow_times>(rendered);

      THEN("it survives, without wrapping")
      {
        REQUIRE(rendered.contains("stamp = 6053-01-23T02:07:00Z"));
        REQUIRE(rendered.contains("local = 0068-09-03T13:20:00"));
        REQUIRE(back.stamp == value.stamp);
        REQUIRE(back.local == value.local);
      }
    }
  }
}

SCENARIO("a period spelled unreduced is the period it reduces to", "[config][toml_time]")
{
  GIVEN("a time point in tenths of a second, spelled std::ratio<10, 100>")
  {
    constexpr auto text = std::string_view{"version = 1\nat = 2026-01-01T00:00:00.5Z\n"};

    WHEN("it is read, written and read back")
    {
      const auto value = arude::config::from_toml<toml_time_test::unreduced_times>(text);
      const auto rendered = arude::config::to_toml(value);
      const auto back = arude::config::from_toml<toml_time_test::unreduced_times>(rendered);

      THEN("it holds the value exactly, and round-trips as a native date-time")
      {
        const auto expected = std::chrono::sys_days{std::chrono::year{2026} / 1 / 1} + std::chrono::milliseconds{500};
        REQUIRE(value.at == expected);
        REQUIRE(back.at == value.at);
        REQUIRE(rendered.contains("at = 2026-01-01T00:00:00.5Z"));
      }
    }
  }
}

SCENARIO("a time of day far beyond 24 hours is refused before it can overflow", "[config][toml_time]")
{
  GIVEN("a minute-precision time of day holding the largest number of minutes")
  {
    auto value = toml_time_test::minute_of_day{};
    value.at = std::chrono::hh_mm_ss<std::chrono::minutes>{std::chrono::minutes::max()};

    THEN("writing it is an invalid_payload naming 24 hours or more, not a time before midnight")
    {
      const auto failure = toml_time_test::failure_of<arude::config::exception_t>(
        [&value] -> void { std::ignore = arude::config::to_toml(value); });

      REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
      REQUIRE(failure.str().contains("a time of 24 hours or more"));
    }
  }
}
