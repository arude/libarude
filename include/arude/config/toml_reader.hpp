///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// The reader the configuration support parses TOML through: reflect-cpp's
/// TOML reader carrying a per-call context, with integers range checked
/// instead of silently cast.
///
/// reflect-cpp's parsers are templated on the reader type and call its
/// to_basic_type() for every leaf, so deriving the reader and hiding that one
/// member reaches every field, vector element, optional and map value a read
/// goes through. The context travels the same way: a parser specialisation
/// that must record a failure with a code of its own takes it from reader.
///
/// read_array() is hidden too, so that newer_in()'s probe reads on past an
/// array element that fails. It names reflect-cpp's internal
/// rfl::parsing::VectorReader, rfl::parsing::ArrayReader and
/// rfl::parsing::TupleReader to do so.
///
#pragma once

#include "arude/config/common.hpp"
#include "arude/config/load_policy.hpp"
#include "arude/type_name.hpp"

#include <rfl/parsing/ArrayReader.hpp>
#include <rfl/parsing/Parser_base.hpp>
#include <rfl/parsing/TupleReader.hpp>
#include <rfl/parsing/VectorReader.hpp>
#include <rfl/Result.hpp>
#include <rfl/toml/Reader.hpp>
#include <rfl/Tuple.hpp>
#include <toml++/toml.hpp>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace arude::config::detail
{

///
/// Per-call state a read records into. One per read, used by one thread.
/// The reader hands out a reference to it, and a Parser specialisation calls
/// fail() to record a failure whose error code the reflect-cpp error text
/// cannot carry; raise_read_failure() then matches the recorded message
/// against the final error to decide the code. A record only ever comes
/// from a part of the read that failed it: neither versioned<> nor a tagged
/// union, the two reads that record, can appear inside an untagged variant
/// alternative (see contains_versioned_or_tagged), and a tagged union parses the
/// one alternative its tag names, so no alternative that lost to another
/// leaves a record behind. The message that decided the failure is
/// therefore in the final error text. newer_in()'s probe, which also reads
/// the array elements after one that fails, asks only newer().
///
class read_context final
{
public: // Structors
  ///
  /// Constructs a context for one read.
  /// \param policy The load policy the read runs under.
  /// \param probing Whether the read is newer_in()'s probe; see probing().
  ///
  explicit read_context(load_policy policy, bool probing = false);

public: // Accessors
  ///
  /// Returns the policy the read runs under.
  /// \return The policy.
  ///
  [[nodiscard]] auto policy() const -> load_policy;

  ///
  /// Returns whether the read is newer_in()'s probe. The probe reads a
  /// versioned table that declares no version as its newest type anyway,
  /// result discarded, so a newer member inside it is still recorded
  /// through note_newer().
  /// \return true for the probe.
  ///
  [[nodiscard]] auto probing() const -> bool;

public: // Methods
  ///
  /// Records a failure that needs a code other than invalid_payload, with the
  /// exact message the caller returns in its rfl::Error, so the two are the
  /// same text. Returns the message, so a call site is
  /// `return rfl::error(context.fail(...));`.
  ///
  /// \param code The code the failure must raise.
  /// \param message The message that names the failure, embedded in the error.
  /// \return The message, for the rfl::Error.
  ///
  auto fail(config_error code, std::string message) -> std::string;

  ///
  /// Returns the code of the first recorded failure whose message the final
  /// error text contains, invalid_payload if none of them does.
  ///
  /// \param final_error The error reflect-cpp reports for the whole read.
  /// \return The code that fits.
  ///
  [[nodiscard]] auto code_for(std::string_view final_error) const -> config_error;

  ///
  /// Records that a table declares a version newer than this build knows.
  /// Kept apart from fail(), because it is asked of a read whatever became of
  /// it: store() refuses to write over newer data even where the rest of the
  /// source does not read. Only the first one is kept.
  ///
  /// \param message The message naming the table and both versions.
  ///
  auto note_newer(std::string message) -> void;

  ///
  /// Returns the message of the first newer version recorded, if any.
  /// \return The message, or std::nullopt where none was seen.
  ///
  [[nodiscard]] auto newer() const -> const std::optional<std::string>&;

private: // Types
  ///
  /// One recorded failure: its code and the message that names it.
  /// Passive pair.
  ///
  struct failure final
  {
    ///
    /// The code the failure must raise.
    ///
    config_error code;

    ///
    /// The message embedded in the rfl::Error.
    ///
    std::string message;
  };

private: // Variables
  ///
  /// The policy the read runs under.
  ///
  load_policy policy_;

  ///
  /// Whether the read is newer_in()'s probe.
  ///
  bool probing_ = false;

  ///
  /// Failures recorded so far, in the order they were recorded.
  ///
  std::vector<failure> failures_;

  ///
  /// The first newer version seen, by its message.
  ///
  std::optional<std::string> newer_;
};

///
/// How the probe reads an array element after one that failed, for the
/// array readers reflect-cpp reads a container through. Unspecialised for
/// any other, which then stops at the first failure as reflect-cpp's does.
///
/// \tparam ArrayReader The array reader reflect-cpp hands read_array().
///
template<typename ArrayReader>
struct array_scan
{
  ///
  /// Whether the elements after a failure can be read on.
  ///
  static constexpr auto known = false;
};

///
/// \see array_scan
///
/// \tparam R The reader.
/// \tparam W The writer.
/// \tparam VecType The container read, a std::vector or another reflect-cpp
///                 reads element by element.
/// \tparam ProcessorsType The processors the read runs under.
///
template<typename R, typename W, typename VecType, typename ProcessorsType>
struct array_scan<rfl::parsing::VectorReader<R, W, VecType, ProcessorsType>>
{
  ///
  /// \see array_scan::known
  ///
  static constexpr auto known = true;

  ///
  /// Reads one element into a scratch container, which is then discarded,
  /// through the same element reader the container is read with.
  /// \param reader The reader.
  /// \param var The node holding the element.
  /// \param index The element's position in the array, which every element
  ///              of the container shares a type with.
  ///
  static auto read(const R& reader, const typename R::InputVarType& var, std::size_t index) -> void;
};

///
/// \see array_scan
///
/// \tparam R The reader.
/// \tparam W The writer.
/// \tparam ProcessorsType The processors the read runs under.
/// \tparam T The element type of the std::array read.
/// \tparam Size The number of elements of the std::array read.
///
template<typename R, typename W, typename ProcessorsType, typename T, std::size_t Size>
struct array_scan<rfl::parsing::ArrayReader<R, W, ProcessorsType, T, Size>>
{
  ///
  /// \see array_scan::known
  ///
  static constexpr auto known = true;

  ///
  /// Reads one element as T, and discards it.
  /// \param reader The reader.
  /// \param var The node holding the element.
  /// \param index The element's position in the array, which every element
  ///              of the std::array shares a type with.
  ///
  static auto read(const R& reader, const typename R::InputVarType& var, std::size_t index) -> void;
};

///
/// \see array_scan
///
/// \tparam R The reader.
/// \tparam W The writer.
/// \tparam TupleType The tuple read, a std::tuple or rfl::Tuple; std::pair
///                   is read as one.
/// \tparam IgnoreEmptyContainers reflect-cpp's flag of that name.
/// \tparam AllRequired reflect-cpp's flag of that name.
/// \tparam ProcessorsType The processors the read runs under.
///
template<
  typename R,
  typename W,
  typename TupleType,
  bool IgnoreEmptyContainers,
  bool AllRequired,
  typename ProcessorsType>
struct array_scan<rfl::parsing::TupleReader<R, W, TupleType, IgnoreEmptyContainers, AllRequired, ProcessorsType>>
{
  ///
  /// \see array_scan::known
  ///
  static constexpr auto known = true;

  ///
  /// Reads one element as the tuple's element type at its position, and
  /// discards it. An element past the tuple's size is not read; the array
  /// has failed already.
  /// \param reader The reader.
  /// \param var The node holding the element.
  /// \param index The element's position in the array.
  ///
  static auto read(const R& reader, const typename R::InputVarType& var, std::size_t index) -> void;
};

///
/// rfl::toml::Reader with the per-call context and range-checked integers.
/// Hides the base's to_basic_type() for integral types: a TOML integer is
/// read as one and must fit T, where the base casts and lets a value wrap
/// or cut. Every other type is forwarded to the base unchanged.
///
class toml_reader final : public rfl::toml::Reader
{
public: // Structors
  ///
  /// Constructs a reader over one read's context.
  /// \param context The context, which the reader hands to specialisations
  ///                and which must outlive the reader.
  ///
  explicit toml_reader(read_context& context);

public: // Accessors
  ///
  /// Returns the context of the read.
  /// \return The context.
  ///
  [[nodiscard]] auto context() const -> read_context&;

public: // Methods
  ///
  /// Reads a leaf as T. An integral T other than bool is read as a TOML
  /// integer that must fit T; every other T is read as the base reads it.
  ///
  /// \tparam T The type to read.
  /// \param var The node holding the value.
  /// \return The value, or the reason it does not fit.
  ///
  template<typename T>
  [[nodiscard]] auto to_basic_type(const InputVarType& var) const noexcept -> rfl::Result<T>;

  ///
  /// Reads an array element by element through reflect-cpp's array reader.
  /// The first element that fails fails the array, as in the base. Under
  /// newer_in()'s probe the elements after it are still read, with what they
  /// read as discarded, so that a newer version in any of them is recorded
  /// through note_newer(); see array_scan for the arrays this reaches.
  ///
  /// \tparam ArrayReader The array reader reflect-cpp hands in.
  /// \param array_reader The array reader, which collects the elements.
  /// \param arr The array.
  /// \return The first element's failure, or std::nullopt where all read.
  ///
  template<typename ArrayReader>
  [[nodiscard]] auto read_array(const ArrayReader& array_reader, const InputArrayType& arr) const noexcept
    -> std::optional<rfl::Error>;

private: // Variables
  ///
  /// The context of the read. Never null.
  ///
  read_context* context_;
};

///
/// The TOML integer a node holds, or why it holds none.
///
/// \param node The node. Not retained.
/// \return The integer, or the reason the node holds none.
///
[[nodiscard]] auto integer_of(const toml::node& node) -> std::expected<std::int64_t, std::string>;

///
/// The value narrowed to T, or the message saying it does not fit.
/// An unsigned T refuses a negative value rather than wrapping it, and a
/// value outside T's range says what T holds.
///
/// \tparam T The integer type to narrow to.
/// \param value The TOML integer, which is signed and 64-bit.
/// \return The value as T, or the reason it is not one.
///
template<std::integral T>
[[nodiscard]] auto narrowed(std::int64_t value) -> std::expected<T, std::string>;

///
///
template<typename R, typename W, typename VecType, typename ProcessorsType>
auto array_scan<rfl::parsing::VectorReader<R, W, VecType, ProcessorsType>>::read(
  const R& reader, const typename R::InputVarType& var, [[maybe_unused]] const std::size_t index) -> void
{
  auto scratch = VecType{};
  const auto element_reader = rfl::parsing::VectorReader<R, W, VecType, ProcessorsType>{&reader, &scratch};

  std::ignore = element_reader.read(var);
}

///
///
template<typename R, typename W, typename ProcessorsType, typename T, std::size_t Size>
auto array_scan<rfl::parsing::ArrayReader<R, W, ProcessorsType, T, Size>>::read(
  const R& reader, const typename R::InputVarType& var, [[maybe_unused]] const std::size_t index) -> void
{
  std::ignore = rfl::parsing::Parser<R, W, std::remove_cvref_t<T>, ProcessorsType>::read(reader, var);
}

///
///
template<
  typename R,
  typename W,
  typename TupleType,
  bool IgnoreEmptyContainers,
  bool AllRequired,
  typename ProcessorsType>
auto array_scan<rfl::parsing::TupleReader<R, W, TupleType, IgnoreEmptyContainers, AllRequired, ProcessorsType>>::read(
  const R& reader, const typename R::InputVarType& var, const std::size_t index) -> void
{
  // The position picks the element type at run time, so each type is
  // compared against it in turn.
  [&reader, &var, index]<std::size_t... I>(std::index_sequence<I...>) -> void
  {
    const auto read_at = [&reader, &var, index]<std::size_t Position>() -> void
    {
      if(index == Position)
      {
        using element_t = std::remove_cvref_t<rfl::tuple_element_t<Position, TupleType>>;

        std::ignore = rfl::parsing::Parser<R, W, element_t, ProcessorsType>::read(reader, var);
      }
    };

    (read_at.template operator()<I>(), ...);
  }(std::make_index_sequence<rfl::tuple_size_v<TupleType>>{});
}

///
///
template<typename T>
auto toml_reader::to_basic_type(const InputVarType& var) const noexcept -> rfl::Result<T>
{
  using value_t = std::remove_cvref_t<T>;

  if constexpr(std::integral<value_t> && !std::same_as<value_t, bool>)
  {
    const auto integer = integer_of(*var);

    if(!integer.has_value())
    {
      return rfl::error(integer.error());
    }

    const auto narrowed_value = narrowed<value_t>(*integer);

    if(!narrowed_value.has_value())
    {
      return rfl::error(narrowed_value.error());
    }

    return *narrowed_value;
  }
  else
  {
    return rfl::toml::Reader::to_basic_type<T>(var);
  }
}

///
///
template<typename ArrayReader>
auto toml_reader::read_array(const ArrayReader& array_reader, const InputArrayType& arr) const noexcept
  -> std::optional<rfl::Error>
{
  // The base stops at the first failure. The probe reads on, where the
  // array reader is one array_scan knows how to read past.
  const auto read_on = array_scan<ArrayReader>::known && context().probing();
  auto first_failure = std::optional<rfl::Error>{};
  auto index = std::size_t{0};

  for(auto& node : *arr)
  {
    if(!first_failure.has_value())
    {
      first_failure = array_reader.read(&node);
    }
    else if constexpr(array_scan<ArrayReader>::known)
    {
      array_scan<ArrayReader>::read(*this, &node, index);
    }

    if(first_failure.has_value() && !read_on)
    {
      break;
    }

    ++index;
  }

  return first_failure;
}

///
///
template<std::integral T>
auto narrowed(const std::int64_t value) -> std::expected<T, std::string>
{
  if constexpr(std::is_signed_v<T>)
  {
    if(!std::in_range<T>(value))
    {
      return std::unexpected{std::format(
        "the integer {} is out of range, as {} holds {} to {}.",
        value,
        type_name<T>(),
        std::numeric_limits<T>::min(),
        std::numeric_limits<T>::max())};
    }

    return static_cast<T>(value);
  }
  else
  {
    if(value < 0)
    {
      return std::unexpected{std::format("the integer {} is negative, where an unsigned one is expected.", value)};
    }

    if(!std::in_range<T>(value))
    {
      return std::unexpected{std::format(
        "the integer {} is out of range, as {} holds {} to {}.",
        value,
        type_name<T>(),
        std::numeric_limits<T>::min(),
        std::numeric_limits<T>::max())};
    }

    return static_cast<T>(value);
  }
}

} // namespace arude::config::detail
