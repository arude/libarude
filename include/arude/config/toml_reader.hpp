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
#pragma once

#include "arude/config/common.hpp"
#include "arude/config/load_policy.hpp"
#include "arude/type_name.hpp"

#include <rfl/Result.hpp>
#include <rfl/toml/Reader.hpp>
#include <toml++/toml.hpp>

#include <concepts>
#include <cstdint>
#include <expected>
#include <format>
#include <limits>
#include <string>
#include <string_view>
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
/// from an alternative that failed the read, because versioned<> cannot
/// appear inside a variant alternative (see variant_with_versioned), so the
/// message that decided the failure is always in the final error text.
///
class read_context final
{
public: // Structors
  ///
  /// Constructs a context for one read.
  /// \param policy The load policy the read runs under.
  ///
  explicit read_context(load_policy policy);

public: // Accessors
  ///
  /// Returns the policy the read runs under.
  /// \return The policy.
  ///
  [[nodiscard]] auto policy() const -> load_policy;

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
  /// Failures recorded so far, in the order they were recorded.
  ///
  std::vector<failure> failures_;
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
