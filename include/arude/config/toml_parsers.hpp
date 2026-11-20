///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// The two places reflect-cpp reads without going through the reader, and
/// therefore around its checks: map keys and enums. Each is replaced by a
/// specialisation on libarude's reader, so a value that is not what the type
/// holds is refused rather than silently wrapped or cut.
///
/// Both replace reflect-cpp internals rather than hiding a reader member, so
/// a reflect-cpp bump has to rerun the reader's tests. MapReader's
/// constructor and read() are what MapParser calls, and the enum Parser
/// replaces StringConverter's fallback that turns a numeric string into an
/// enumerator.
///
#pragma once

#include "arude/config/toml_reader.hpp"
#include "arude/config/toml_writer.hpp"
#include "arude/type_name.hpp"

#include <rfl/always_false.hpp>
#include <rfl/enums.hpp>
#include <rfl/internal/enums/StringConverter.hpp>
#include <rfl/internal/has_reflection_type_v.hpp>
#include <rfl/Result.hpp>
// Parser.hpp, not MapReader.hpp alone: it brings the primary MapReader after
// the primary Parser, which MapReader's definition names.
#include <rfl/parsing/Parent.hpp>
#include <rfl/parsing/Parser.hpp>
#include <rfl/parsing/schema/Type.hpp>
#include <toml++/toml.hpp>

#include <charconv>
#include <chrono>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <expected>
#include <format>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <ratio>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

namespace arude::config::detail
{

///
/// The integral key a map's TOML key spells, or why it spells none.
/// The whole key must be a decimal integer that fits K: a trailing character
/// and a value outside K's range are refused, and an unsigned K refuses a
/// negative key rather than wrapping it.
///
/// \tparam K
/// \param key The TOML key, as read.
/// \return The key, or the reason it is not one.
///
template<std::integral K>
[[nodiscard]] auto integral_key(std::string_view key) -> std::expected<K, std::string>;

///
/// The enumerator a name spells, or why it spells none.
/// A flag enum's value is the '|'-separated names the writer produces; each
/// part must name a flag. Anything else, a numeric string included, names
/// nothing.
///
/// \tparam E The enum type.
/// \param name The value, as read from a TOML string.
/// \return The enumerator, or the reason the name spells none.
///
template<typename E>
  requires std::is_enum_v<E>
[[nodiscard]] auto enumerator_named(std::string_view name) -> std::expected<E, std::string>;

///
///
template<std::integral K>
auto integral_key(const std::string_view key) -> std::expected<K, std::string>
{
  const auto* const begin = key.data();
  const auto* const end = begin + key.size();

  const auto not_integer = [&key] -> std::string { return std::format("the key '{}' is not an integer.", key); };

  const auto out_of_range = [&key] -> std::string
  {
    return std::format(
      "the key '{}' is out of range, as {} holds {} to {}.",
      key,
      type_name<K>(),
      std::numeric_limits<K>::min(),
      std::numeric_limits<K>::max());
  };

  if constexpr(std::is_signed_v<K>)
  {
    auto value = std::int64_t{};
    const auto [parsed, errc] = std::from_chars(begin, end, value);

    if(errc != std::errc{} || parsed != end)
    {
      return std::unexpected{not_integer()};
    }

    if(!std::in_range<K>(value))
    {
      return std::unexpected{out_of_range()};
    }

    return static_cast<K>(value);
  }
  else
  {
    // A negative key says so, as the value check does, before the range
    // check: it is a sign error, not an out-of-range error.
    if(begin != end && *begin == '-')
    {
      auto magnitude = std::uint64_t{};
      const auto [parsed, errc] = std::from_chars(begin + 1, end, magnitude);

      if(errc == std::errc{} && parsed == end)
      {
        return std::unexpected{std::format("the key '{}' is negative, where an unsigned one is expected.", key)};
      }

      return std::unexpected{not_integer()};
    }

    // A positive key beyond INT64_MAX can still fit an unsigned K, so it is
    // parsed unsigned rather than reported as not an integer.
    auto value = std::uint64_t{};
    const auto [parsed, errc] = std::from_chars(begin, end, value);

    if(errc != std::errc{} || parsed != end)
    {
      return std::unexpected{not_integer()};
    }

    if(!std::in_range<K>(value))
    {
      return std::unexpected{out_of_range()};
    }

    return static_cast<K>(value);
  }
}

///
///
template<typename E>
  requires std::is_enum_v<E>
auto enumerator_named(const std::string_view name) -> std::expected<E, std::string>
{
  const auto unknown = [&name] -> std::string
  { return std::format("'{}' names no enumerator of {}.", name, type_name<E>()); };

  const auto find_named = [](const std::string_view enumerator_name) -> std::optional<E>
  {
    for(const auto& [known_name, enumerator] : rfl::get_enumerator_array<E>())
    {
      if(known_name == enumerator_name)
      {
        return enumerator;
      }
    }

    return std::nullopt;
  };

  if constexpr(rfl::internal::enums::is_flag_enum<E>)
  {
    auto value = std::underlying_type_t<E>{0};

    for(const auto part : name | std::views::split('|'))
    {
      const auto part_name = std::string_view{part.begin(), part.end()};
      const auto found = find_named(part_name);

      if(!found.has_value())
      {
        return std::unexpected{unknown()};
      }

      value |= static_cast<std::underlying_type_t<E>>(*found);
    }

    return E{value};
  }
  else
  {
    const auto found = find_named(name);

    if(!found.has_value())
    {
      return std::unexpected{unknown()};
    }

    return *found;
  }
}

} // namespace arude::config::detail

