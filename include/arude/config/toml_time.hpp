///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// The date and time types of a configuration, through Parser
/// specialisations on libarude's reader and writer: std::chrono::sys_time<D>
/// as a TOML offset date-time (read with any offset, written in UTC with Z),
/// std::chrono::local_time<D> as a TOML local date-time,
/// std::chrono::year_month_day as a TOML local date, and
/// std::chrono::hh_mm_ss<D> as a TOML local time.
///
/// Each reads the native TOML value and also accepts a quoted string in
/// TOML's own date and time syntax, parsed with toml++ itself so the grammar
/// is exactly TOML's, and always writes the native value, never a string.
/// A value the type cannot hold exactly is refused as invalid_payload rather
/// than truncated, and a value that cannot be written as a native TOML value
/// is refused through the write context, so the file is left untouched. An
/// offset date-time whose UTC value leaves the years 0 to 9999 is refused on
/// read, so what a file loads can always be stored back.
///
/// TOML holds a time to the nanosecond, so D must have a signed integral
/// representation and a period of whole seconds or an exact decimal fraction
/// of a second (num == 1, den a power of ten up to 1'000'000'000). Months and
/// years, whose periods are calendar averages, and every other period are a
/// compile error with a clear message. Refusal messages name D by
/// its period ("milliseconds", "seconds", "units of 1/100 s"), never by how
/// a standard library spells the type. toml++ truncates a fraction beyond
/// nine digits silently when it parses, before this reader sees it, so a
/// value like .1234567891 reads as 123456789 ns. toml++ rejects a leap
/// second (:60) and an invalid date when it parses, so natively they are
/// invalid_payload as "not valid TOML"; quoted, the reader's own parse of
/// the string refuses them, with toml++'s reason.
///
#pragma once

#include "arude/config/toml_reader.hpp"
#include "arude/config/toml_writer.hpp"

#include <rfl/parsing/Parent.hpp>
#include <rfl/parsing/Parser_base.hpp>
#include <rfl/parsing/schema/Type.hpp>
#include <rfl/Result.hpp>
#include <toml++/toml.hpp>

