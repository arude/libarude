///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///

#include "arude/config/toml_time.hpp"

#include <toml++/toml.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <expected>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace arude::config::detail
{

///
///
auto temporal_of(const toml::node& node) -> std::expected<temporal_t, std::string>
{
  if(const auto* const date = node.as_date())
  {
    return temporal_t{**date};
  }

  if(const auto* const time = node.as_time())
  {
    return temporal_t{**time};
  }

  if(const auto* const date_time = node.as_date_time())
  {
    return temporal_t{**date_time};
  }

  if(const auto* const text = node.as_string())
  {
    const auto value = text->get();

    // A quoted value is parsed only where it consists of the characters
    // TOML's own date and time syntax uses, so a newline, a comment or an
    // injection is refused before toml++ sees it.
    const auto allowed = [](const char chr) -> bool
    {
      return (chr >= '0' && chr <= '9') ||
             chr == '-' ||
             chr == ':' ||
             chr == '.' ||
             chr == '+' ||
             chr == 'T' ||
             chr == 't' ||
             chr == 'Z' ||
             chr == 'z' ||
             chr == ' ';
    };

    if(value.empty() || !std::ranges::all_of(value, allowed))
    {
      return std::unexpected{std::string{"the text is not a date or a time."}};
    }

    try
    {
      auto document = toml::parse(std::string{"v = "} + value);

      if(const auto* const parsed = document.get("v"); parsed != nullptr)
      {
        if(const auto* const date = parsed->as_date())
        {
          return temporal_t{**date};
        }

        if(const auto* const time = parsed->as_time())
        {
          return temporal_t{**time};
        }

        if(const auto* const date_time = parsed->as_date_time())
        {
          return temporal_t{**date_time};
        }
      }

      return std::unexpected{std::string{"the text is not a date or a time."}};
    }
    catch(const toml::parse_error& ex)
    {
      // The characters were all TOML's, but the text is not a date or a
      // time, so toml++'s own reason is kept in the message.
      return std::unexpected{std::format("the text is not a date or a time: {}", ex.description())};
    }
    catch(...)
    {
      return std::unexpected{std::string{"the text is not a date or a time."}};
    }
  }

  return std::unexpected{std::string{"the value is not a date or a time."}};
}

///
///
auto temporal_mismatch(const temporal_t& found, const std::string_view fitting) -> std::string
{
  return std::format("the value is {}, which fits {}.", spell(found), fitting);
}

///
///
auto fitting_of(const temporal_t& found) -> std::string_view
{
  if(const auto* const date_time = std::get_if<toml::date_time>(&found); date_time != nullptr)
  {
    if(date_time->offset.has_value())
    {
      return "a std::chrono::sys_time";
    }

    return "a std::chrono::local_time";
  }

  if(std::holds_alternative<toml::date>(found))
  {
    return "a std::chrono::year_month_day";
  }

  return "a std::chrono::hh_mm_ss";
}

///
///
auto calendar_date_of(const toml::date& date) -> std::expected<std::chrono::year_month_day, std::string>
{
  const auto calendar = std::chrono::year_month_day{
    std::chrono::year{date.year}, std::chrono::month{date.month}, std::chrono::day{date.day}};

  if(!calendar.ok())
  {
    return std::unexpected{
      std::format("{:04}-{:02}-{:02} is not a valid calendar date.", date.year, date.month, date.day)};
  }

  return calendar;
}

///
///
auto toml_date_of(const std::chrono::year_month_day date) -> std::expected<toml::date, std::string>
{
  if(!date.ok())
  {
    return std::unexpected{std::string{"a date that is not valid"}};
  }

  const auto year = static_cast<int>(date.year());

  if(year < 0 || year > 9999)
  {
    return std::unexpected{std::format("year {}, outside 0 to 9999", year)};
  }

  return toml::date{
    static_cast<std::uint16_t>(year), static_cast<unsigned>(date.month()), static_cast<unsigned>(date.day())};
}

///
///
auto civil_of(const toml::date& date, const toml::time& time)
  -> std::expected<std::pair<std::chrono::seconds, std::chrono::nanoseconds>, std::string>
{
  const auto calendar = calendar_date_of(date);

  if(!calendar.has_value())
  {
    return std::unexpected{calendar.error()};
  }

  // The days are converted to int64 seconds before the hours, minutes and
  // seconds join, so the sum is never computed in the representation of a
  // mixed unit: on MSVC, days, hours and minutes have an int representation
  // and the intermediate minutes of year 9999 would wrap.
  const auto seconds = std::chrono::seconds{std::chrono::sys_days{*calendar}.time_since_epoch()} +
                       std::chrono::seconds{
                         (static_cast<std::int64_t>(time.hour) * 3600) +
                         (static_cast<std::int64_t>(time.minute) * 60) +
                         static_cast<std::int64_t>(time.second)};

  return std::pair{seconds, std::chrono::nanoseconds{time.nanosecond}};
}

///
///
auto year_month_day_of(const temporal_t& temporal) -> std::expected<std::chrono::year_month_day, std::string>
{
  if(const auto* const date = std::get_if<toml::date>(&temporal))
  {
    return calendar_date_of(*date);
  }

  return std::unexpected{temporal_mismatch(temporal, fitting_of(temporal))};
}

namespace
{

///
///
auto spell_time(const toml::time& time) -> std::string
{
  auto text = std::format("{:02}:{:02}:{:02}", time.hour, time.minute, time.second);

  if(time.nanosecond != 0)
  {
    auto fraction = std::format("{:09}", time.nanosecond);

    while(fraction.back() == '0')
    {
      fraction.pop_back();
    }

    text += "." + fraction;
  }

  return text;
}

} // namespace

///
///
auto spell(const temporal_t& temporal) -> std::string
{
  if(const auto* const date = std::get_if<toml::date>(&temporal))
  {
    return std::format("{:04}-{:02}-{:02}", date->year, date->month, date->day);
  }

  if(const auto* const time = std::get_if<toml::time>(&temporal))
  {
    return spell_time(*time);
  }

  const auto& date_time = std::get<toml::date_time>(temporal);
  auto text = std::format("{:04}-{:02}-{:02}T", date_time.date.year, date_time.date.month, date_time.date.day) +
              spell_time(date_time.time);

  if(date_time.offset.has_value())
  {
    const auto minutes = date_time.offset->minutes;
    const auto sign = minutes < 0 ? '-' : '+';
    const auto magnitude = minutes < 0 ? -minutes : minutes;

    text += std::format("{}{:02}:{:02}", sign, magnitude / 60, magnitude % 60);
  }

  return text;
}

} // namespace arude::config::detail