namespace rfl::parsing
{

///
/// Replaces reflect-cpp's MapReader for libarude's reader, so an integral
/// key is parsed whole and range checked rather than cut down by a cast.
/// Reads the value exactly as the primary template does.
///
/// \tparam MapType The map type being read.
/// \tparam ProcessorsType The processors the read runs under.
///
template<typename MapType, typename ProcessorsType>
class MapReader<arude::config::detail::toml_reader, arude::config::detail::toml_writer, MapType, ProcessorsType>
{
public: // Typedefs
  ///
  /// The node a read receives, as the reader spells it.
  ///
  using input_var_t = arude::config::detail::toml_reader::InputVarType;

  ///
  /// The map's key type.
  ///
  using key_t = std::remove_cvref_t<typename MapType::value_type::first_type>;

  ///
  /// The map's value type.
  ///
  using value_t = std::remove_cvref_t<typename MapType::value_type::second_type>;

public: // Structors
  ///
  /// Constructs a reader over a map being read.
  /// \param reader The reader, whose value parser is reused.
  /// \param map The map being filled. Not retained.
  /// \param errors Where a field that does not read is reported.
  ///
  MapReader(const arude::config::detail::toml_reader* reader, MapType* map, std::vector<Error>* errors);

public: // Methods
  ///
  /// Reads one entry of the map.
  /// \param name The TOML key, which becomes the entry's key.
  /// \param var The node holding the value.
  ///
  auto read(std::string_view name, const input_var_t& var) const noexcept -> void;

private: // Helpers
  ///
  /// Converts a TOML key to the map's key type, range checked where the key
  /// type is integral.
  ///
  /// \param name The TOML key.
  /// \return The key, or why the name spells none.
  ///
  [[nodiscard]] static auto make_key(std::string_view name) -> Result<key_t>;

private: // Variables
  ///
  /// The reader, whose value parser the entries are read with.
  ///
  const arude::config::detail::toml_reader* reader_;

  ///
  /// The map being filled.
  ///
  MapType* map_;