#include <chrono>
#include <concepts>
#include <cstdint>
#include <expected>
#include <format>
#include <limits>
#include <map>
#include <optional>
#include <ratio>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace arude::config::detail
{

///
/// Whether a type is a std::chrono::duration of any representation and
/// period, the period as spelled: std::ratio<10, 100> matches as well as
/// std::ratio<1, 10>. The gate that selects the date and time Parser
/// specialisations. Whether TOML holds a value of D exactly is checked by
/// toml_duration_supported with a message of its own, so a duration TOML
/// cannot hold is refused where it is used, not inside a constraint.
///
/// \tparam D Duration type to test.
///
template<typename D>
struct is_duration : std::false_type
{};

///
/// \see is_duration
///
/// \tparam Rep The duration's representation.
/// \tparam Period The duration's period, as spelled, reduced or not.
///
template<typename Rep, typename Period>
struct is_duration<std::chrono::duration<Rep, Period>> : std::true_type
{};

///
/// \see is_duration
///
/// \tparam D Duration type to test.
///
template<typename D>
concept toml_duration_c = is_duration<D>::value;

///
/// Checks at compile time that TOML holds a value of D exactly, with a
/// message that names the requirement.
///
/// \tparam D Duration type to check.
/// \return True.
///
template<typename D>
consteval auto toml_duration_supported() -> bool
{
  static_assert(
    std::signed_integral<typename D::rep>,
    "a date or time member needs a duration with a signed integral representation");
  constexpr auto num = static_cast<std::int64_t>(D::period::num);
  constexpr auto den = static_cast<std::int64_t>(D::period::den);
  constexpr auto is_power_of_ten = [](const std::int64_t n) -> bool
  {
    auto value = n;

    while(value % 10 == 0 && value != 1)
    {
      value /= 10;
    }

    return value == 1;
  };
  static_assert(
    !std::ratio_equal_v<typename D::period, std::chrono::months::period> &&
      !std::ratio_equal_v<typename D::period, std::chrono::years::period>,
    "a date or time member cannot use months or years, whose periods are calendar averages rather than exact units");
  constexpr auto whole_seconds = num >= 1 && den == 1;
  constexpr auto decimal_fraction = num == 1 && den >= 10 && den <= 1000000000 && is_power_of_ten(den);
  static_assert(
    whole_seconds || decimal_fraction,
    "a date or time member needs a duration whose period is whole seconds or an exact decimal fraction of a second");
  return true;
}

///
/// The name of D's period, spelled independently of any standard library:
/// "nanoseconds", "milliseconds", "seconds", "minutes", "hours", "days" and
/// "weeks" for the named periods, and "units of 1/100 s" for the rest.
///
/// \tparam D Duration type to name.
/// \return The name.
///
template<toml_duration_c D>
[[nodiscard]] auto period_name() -> std::string
{
  constexpr auto num = static_cast<std::int64_t>(D::period::num);
  constexpr auto den = static_cast<std::int64_t>(D::period::den);

  if constexpr(num == 1 && den == 1)
  {
    return "seconds";
  }
  if constexpr(num == 60 && den == 1)
  {
    return "minutes";
  }
  if constexpr(num == 3600 && den == 1)
  {
    return "hours";
  }
  if constexpr(num == 86400 && den == 1)
  {
    return "days";
  }
  if constexpr(num == 604800 && den == 1)
  {
    return "weeks";
  }
  if constexpr(num == 1 && den == 1000)
  {
    return "milliseconds";
  }
  if constexpr(num == 1 && den == 1000000)
  {
    return "microseconds";
  }
  if constexpr(num == 1 && den == 1000000000)
  {
    return "nanoseconds";
  }
  if constexpr(den == 1)
  {
    return std::format("units of {} s", num);
  }

  return std::format("units of {}/{} s", num, den);
}

///
/// A TOML date or time, whichever kind a node holds.
///
using temporal_t = std::variant<toml::date, toml::time, toml::date_time>;

///
/// The date or time a node holds, or the one a quoted string holds in TOML's
/// own syntax. A string is refused before parsing unless it consists of the
/// characters TOML's date and time syntax uses, and it is parsed with
/// toml++ itself, so the grammar is exactly TOML's and an invalid date or a
/// leap second is refused with toml++'s own reason.
///
/// \param node The node. Not retained.
/// \return The date or time, or why the node holds none.
///
[[nodiscard]] auto temporal_of(const toml::node& node) -> std::expected<temporal_t, std::string>;

///
/// The message for a value of one kind read as another: it names the kind
/// found and the type that fits it.
///
/// \param found The value, whose kind is named.
/// \param fitting The type that fits the value.
/// \return The message.
///
[[nodiscard]] auto temporal_mismatch(const temporal_t& found, std::string_view fitting) -> std::string;

///
/// The type a TOML date or time fits, named as an error message names it.
///
/// \param found The value.
/// \return The fitting type.
///
[[nodiscard]] auto fitting_of(const temporal_t& found) -> std::string_view;

///
/// The calendar date a TOML date names, or why it names none.
///
/// \param date The date. Not retained.
/// \return The calendar date, which is checked to be valid again.
///
[[nodiscard]] auto calendar_date_of(const toml::date& date) -> std::expected<std::chrono::year_month_day, std::string>;

///
/// The TOML date a calendar date names, or why it names none: the year must
/// sit in 0 to 9999, which is what TOML writes and reads back.
///
/// \param date The calendar date. Not retained.
/// \return The TOML date, or the reason it cannot be one.
///
[[nodiscard]] auto toml_date_of(std::chrono::year_month_day date) -> std::expected<toml::date, std::string>;

///
/// The seconds and nanosecond fraction a TOML date and time name, counted
/// from the epoch. The sum is computed in int64 seconds, never in the
/// representation of any intermediate duration.
///
/// \param date The date. Not retained.
/// \param time The time. Not retained.
/// \return The seconds and the fraction, or why the date is not valid.
///
[[nodiscard]] auto civil_of(const toml::date& date, const toml::time& time)
  -> std::expected<std::pair<std::chrono::seconds, std::chrono::nanoseconds>, std::string>;

///
/// The value spelled as TOML spells it, for messages.
///
/// \param temporal The value.
/// \return The spelling.
///
[[nodiscard]] auto spell(const temporal_t& temporal) -> std::string;

///
/// The value a TOML date or time names, as a duration since the epoch or
/// since midnight, or why it cannot be one: a fraction finer than D holds,
/// or a value outside D's range, which is checked against D's representation
/// so nothing wraps.
///
/// \tparam D The duration type to hold the value.
/// \param value The value, spelled, for the message.
/// \param whole The whole seconds.
/// \param fraction The nanosecond fraction, which is never negative.
/// \return The duration, or the reason it does not fit.
///
template<toml_duration_c D>
[[nodiscard]] auto duration_of(std::string_view value, std::chrono::seconds whole, std::chrono::nanoseconds fraction)
  -> std::expected<D, std::string>;

///
/// The TOML time a duration since midnight names, or why it names none: a
/// time before midnight or of 24 hours or more cannot be written.
///
/// \tparam D The duration type.
/// \param since_midnight The duration, in [0, one day).
/// \return The TOML time, or the reason it is not one.
///
template<toml_duration_c D>
[[nodiscard]] auto toml_time_of(D since_midnight) -> std::expected<toml::time, std::string>;

///
/// The sys_time a TOML date or time names, or why it does not: an offset
/// date-time is converted to UTC, a UTC result outside the years 0 to 9999
/// is refused, and any other kind fits another type.
///
/// \tparam D The duration type.
/// \param temporal The value.
/// \return The time point, or the reason the value does not fit.
///
template<toml_duration_c D>
[[nodiscard]] auto sys_time_of(const temporal_t& temporal) -> std::expected<std::chrono::sys_time<D>, std::string>;

///
/// The local_time a TOML date or time names, or why it does not: a local
/// date-time is what fits, and any other kind fits another type.
///
/// \tparam D The duration type.
/// \param temporal The value.
/// \return The time point, or the reason the value does not fit.
///
template<toml_duration_c D>
[[nodiscard]] auto local_time_of(const temporal_t& temporal) -> std::expected<std::chrono::local_time<D>, std::string>;

///
/// The year_month_day a TOML date or time names, or why it does not.
///
/// \param temporal The value.
/// \return The calendar date, or the reason the value does not fit.
///
[[nodiscard]] auto year_month_day_of(const temporal_t& temporal)
  -> std::expected<std::chrono::year_month_day, std::string>;

///
/// The hh_mm_ss a TOML date or time names, or why it does not: a local time
/// is what fits, and any other kind fits another type.
///
/// \tparam D The duration type.
/// \param temporal The value.
/// \return The time of day, or the reason the value does not fit.
///
template<toml_duration_c D>
[[nodiscard]] auto hh_mm_ss_of(const temporal_t& temporal) -> std::expected<std::chrono::hh_mm_ss<D>, std::string>;

///
/// The product a * b, or none where the product overflows int64. b is
/// non-negative.
///
/// \param a One factor, of either sign.
/// \param b The other factor, never negative.
/// \return The product, or none.
///
[[nodiscard]] constexpr auto scaled(std::int64_t a, std::int64_t b) -> std::optional<std::int64_t>;

///
///
constexpr auto scaled(const std::int64_t a, const std::int64_t b) -> std::optional<std::int64_t>
{
  if(a == 0 || b == 0)
  {
    return 0;
  }

  if(a > 0)
  {
    if(b > std::numeric_limits<std::int64_t>::max() / a)
    {
      return std::nullopt;
    }
  }
  else if(a == std::numeric_limits<std::int64_t>::min())
  {
    if(b > 1)
    {
      return std::nullopt;
    }
  }
  else if(b > std::numeric_limits<std::int64_t>::max() / -a)
  {
    return std::nullopt;
  }

  return a * b;
}

///
/// The ticks of a whole-second period, or why the value does not fit: the
/// seconds must divide the period and the fraction must be zero, and then
/// no multiplication can overflow.
///
/// \tparam D The duration type, of whole seconds.
/// \param value The value, spelled, for the message.
/// \param whole The whole seconds.
/// \param fraction The nanosecond fraction, which is never negative.
/// \return The ticks, in int64, or the reason the value does not fit.
///
template<toml_duration_c D>
[[nodiscard]] auto whole_second_ticks(
  const std::string_view value, const std::chrono::seconds whole, const std::chrono::nanoseconds fraction)
  -> std::expected<std::int64_t, std::string>
{
  constexpr auto num = static_cast<std::int64_t>(D::period::num);

  if(whole.count() % num != 0)
  {
    return std::unexpected{std::format("{} cannot be represented as {} exactly.", value, period_name<D>())};
  }

  if(fraction.count() != 0)
  {
    return std::unexpected{std::format("{} is finer than {} holds.", value, period_name<D>())};
  }

  return whole.count() / num;
}

///
/// The ticks of an exact decimal fraction of a second, or why the value
/// does not fit: the whole seconds and the fraction are combined before the
/// last multiply, and the range is checked against the result, so a value
/// at the very edge of int64, whose product alone would overflow, is not
/// refused.
///
/// \tparam D The duration type, an exact decimal fraction of a second.
/// \param value The value, spelled, for the message.
/// \param whole The whole seconds.
/// \param fraction The nanosecond fraction, which is never negative.
/// \return The ticks, in int64, or the reason the value does not fit.
///
template<toml_duration_c D>
[[nodiscard]] auto decimal_fraction_ticks(
  const std::string_view value, const std::chrono::seconds whole, const std::chrono::nanoseconds fraction)
  -> std::expected<std::int64_t, std::string>
{
  constexpr auto den = static_cast<std::int64_t>(D::period::den);
  const auto fraction_scaled = scaled(fraction.count(), den);

  if(!fraction_scaled.has_value() || *fraction_scaled % 1000000000 != 0)
  {
    return std::unexpected{std::format("{} is finer than {} holds.", value, period_name<D>())};
  }

  const auto fraction_part = *fraction_scaled / 1000000000;

  if(whole.count() >= 0)
  {
    if(whole.count() > (std::numeric_limits<std::int64_t>::max() - fraction_part) / den)
    {
      return std::unexpected{std::format("{} is outside the range of {}.", value, period_name<D>())};
    }

    return (whole.count() * den) + fraction_part;
  }

  if(whole.count() == std::numeric_limits<std::int64_t>::min())
  {
    return std::unexpected{std::format("{} is outside the range of {}.", value, period_name<D>())};
  }

  const auto magnitude = static_cast<std::uint64_t>(-whole.count());
  const auto limit = (static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) +
                      1 +
                      static_cast<std::uint64_t>(fraction_part)) /
                     static_cast<std::uint64_t>(den);

  if(magnitude > limit)
  {
    return std::unexpected{std::format("{} is outside the range of {}.", value, period_name<D>())};
  }

  const auto difference = (magnitude * static_cast<std::uint64_t>(den)) - static_cast<std::uint64_t>(fraction_part);

  if(difference <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
  {
    return -static_cast<std::int64_t>(difference);
  }

  return std::numeric_limits<std::int64_t>::min();
}

///
///
template<toml_duration_c D>
auto duration_of(
  const std::string_view value, const std::chrono::seconds whole, const std::chrono::nanoseconds fraction)
  -> std::expected<D, std::string>
{
  static_assert(toml_duration_supported<D>());

  constexpr auto den = static_cast<std::int64_t>(D::period::den);
  const auto ticks = [&]() -> std::expected<std::int64_t, std::string>
  {
    if constexpr(den == 1)
    {
      return whole_second_ticks<D>(value, whole, fraction);
    }

    return decimal_fraction_ticks<D>(value, whole, fraction);
  }();

  if(!ticks.has_value())
  {
    return std::unexpected{ticks.error()};
  }

  if(!std::in_range<typename D::rep>(*ticks))
  {
    return std::unexpected{std::format("{} is outside the range of {}.", value, period_name<D>())};
  }

  return D{static_cast<typename D::rep>(*ticks)};
}

///
///
template<toml_duration_c D>
auto toml_time_of(const D since_midnight) -> std::expected<toml::time, std::string>
{
  static_assert(toml_duration_supported<D>());

  if(since_midnight < D{0})
  {
    return std::unexpected{"a time before midnight"};
  }

  if(since_midnight >= std::chrono::days{1})
  {
    return std::unexpected{"a time of 24 hours or more"};
  }

  // Every accepted period is a whole number of nanoseconds, so the
  // conversion is exact, and a value below one day is below 8.64e13
  // nanoseconds, so it cannot overflow.
  const auto since_midnight_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(since_midnight);
  const auto split = std::chrono::hh_mm_ss<std::chrono::nanoseconds>{since_midnight_ns};

  return toml::time{
    static_cast<std::uint8_t>(split.hours().count()),
    static_cast<std::uint8_t>(split.minutes().count()),
    static_cast<std::uint8_t>(split.seconds().count()),
    static_cast<std::uint32_t>(split.subseconds().count())};
}

///
///
template<toml_duration_c D>
auto sys_time_of(const temporal_t& temporal) -> std::expected<std::chrono::sys_time<D>, std::string>
{
  if(const auto* const date_time = std::get_if<toml::date_time>(&temporal))
  {
    if(!date_time->offset.has_value())
    {
      return std::unexpected{temporal_mismatch(temporal, fitting_of(temporal))};
    }

    const auto civil = civil_of(date_time->date, date_time->time);

    if(!civil.has_value())
    {
      return std::unexpected{civil.error()};
    }

    // Converted to UTC by taking the offset away; the offset is whole
    // minutes, so it converts exactly where D holds it.
    const auto utc = civil->first - std::chrono::minutes{date_time->offset->minutes};
    const auto spelled = spell(temporal);

    // What is read must be storable: the writer refuses a UTC year outside
    // 0 to 9999, so the reader refuses it here rather than hand a value
    // that cannot be written back.
    const auto utc_days = std::chrono::floor<std::chrono::days>(utc);
    const auto utc_year = static_cast<int>(std::chrono::year_month_day{std::chrono::sys_days{utc_days}}.year());

    if(utc_year < 0 || utc_year > 9999)
    {
      return std::unexpected{std::format("{} is outside the years 0 to 9999 as UTC.", spelled)};
    }

    return duration_of<D>(spelled, utc, civil->second)
      .transform([](const D duration) -> std::chrono::sys_time<D> { return std::chrono::sys_time<D>{duration}; });
  }

  return std::unexpected{temporal_mismatch(temporal, fitting_of(temporal))};
}

///
///
template<toml_duration_c D>
auto local_time_of(const temporal_t& temporal) -> std::expected<std::chrono::local_time<D>, std::string>
{
  if(const auto* const date_time = std::get_if<toml::date_time>(&temporal))
  {
    if(date_time->offset.has_value())
    {
      return std::unexpected{temporal_mismatch(temporal, fitting_of(temporal))};
    }

    const auto civil = civil_of(date_time->date, date_time->time);

    if(!civil.has_value())
    {
      return std::unexpected{civil.error()};
    }

    return duration_of<D>(spell(temporal), civil->first, civil->second)
      .transform([](const D duration) -> std::chrono::local_time<D> { return std::chrono::local_time<D>{duration}; });
  }

  return std::unexpected{temporal_mismatch(temporal, fitting_of(temporal))};
}

///
///
template<toml_duration_c D>
auto hh_mm_ss_of(const temporal_t& temporal) -> std::expected<std::chrono::hh_mm_ss<D>, std::string>
{
  if(const auto* const time = std::get_if<toml::time>(&temporal))
  {
    const auto seconds = std::chrono::seconds{
      (static_cast<std::int64_t>(time->hour) * 3600) +
      (static_cast<std::int64_t>(time->minute) * 60) +
      static_cast<std::int64_t>(time->second)};

    return duration_of<D>(spell(temporal), seconds, std::chrono::nanoseconds{time->nanosecond})
      .transform([](const D duration) -> std::chrono::hh_mm_ss<D> { return std::chrono::hh_mm_ss<D>{duration}; });
  }

  return std::unexpected{temporal_mismatch(temporal, fitting_of(temporal))};
}

///
/// The quotient and the non-negative remainder of a floor division.
///
/// \param dividend The dividend, of either sign.
/// \param divisor The divisor, which is positive.
/// \return The quotient, rounded towards negative infinity, and the
///         remainder, in [0, divisor).
///
[[nodiscard]] constexpr auto floor_divided(std::int64_t dividend, std::int64_t divisor)
  -> std::pair<std::int64_t, std::int64_t>;

///
///
constexpr auto floor_divided(const std::int64_t dividend, const std::int64_t divisor)
  -> std::pair<std::int64_t, std::int64_t>
{
  auto quotient = dividend / divisor;
  auto remainder = dividend % divisor;

  if(remainder < 0)
  {
    quotient -= 1;
    remainder += divisor;
  }

  return {quotient, remainder};
}

///
/// The TOML date and time a duration since the epoch names, or why they
/// cannot be written: the date must sit in the years 0 to 9999. Everything
/// is computed in int64 from the tick count and D's period, never in
/// D::rep or in a standard duration, so a 32-bit representation cannot wrap
/// and a value at D's minimum cannot overflow.
///
/// \tparam D The duration type.
/// \param since_epoch The duration since the epoch.
/// \return The date and the time, or the reason they cannot be written.
///
template<toml_duration_c D>
[[nodiscard]] auto toml_date_time_of(D since_epoch) -> std::expected<std::pair<toml::date, toml::time>, std::string>;

///
///
template<toml_duration_c D>
auto toml_date_time_of(const D since_epoch) -> std::expected<std::pair<toml::date, toml::time>, std::string>
{
  static_assert(toml_duration_supported<D>());

  constexpr auto num = static_cast<std::int64_t>(D::period::num);
  constexpr auto den = static_cast<std::int64_t>(D::period::den);
  constexpr auto outside = std::string_view{"a date outside the years 0 to 9999"};

  // In units of 1/den of a second; an accepted den divides 1'000'000'000.
  const auto ticks = scaled(static_cast<std::int64_t>(since_epoch.count()), num);

  if(!ticks.has_value())
  {
    return std::unexpected{std::string{outside}};
  }

  const auto [whole, remainder] = floor_divided(*ticks, den);
  const auto [day, second_of_day] = floor_divided(whole, 86400);

  // A day no calendar year can name is refused before a year_month_day is
  // built from it; any other is checked by toml_date_of, which names the
  // year when it refuses.
  constexpr auto first_namable =
    static_cast<std::int64_t>(std::chrono::sys_days{std::chrono::year::min() / 1 / 1}.time_since_epoch().count());
  constexpr auto last_namable =
    static_cast<std::int64_t>(std::chrono::sys_days{std::chrono::year::max() / 12 / 31}.time_since_epoch().count());

  if(day < first_namable || day > last_namable)
  {
    return std::unexpected{std::string{outside}};
  }

  const auto date = toml_date_of(
    std::chrono::year_month_day{std::chrono::sys_days{std::chrono::days{static_cast<std::chrono::days::rep>(day)}}});

  if(!date.has_value())
  {
    return std::unexpected{date.error()};
  }

  const auto time = toml::time{
    static_cast<std::uint8_t>(second_of_day / 3600),
    static_cast<std::uint8_t>((second_of_day / 60) % 60),
    static_cast<std::uint8_t>(second_of_day % 60),
    static_cast<std::uint32_t>(remainder * (1000000000 / den))};

  return std::pair{*date, time};
}

} // namespace arude::config::detail

namespace rfl::parsing
{

///
/// Reads and writes a std::chrono::sys_time through libarude's reader and
/// writer: a TOML offset date-time, read with any offset and written in UTC
/// with Z, and a quoted string in TOML's own syntax accepted on read.
///
/// \tparam D The duration type.
/// \tparam ProcessorsType The processors the read runs under.
///
template<arude::config::detail::toml_duration_c D, typename ProcessorsType>
struct Parser<
  arude::config::detail::toml_reader,
  arude::config::detail::toml_writer,
  std::chrono::sys_time<D>,
  ProcessorsType>
{
  static_assert(arude::config::detail::toml_duration_supported<D>());

public: // Typedefs
  ///
  /// \see rfl::toml::Reader::InputVarType
  ///
  using input_var_t = arude::config::detail::toml_reader::InputVarType;

public: // Methods
  ///
  /// Reads an offset date-time as a sys_time, in UTC.
  /// \param reader The reader, unused beyond the node.
  /// \param var The node holding the value.
  /// \return The time point, or why the value is not one.
  ///
  [[nodiscard]] static auto read(const arude::config::detail::toml_reader& reader, const input_var_t& var) noexcept
    -> Result<std::chrono::sys_time<D>>;

  ///
  /// Writes a sys_time as an offset date-time in UTC, with Z.
  /// \tparam P The parent type, as rfl::parsing::Parent spells it.
  /// \param writer The writer.
  /// \param val The time point.
  /// \param parent The parent holding the field.
  ///
  template<typename P>
  static auto
  write(const arude::config::detail::toml_writer& writer, const std::chrono::sys_time<D>& val, const P& parent) noexcept
    -> void;

  ///
  /// Generates the schema, as a string field's.
  /// \param definitions Schema definitions collected so far.
  /// \return The schema.
  ///
  static auto to_schema(std::map<std::string, schema::Type>* definitions) -> schema::Type;
};

///
///
template<arude::config::detail::toml_duration_c D, typename ProcessorsType>
auto Parser<
  arude::config::detail::toml_reader,
  arude::config::detail::toml_writer,
  std::chrono::sys_time<D>,
  ProcessorsType>::
  read([[maybe_unused]] const arude::config::detail::toml_reader& reader, const input_var_t& var) noexcept
  -> Result<std::chrono::sys_time<D>>
{
  const auto temporal = arude::config::detail::temporal_of(*var);

  if(!temporal.has_value())
  {
    return error(temporal.error());
  }

  const auto value = arude::config::detail::sys_time_of<D>(*temporal);

  if(!value.has_value())
  {
    return error(value.error());
  }

  return *value;
}

///
///
template<arude::config::detail::toml_duration_c D, typename ProcessorsType>
template<typename P>
auto Parser<
  arude::config::detail::toml_reader,
  arude::config::detail::toml_writer,
  std::chrono::sys_time<D>,
  ProcessorsType>::
  write(const arude::config::detail::toml_writer& writer, const std::chrono::sys_time<D>& val, const P& parent) noexcept
  -> void
{
  const auto date_time = arude::config::detail::toml_date_time_of(val.time_since_epoch());

  if(!date_time.has_value())
  {
    writer.context().fail(
      std::format(
        "{} holds {}, which TOML cannot read back.", arude::config::detail::value_label(parent), date_time.error()));

    return;
  }

  Parent<arude::config::detail::toml_writer>::add_value(
    writer,
    ::toml::date_time{
      date_time->first, date_time->second, ::toml::time_offset{0, 0}
  },
    parent);
}

///
///
template<arude::config::detail::toml_duration_c D, typename ProcessorsType>
auto Parser<
  arude::config::detail::toml_reader,
  arude::config::detail::toml_writer,
  std::chrono::sys_time<D>,
  ProcessorsType>::to_schema(std::map<std::string, schema::Type>* definitions) -> schema::Type
{
  return Parser<arude::config::detail::toml_reader, arude::config::detail::toml_writer, std::string, ProcessorsType>::
    to_schema(definitions);
}

///
/// Reads and writes a std::chrono::local_time through libarude's reader and
/// writer: a TOML local date-time, never with an offset, and a quoted string
/// in TOML's own syntax accepted on read.
///
/// \tparam D The duration type.
/// \tparam ProcessorsType The processors the read runs under.
///
template<arude::config::detail::toml_duration_c D, typename ProcessorsType>
struct Parser<
  arude::config::detail::toml_reader,
  arude::config::detail::toml_writer,
  std::chrono::local_time<D>,
  ProcessorsType>
{
  static_assert(arude::config::detail::toml_duration_supported<D>());

public: // Typedefs
  ///
  /// \see rfl::toml::Reader::InputVarType
  ///
  using input_var_t = arude::config::detail::toml_reader::InputVarType;

public: // Methods
  ///
  /// Reads a local date-time as a local_time.
  /// \param reader The reader, unused beyond the node.
  /// \param var The node holding the value.
  /// \return The time point, or why the value is not one.
  ///
  [[nodiscard]] static auto read(const arude::config::detail::toml_reader& reader, const input_var_t& var) noexcept
    -> Result<std::chrono::local_time<D>>;

  ///
  /// Writes a local_time as a local date-time, without an offset.
  /// \tparam P The parent type, as rfl::parsing::Parent spells it.
  /// \param writer The writer.
  /// \param val The time point.
  /// \param parent The parent holding the field.
  ///
  template<typename P>
  static auto write(
    const arude::config::detail::toml_writer& writer, const std::chrono::local_time<D>& val, const P& parent) noexcept
    -> void;

  ///
  /// Generates the schema, as a string field's.
  /// \param definitions Schema definitions collected so far.
  /// \return The schema.
  ///
  static auto to_schema(std::map<std::string, schema::Type>* definitions) -> schema::Type;
};

///
///
template<arude::config::detail::toml_duration_c D, typename ProcessorsType>
auto Parser<
  arude::config::detail::toml_reader,
  arude::config::detail::toml_writer,
  std::chrono::local_time<D>,
  ProcessorsType>::
  read([[maybe_unused]] const arude::config::detail::toml_reader& reader, const input_var_t& var) noexcept
  -> Result<std::chrono::local_time<D>>
{
  const auto temporal = arude::config::detail::temporal_of(*var);

  if(!temporal.has_value())
  {
    return error(temporal.error());
  }

  const auto value = arude::config::detail::local_time_of<D>(*temporal);

  if(!value.has_value())
  {
    return error(value.error());
  }

  return *value;
}

///
///
template<arude::config::detail::toml_duration_c D, typename ProcessorsType>
template<typename P>
auto Parser<
  arude::config::detail::toml_reader,
  arude::config::detail::toml_writer,
  std::chrono::local_time<D>,
  ProcessorsType>::
  write(
    const arude::config::detail::toml_writer& writer, const std::chrono::local_time<D>& val, const P& parent) noexcept
  -> void
{
  const auto date_time = arude::config::detail::toml_date_time_of(val.time_since_epoch());

  if(!date_time.has_value())
  {
    writer.context().fail(
      std::format(
        "{} holds {}, which TOML cannot read back.", arude::config::detail::value_label(parent), date_time.error()));

    return;
  }

  Parent<arude::config::detail::toml_writer>::add_value(
    writer, ::toml::date_time{date_time->first, date_time->second}, parent);
}

///
///
template<arude::config::detail::toml_duration_c D, typename ProcessorsType>
auto Parser<
  arude::config::detail::toml_reader,
  arude::config::detail::toml_writer,
  std::chrono::local_time<D>,
  ProcessorsType>::to_schema(std::map<std::string, schema::Type>* definitions) -> schema::Type
{
  return Parser<arude::config::detail::toml_reader, arude::config::detail::toml_writer, std::string, ProcessorsType>::
    to_schema(definitions);
}

///
/// Reads and writes a std::chrono::year_month_day through libarude's reader
/// and writer: a TOML local date, and a quoted string in TOML's own syntax
/// accepted on read.
///
/// \tparam ProcessorsType The processors the read runs under.
///
template<typename ProcessorsType>
struct Parser<
  arude::config::detail::toml_reader,
  arude::config::detail::toml_writer,
  std::chrono::year_month_day,
  ProcessorsType>
{
public: // Typedefs
  ///
  /// \see rfl::toml::Reader::InputVarType
  ///
  using input_var_t = arude::config::detail::toml_reader::InputVarType;

public: // Methods
  ///
  /// Reads a local date as a year_month_day.
  /// \param reader The reader, unused beyond the node.
  /// \param var The node holding the value.
  /// \return The calendar date, or why the value is not one.
  ///
  [[nodiscard]] static auto read(const arude::config::detail::toml_reader& reader, const input_var_t& var) noexcept
    -> Result<std::chrono::year_month_day>;

  ///
  /// Writes a year_month_day as a local date.
  /// \tparam P The parent type, as rfl::parsing::Parent spells it.
  /// \param writer The writer.
  /// \param val The calendar date.
  /// \param parent The parent holding the field.
  ///
  template<typename P>
  static auto write(
    const arude::config::detail::toml_writer& writer, const std::chrono::year_month_day& val, const P& parent) noexcept
    -> void;

  ///
  /// Generates the schema, as a string field's.
  /// \param definitions Schema definitions collected so far.
  /// \return The schema.
  ///
  static auto to_schema(std::map<std::string, schema::Type>* definitions) -> schema::Type;
};

///
///
template<typename ProcessorsType>
auto Parser<
  arude::config::detail::toml_reader,
  arude::config::detail::toml_writer,
  std::chrono::year_month_day,
  ProcessorsType>::
  read([[maybe_unused]] const arude::config::detail::toml_reader& reader, const input_var_t& var) noexcept
  -> Result<std::chrono::year_month_day>
{
  const auto temporal = arude::config::detail::temporal_of(*var);

  if(!temporal.has_value())
  {
    return error(temporal.error());
  }

  const auto value = arude::config::detail::year_month_day_of(*temporal);

  if(!value.has_value())
  {
    return error(value.error());
  }

  return *value;
}

///
///
template<typename ProcessorsType>
template<typename P>
auto Parser<
  arude::config::detail::toml_reader,
  arude::config::detail::toml_writer,
  std::chrono::year_month_day,
  ProcessorsType>::
  write(
    const arude::config::detail::toml_writer& writer, const std::chrono::year_month_day& val, const P& parent) noexcept
  -> void
{
  const auto date = arude::config::detail::toml_date_of(val);

  if(!date.has_value())
  {
    writer.context().fail(
      std::format(
        "{} holds {}, which TOML cannot read back.", arude::config::detail::value_label(parent), date.error()));

    return;
  }

  Parent<arude::config::detail::toml_writer>::add_value(writer, *date, parent);
}

///
///
template<typename ProcessorsType>
auto Parser<
  arude::config::detail::toml_reader,
  arude::config::detail::toml_writer,
  std::chrono::year_month_day,
  ProcessorsType>::to_schema(std::map<std::string, schema::Type>* definitions) -> schema::Type
{
  return Parser<arude::config::detail::toml_reader, arude::config::detail::toml_writer, std::string, ProcessorsType>::
    to_schema(definitions);
}

///
/// Reads and writes a std::chrono::hh_mm_ss through libarude's reader and
/// writer: a TOML local time, and a quoted string in TOML's own syntax
/// accepted on read.
///
/// \tparam D The duration type.
/// \tparam ProcessorsType The processors the read runs under.
///
template<arude::config::detail::toml_duration_c D, typename ProcessorsType>
struct Parser<
  arude::config::detail::toml_reader,
  arude::config::detail::toml_writer,
  std::chrono::hh_mm_ss<D>,
  ProcessorsType>
{
  static_assert(arude::config::detail::toml_duration_supported<D>());

public: // Typedefs
  ///
  /// \see rfl::toml::Reader::InputVarType
  ///
  using input_var_t = arude::config::detail::toml_reader::InputVarType;

public: // Methods
  ///
  /// Reads a local time as an hh_mm_ss.
  /// \param reader The reader, unused beyond the node.
  /// \param var The node holding the value.
  /// \return The time of day, or why the value is not one.
  ///
  [[nodiscard]] static auto read(const arude::config::detail::toml_reader& reader, const input_var_t& var) noexcept
    -> Result<std::chrono::hh_mm_ss<D>>;

  ///
  /// Writes an hh_mm_ss as a local time.
  /// \tparam P The parent type, as rfl::parsing::Parent spells it.
  /// \param writer The writer.
  /// \param val The time of day.
  /// \param parent The parent holding the field.
  ///
  template<typename P>
  static auto
  write(const arude::config::detail::toml_writer& writer, const std::chrono::hh_mm_ss<D>& val, const P& parent) noexcept
    -> void;

  ///
  /// Generates the schema, as a string field's.
  /// \param definitions Schema definitions collected so far.
  /// \return The schema.
  ///
  static auto to_schema(std::map<std::string, schema::Type>* definitions) -> schema::Type;
};

///
///
template<arude::config::detail::toml_duration_c D, typename ProcessorsType>
auto Parser<
  arude::config::detail::toml_reader,
  arude::config::detail::toml_writer,
  std::chrono::hh_mm_ss<D>,
  ProcessorsType>::
  read([[maybe_unused]] const arude::config::detail::toml_reader& reader, const input_var_t& var) noexcept
  -> Result<std::chrono::hh_mm_ss<D>>
{
  const auto temporal = arude::config::detail::temporal_of(*var);

  if(!temporal.has_value())
  {
    return error(temporal.error());
  }

  const auto value = arude::config::detail::hh_mm_ss_of<D>(*temporal);

  if(!value.has_value())
  {
    return error(value.error());
  }

  return *value;
}

///
///
template<arude::config::detail::toml_duration_c D, typename ProcessorsType>
template<typename P>
auto Parser<
  arude::config::detail::toml_reader,
  arude::config::detail::toml_writer,
  std::chrono::hh_mm_ss<D>,
  ProcessorsType>::
  write(const arude::config::detail::toml_writer& writer, const std::chrono::hh_mm_ss<D>& val, const P& parent) noexcept
  -> void
{
  if(val.is_negative())
  {
    writer.context().fail(
      std::format(
        "{} holds {}, which TOML cannot read back.",
        arude::config::detail::value_label(parent),
        "a time before midnight"));

    return;
  }

  // Asked before to_duration(), which sums the parts in seconds and would
  // overflow for a D as coarse as minutes holding a huge value. hours() is
  // already floored from the absolute value, so it cannot overflow itself.
  if(val.hours() >= std::chrono::hours{24})
  {
    writer.context().fail(
      std::format(
        "{} holds {}, which TOML cannot read back.",
        arude::config::detail::value_label(parent),
        "a time of 24 hours or more"));

    return;
  }

  // hh_mm_ss<D>::to_duration() returns hh_mm_ss<D>::precision, seconds for
  // a D no finer than a second; the template argument is deduced from it.
  const auto time = arude::config::detail::toml_time_of(val.to_duration());

  if(!time.has_value())
  {
    writer.context().fail(
      std::format(
        "{} holds {}, which TOML cannot read back.", arude::config::detail::value_label(parent), time.error()));

    return;
  }

  Parent<arude::config::detail::toml_writer>::add_value(writer, *time, parent);
}

///
///
template<arude::config::detail::toml_duration_c D, typename ProcessorsType>
auto Parser<
  arude::config::detail::toml_reader,
  arude::config::detail::toml_writer,
  std::chrono::hh_mm_ss<D>,
  ProcessorsType>::to_schema(std::map<std::string, schema::Type>* definitions) -> schema::Type
{
  return Parser<arude::config::detail::toml_reader, arude::config::detail::toml_writer, std::string, ProcessorsType>::
    to_schema(definitions);
}

} // namespace rfl::parsing