  ///
  /// Where a field that does not read is reported.
  ///
  std::vector<Error>* errors_;
};

///
///
template<typename MapType, typename ProcessorsType>
MapReader<arude::config::detail::toml_reader, arude::config::detail::toml_writer, MapType, ProcessorsType>::MapReader(
  const arude::config::detail::toml_reader* const reader, MapType* const map, std::vector<Error>* const errors)
  : reader_{reader}
  , map_{map}
  , errors_{errors}
{
}

///
///
template<typename MapType, typename ProcessorsType>
// NOLINTNEXTLINE(bugprone-exception-escape): noexcept is the reader contract; only an allocator failure can throw.
auto MapReader<arude::config::detail::toml_reader, arude::config::detail::toml_writer, MapType, ProcessorsType>::read(
  const std::string_view name, const input_var_t& var) const noexcept -> void
{
  auto key = make_key(name);

  if(!key)
  {
    errors_->push_back(Error("Failed to parse field '" + std::string(name) + "': " + key.error().what()));

    return;
  }

  auto value =
    Parser<arude::config::detail::toml_reader, arude::config::detail::toml_writer, value_t, ProcessorsType>::read(
      *reader_, var);

  if(!value)
  {
    errors_->push_back(Error("Failed to parse field '" + std::string(name) + "': " + value.error().what()));

    return;
  }

  // Two distinct TOML keys, such as "7" and "007", can spell one integral
  // key; emplace() would then keep the first entry and drop this one in
  // silence.
  if(!map_->emplace(std::move(*key), std::move(*value)).second)
  {
    errors_->emplace_back(
      "Failed to parse field '" +
      std::string(name) +
      "': the key '" +
      std::string(name) +
      "' names the same entry as another key.");
  }
}

///
///
template<typename MapType, typename ProcessorsType>
auto MapReader<arude::config::detail::toml_reader, arude::config::detail::toml_writer, MapType, ProcessorsType>::
  make_key(const std::string_view name) -> Result<key_t>
{
  if constexpr(std::is_integral_v<key_t>)
  {
    const auto key = arude::config::detail::integral_key<key_t>(name);

    if(!key.has_value())
    {
      return error(key.error());
    }

    return *key;
  }
  else if constexpr(std::is_floating_point_v<key_t>)
  {
    // Forwarded to std::stod unchanged, as the primary template forwards it.
    try
    {
      return static_cast<key_t>(std::stod(std::string{name}));
    }
    catch(const std::exception& ex)
    {
      return error(ex.what());
    }
  }
  else if constexpr(internal::has_reflection_type_v<key_t>)
  {
    using reflection_t = std::remove_cvref_t<typename key_t::ReflectionType>;

    if constexpr(std::is_integral_v<reflection_t>)
    {
      const auto key = arude::config::detail::integral_key<reflection_t>(name);

      if(!key.has_value())
      {
        return error(key.error());
      }

      try
      {
        return key_t{*key};
      }
      catch(const std::exception& ex)
      {
        return error(ex.what());
      }
    }
    else if constexpr(std::is_floating_point_v<reflection_t>)
    {
      try
      {
        return key_t{static_cast<reflection_t>(std::stod(std::string{name}))};
      }
      catch(const std::exception& ex)
      {
        return error(ex.what());
      }
    }
    else
    {
      return key_t{std::string{name}};
    }
  }
  else
  {
    return key_t{std::string{name}};
  }
}

///
/// Reads an enum by its enumerator names only, where the primary template
/// also turns a numeric string into an enumerator through std::stoi and a
/// static_cast. Writes it as reflect-cpp does, and refuses a value that
/// names no enumerator rather than writing the number it would spell.
///
/// \tparam E The enum type.
/// \tparam ProcessorsType The processors the read runs under.
///
template<typename E, typename ProcessorsType>
  requires std::is_enum_v<E>
struct Parser<arude::config::detail::toml_reader, arude::config::detail::toml_writer, E, ProcessorsType>
{
public: // Typedefs
  ///
  /// The node a read receives, as the reader spells it.
  ///
  using input_var_t = arude::config::detail::toml_reader::InputVarType;

public: // Methods
  ///
  /// Reads a TOML string as an enumerator of E.
  /// \param reader The reader, for the string itself.
  /// \param var The node holding the value.
  /// \return The enumerator, or why the value names none.
  ///
  [[nodiscard]] static auto read(const arude::config::detail::toml_reader& reader, const input_var_t& var) noexcept
    -> Result<E>;

  ///
  /// Writes an enumerator as its name, or records through the writer's
  /// context that the value names no enumerator and cannot be written.
  /// \tparam P The parent type, as rfl::parsing::Parent spells it.
  /// \param writer The writer.
  /// \param val The enumerator.
  /// \param parent The parent holding the value.
  ///
  template<typename P>
  static auto write(const arude::config::detail::toml_writer& writer, const E& val, const P& parent) noexcept -> void;

  ///
  /// Generates the schema of E: the same one a string field would have.
  /// \param definitions Schema definitions collected so far.
  /// \return The schema.
  ///
  static auto to_schema(std::map<std::string, schema::Type>* definitions) -> schema::Type;

private: // Helpers
  ///
  /// The value's name as reflect-cpp writes it.
  /// \param val The enumerator.
  /// \return The name, or a numeric fallback where the value names none.
  ///
  [[nodiscard]] static auto name_of(const E& val) -> std::string;
};

///
///
template<typename E, typename ProcessorsType>
  requires std::is_enum_v<E>
// NOLINTNEXTLINE(bugprone-exception-escape): noexcept is the reader contract; only an allocator failure can throw.
auto Parser<arude::config::detail::toml_reader, arude::config::detail::toml_writer, E, ProcessorsType>::read(
  const arude::config::detail::toml_reader& reader, const input_var_t& var) noexcept -> Result<E>
{
  const auto text = reader.template to_basic_type<std::string>(var);

  if(!text.has_value())
  {
    return error(text.error());
  }

  const auto found = arude::config::detail::enumerator_named<E>(*text);

  if(!found.has_value())
  {
    return error(found.error());
  }

  return *found;
}

///
///
template<typename E, typename ProcessorsType>
  requires std::is_enum_v<E>
template<typename P>
auto Parser<arude::config::detail::toml_reader, arude::config::detail::toml_writer, E, ProcessorsType>::write(
  const arude::config::detail::toml_writer& writer, const E& val, const P& parent) noexcept -> void
{
  // A flag enum's converter shifts the value right until it is zero, so a
  // negative value, or one with bits outside the known flags, is checked
  // before the converter ever sees it: it would not terminate on one.
  if constexpr(rfl::internal::enums::is_flag_enum<E>)
  {
    using underlying_t = std::underlying_type_t<E>;

    auto known = underlying_t{0};

    for(const auto& [known_name, enumerator] : rfl::get_enumerator_array<E>())
    {
      known |= static_cast<underlying_t>(enumerator);
    }

    if((static_cast<underlying_t>(std::to_underlying(val)) & ~known) != underlying_t{0})
    {
      writer.context().fail(
        std::format(
          "{} holds {}, which names no enumerator of {}.",
          arude::config::detail::value_label(parent),
          std::to_underlying(val),
          arude::type_name<E>()));

      return;
    }
  }

  const auto text = name_of(val);

  // A value the converter spells as a number is refused: this reader would
  // refuse it, so writing it would produce a file the application cannot
  // load back.
  if(!arude::config::detail::enumerator_named<E>(text).has_value())
  {
    writer.context().fail(
      std::format(
        "{} holds {}, which names no enumerator of {}.",
        arude::config::detail::value_label(parent),
        std::to_underlying(val),
        arude::type_name<E>()));

    return;
  }

  Parent<arude::config::detail::toml_writer>::add_value(writer, text, parent);
}

///
///
template<typename E, typename ProcessorsType>
  requires std::is_enum_v<E>
auto Parser<arude::config::detail::toml_reader, arude::config::detail::toml_writer, E, ProcessorsType>::to_schema(
  std::map<std::string, schema::Type>* definitions) -> schema::Type
{
  return Parser<arude::config::detail::toml_reader, arude::config::detail::toml_writer, std::string, ProcessorsType>::
    to_schema(definitions);
}

///
///
template<typename E, typename ProcessorsType>
  requires std::is_enum_v<E>
auto Parser<arude::config::detail::toml_reader, arude::config::detail::toml_writer, E, ProcessorsType>::name_of(
  const E& val) -> std::string
{
  return internal::enums::StringConverter<E>::enum_to_string(val);
}

///
/// Reads and writes a std::chrono::duration as the table reflect-cpp writes
/// today: `{ count = <int>, unit = '<name>' }`. Replaces reflect-cpp's own
/// duration parser, which cannot build under rfl::DefaultIfMissing because
/// its unit literal has no default constructor; this one holds the unit as a
/// plain string, and refuses a unit name or a count the target duration
/// cannot hold without loss, which the replaced parser silently truncates.
///
/// \tparam Rep The duration's representation.
/// \tparam Period The duration's period.
/// \tparam ProcessorsType The processors the read runs under.
///
template<typename Rep, typename Period, typename ProcessorsType>
  requires AreReaderAndWriter<
    arude::config::detail::toml_reader,
    arude::config::detail::toml_writer,
    std::chrono::duration<Rep, Period>>
struct Parser<
  arude::config::detail::toml_reader,
  arude::config::detail::toml_writer,
  std::chrono::duration<Rep, Period>,
  ProcessorsType>
{
public: // Typedefs
  ///
  /// \see rfl::toml::Reader::InputVarType
  ///
  using input_var_t = arude::config::detail::toml_reader::InputVarType;

  ///
  /// The duration type.
  ///
  using duration_t = std::chrono::duration<Rep, Period>;

public: // Methods
  ///
  /// Reads a count and a unit as a duration, refusing what the duration
  /// cannot hold without loss.
  /// \param reader The reader, for the table's fields.
  /// \param var The node holding the table.
  /// \return The duration, or why the table does not hold one.
  ///
  [[nodiscard]] static auto read(const arude::config::detail::toml_reader& reader, const input_var_t& var) noexcept
    -> Result<duration_t>;

  ///
  /// Writes the duration as a count in the unit its period names.
  /// \tparam P The parent type, as rfl::parsing::Parent spells it.
  /// \param writer The writer.
  /// \param val The duration.
  /// \param parent The parent holding the field.
  ///
  template<typename P>
  static auto write(const arude::config::detail::toml_writer& writer, const duration_t& val, const P& parent) noexcept
    -> void;

  ///
  /// Generates the schema of the table a duration is held in.
  /// \param definitions Schema definitions collected so far.
  /// \return The schema.
  ///
  static auto to_schema(std::map<std::string, schema::Type>* definitions) -> schema::Type;

private: // Types
  ///
  /// The table the duration is held in. The unit is a plain string, which
  /// is what keeps the type default-constructible under
  /// rfl::DefaultIfMissing.
  ///
  struct table_t
  {
    ///
    /// The count.
    ///
    std::int64_t count = 0;

    ///
    /// The unit's name.
    ///
    std::string unit = "seconds";
  };

private: // Helpers
  ///
  /// The unit a duration type spells, which is also how it is written.
  /// \tparam Unit The duration type, one of the ten chrono durations TOML's
  ///              table can name.
  /// \return The unit's name.
  ///
  template<typename Unit>
  [[nodiscard]] static consteval auto unit_name() -> std::string_view;

  ///
  /// Converts a count in the unit given to the target duration, refusing a
  /// value the duration cannot hold without loss.
  /// \tparam Unit The duration type the count is in.
  /// \param count The count.
  /// \return The duration, or why the count does not fit.
  ///
  template<typename Unit>
  [[nodiscard]] static auto convert(std::int64_t count) -> Result<duration_t>;

  ///
  /// Converts a count and a unit name to the target duration.
  /// \param value The table read.
  /// \return The duration, or why the table does not hold one.
  ///
  [[nodiscard]] static auto to_duration(const table_t& value) -> Result<duration_t>;
};

///
///
template<typename Rep, typename Period, typename ProcessorsType>
  requires AreReaderAndWriter<
    arude::config::detail::toml_reader,
    arude::config::detail::toml_writer,
    std::chrono::duration<Rep, Period>>
auto Parser<
  arude::config::detail::toml_reader,
  arude::config::detail::toml_writer,
  std::chrono::duration<Rep, Period>,
  ProcessorsType>::read(const arude::config::detail::toml_reader& reader, const input_var_t& var) noexcept
  -> Result<duration_t>
{
  const auto found =
    Parser<arude::config::detail::toml_reader, arude::config::detail::toml_writer, table_t, ProcessorsType>::read(
      reader, var);

  if(!found.has_value())
  {
    return error(found.error());
  }

  return to_duration(*found);
}

///
///
template<typename Rep, typename Period, typename ProcessorsType>
  requires AreReaderAndWriter<
    arude::config::detail::toml_reader,
    arude::config::detail::toml_writer,
    std::chrono::duration<Rep, Period>>
template<typename P>
auto Parser<
  arude::config::detail::toml_reader,
  arude::config::detail::toml_writer,
  std::chrono::duration<Rep, Period>,
  ProcessorsType>::
  write(const arude::config::detail::toml_writer& writer, const duration_t& val, const P& parent) noexcept -> void
{
  const auto held =
    table_t{.count = static_cast<std::int64_t>(val.count()), .unit = std::string{unit_name<duration_t>()}};

  Parser<arude::config::detail::toml_reader, arude::config::detail::toml_writer, table_t, ProcessorsType>::write(
    writer, held, parent);
}

///
///
template<typename Rep, typename Period, typename ProcessorsType>
  requires AreReaderAndWriter<
    arude::config::detail::toml_reader,
    arude::config::detail::toml_writer,
    std::chrono::duration<Rep, Period>>
auto Parser<
  arude::config::detail::toml_reader,
  arude::config::detail::toml_writer,
  std::chrono::duration<Rep, Period>,
  ProcessorsType>::to_schema(std::map<std::string, schema::Type>* definitions) -> schema::Type
{
  return Parser<arude::config::detail::toml_reader, arude::config::detail::toml_writer, table_t, ProcessorsType>::
    to_schema(definitions);
}

///
///
template<typename Rep, typename Period, typename ProcessorsType>
  requires AreReaderAndWriter<
    arude::config::detail::toml_reader,
    arude::config::detail::toml_writer,
    std::chrono::duration<Rep, Period>>
template<typename Unit>
consteval auto Parser<
  arude::config::detail::toml_reader,
  arude::config::detail::toml_writer,
  std::chrono::duration<Rep, Period>,
  ProcessorsType>::unit_name() -> std::string_view
{
  if constexpr(std::same_as<Unit, std::chrono::nanoseconds>)
  {
    return "nanoseconds";
  }
  else if constexpr(std::same_as<Unit, std::chrono::microseconds>)
  {
    return "microseconds";
  }
  else if constexpr(std::same_as<Unit, std::chrono::milliseconds>)
  {
    return "milliseconds";
  }
  else if constexpr(std::same_as<Unit, std::chrono::seconds>)
  {
    return "seconds";
  }
  else if constexpr(std::same_as<Unit, std::chrono::minutes>)
  {
    return "minutes";
  }
  else if constexpr(std::same_as<Unit, std::chrono::hours>)
  {
    return "hours";
  }
  else if constexpr(std::same_as<Unit, std::chrono::days>)
  {
    return "days";
  }
  else if constexpr(std::same_as<Unit, std::chrono::weeks>)
  {
    return "weeks";
  }
  else if constexpr(std::same_as<Unit, std::chrono::months>)
  {
    return "months";
  }
  else if constexpr(std::same_as<Unit, std::chrono::years>)
  {
    return "years";
  }
  else
  {
    static_assert(
      always_false_v<Unit>,
      "arude::config: a std::chrono::duration is written in the unit its period names; this period names none of the "
      "ten.");
  }
}

///
///
template<typename Rep, typename Period, typename ProcessorsType>
  requires AreReaderAndWriter<
    arude::config::detail::toml_reader,
    arude::config::detail::toml_writer,
    std::chrono::duration<Rep, Period>>
template<typename Unit>
auto Parser<
  arude::config::detail::toml_reader,
  arude::config::detail::toml_writer,
  std::chrono::duration<Rep, Period>,
  ProcessorsType>::convert(const std::int64_t count) -> Result<duration_t>
{
  // The target tick count is the count scaled by the ratio of the two
  // periods, reduced at compile time. The multiplication is checked in
  // int64 before it is taken, and the division must come out exact, so
  // neither an overflow nor a truncation can slip through.
  constexpr auto ratio = std::ratio_divide<typename Unit::period, Period>{};
  constexpr auto num = static_cast<std::int64_t>(ratio.num);
  constexpr auto den = static_cast<std::int64_t>(ratio.den);

  const auto refuse = [count] -> Result<duration_t>
  {
    return error(
      std::format(
        "the value of {} {} cannot be represented as {} without loss.",
        count,
        unit_name<Unit>(),
        arude::type_name<duration_t>()));
  };

  if constexpr(std::integral<Rep>)
  {
    // count * num, checked before it is taken. Both periods are positive,
    // so a normalised ratio keeps num non-negative.
    auto overflowed = false;

    if(count > 0)
    {
      overflowed = num > std::numeric_limits<std::int64_t>::max() / count;
    }
    else if(count < 0)
    {
      if(count == std::numeric_limits<std::int64_t>::min())
      {
        overflowed = num > 1;
      }
      else
      {
        overflowed = num > std::numeric_limits<std::int64_t>::max() / -count;
      }
    }

    const auto product = overflowed ? std::int64_t{0} : count * num;
    const auto ticks = product / den;

    const auto fits = ticks >= static_cast<std::int64_t>(std::numeric_limits<Rep>::min()) &&
                      ticks <= static_cast<std::int64_t>(std::numeric_limits<Rep>::max());

    if(overflowed || product % den != 0 || !fits)
    {
      return refuse();
    }

    return duration_t{static_cast<Rep>(ticks)};
  }
  else
  {
    // A floating-point representation: the range is what is checked.
    const auto ticks = static_cast<long double>(count) * ratio.num / ratio.den;

    if(
      !std::isfinite(ticks) ||
      ticks < static_cast<long double>(std::numeric_limits<Rep>::lowest()) ||
      ticks > static_cast<long double>(std::numeric_limits<Rep>::max()))
    {
      return refuse();
    }

    return duration_t{static_cast<Rep>(ticks)};
  }
}

///
///
template<typename Rep, typename Period, typename ProcessorsType>
  requires AreReaderAndWriter<
    arude::config::detail::toml_reader,
    arude::config::detail::toml_writer,
    std::chrono::duration<Rep, Period>>
auto Parser<
  arude::config::detail::toml_reader,
  arude::config::detail::toml_writer,
  std::chrono::duration<Rep, Period>,
  ProcessorsType>::to_duration(const table_t& value) -> Result<duration_t>
{
  if(value.unit == "nanoseconds")
  {
    return convert<std::chrono::nanoseconds>(value.count);
  }

  if(value.unit == "microseconds")
  {
    return convert<std::chrono::microseconds>(value.count);
  }

  if(value.unit == "milliseconds")
  {
    return convert<std::chrono::milliseconds>(value.count);
  }

  if(value.unit == "seconds")
  {
    return convert<std::chrono::seconds>(value.count);
  }

  if(value.unit == "minutes")
  {
    return convert<std::chrono::minutes>(value.count);
  }

  if(value.unit == "hours")
  {
    return convert<std::chrono::hours>(value.count);
  }

  if(value.unit == "days")
  {
    return convert<std::chrono::days>(value.count);
  }

  if(value.unit == "weeks")
  {
    return convert<std::chrono::weeks>(value.count);
  }

  if(value.unit == "months")
  {
    return convert<std::chrono::months>(value.count);
  }

  if(value.unit == "years")
  {
    return convert<std::chrono::years>(value.count);
  }

  return error(std::format("'{}' is not a unit of a std::chrono::duration.", value.unit));
}

} // namespace rfl::parsing
