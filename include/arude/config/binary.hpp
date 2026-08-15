///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
#pragma once

#include "arude/config/base64.hpp"

#include <rfl.hpp>

#include <concepts>
#include <cstddef>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace arude::config
{

///
/// A range of std::byte that can be copied out with a pair of iterators.
/// common_range is part of the requirement because that is what the container
/// constructor below needs; it holds for every array, vector, span, and
/// initializer_list of bytes, and excludes only the view types whose sentinel
/// is not an iterator.
///
/// \tparam R Range type.
///
template<typename R>
concept byte_range_c =
  std::ranges::input_range<R> && std::ranges::common_range<R> && std::same_as<std::ranges::range_value_t<R>, std::byte>;

///
/// An opaque run of bytes, stored in a configuration file as base64 text.
/// TOML has no binary type, so bytes have to be spelled as something a TOML
/// document can hold. base64 is that something: it survives a round trip
/// through a text file unchanged, stays on one line, and is what a reader
/// coming from any other language will expect.
///
/// Declare a member of this type rather than a std::vector<std::byte>; the
/// reflect-cpp reflector that performs the encoding is attached to this type,
/// and a plain byte container has no place to carry it. Any byte_range_c
/// converts to one.
///
class binary final
{
public: // Typedefs
  using bytes_t = std::vector<std::byte>;
  using string_t = std::string;

public: // Structors / Operators
  ///
  /// Constructs an empty payload.
  ///
  binary() = default;

  ///
  /// Constructs a payload from a byte container, taking ownership of it.
  /// \param bytes Bytes to hold.
  ///
  explicit constexpr binary(bytes_t bytes);

  ///
  /// Constructs a payload by copying any range of bytes.
  /// The range is copied and not retained, so a payload built from a span
  /// outlives whatever the span pointed at.
  ///
  /// \param range Bytes to copy.
  ///
  explicit constexpr binary(const byte_range_c auto& range);

  ///
  /// Compares two payloads by their bytes.
  ///
  /// \param other Payload to compare against.
  /// \return true if both hold the same bytes in the same order.
  ///
  [[nodiscard]] constexpr auto operator==(const binary& other) const -> bool = default;

public: // Accessors
  ///
  /// Returns the bytes held.
  /// \return The bytes, in order.
  ///
  [[nodiscard]] constexpr auto bytes() const -> const bytes_t&;

  ///
  /// Replaces the bytes held.
  /// \param val Bytes to hold.
  ///
  constexpr auto bytes(bytes_t val) -> void;

  ///
  /// Returns the bytes held, encoded as base64.
  /// This is the form written to a configuration file.
  ///
  /// \return The encoded text, its length a multiple of four.
  ///
  [[nodiscard]] constexpr auto base64() const -> string_t;

  ///
  /// Replaces the bytes held with the decoding of base64 text.
  /// Strongly exception safe: the payload is left as it was if the text does
  /// not decode.
  ///
  /// \param text Encoded text. Not retained.
  /// \throws arude::exception If the text is not valid base64.
  ///
  constexpr auto base64(std::string_view text) -> void;

  ///
  /// Returns the number of bytes held.
  /// \return The number of bytes.
  ///
  [[nodiscard]] constexpr auto size() const -> std::size_t;

  ///
  /// Reports whether any bytes are held.
  /// \return true if the payload is empty.
  ///
  [[nodiscard]] constexpr auto empty() const -> bool;

private: // Variables
  bytes_t bytes_;
};

///
///
constexpr binary::binary(bytes_t bytes)
  : bytes_{std::move(bytes)}
{
}

///
///
constexpr binary::binary(const byte_range_c auto& range)
  : bytes_{std::ranges::begin(range), std::ranges::end(range)}
{
}

///
///
constexpr auto binary::bytes() const -> const bytes_t&
{
  return bytes_;
}

///
///
constexpr auto binary::bytes(bytes_t val) -> void
{
  bytes_ = std::move(val);
}

///
///
constexpr auto binary::base64() const -> string_t
{
  return base64_encode(bytes_);
}

///
///
constexpr auto binary::base64(const std::string_view text) -> void
{
  // Decoded into a temporary first, so a throw from base64_decode() leaves the
  // payload untouched rather than half replaced.
  auto decoded = base64_decode(text);
  bytes_ = std::move(decoded);
}

///
///
constexpr auto binary::size() const -> std::size_t
{
  return bytes_.size();
}

///
///
constexpr auto binary::empty() const -> bool
{
  return bytes_.empty();
}

} // namespace arude::config

namespace rfl
{

///
/// reflect-cpp reflector for arude::config::binary.
/// reflect-cpp reflects public data members, and arude::config::binary has
/// none to reflect: it holds its bytes privately and there is no text format
/// that could store them raw. This reflector is what stands in, mapping the
/// payload to and from a single base64 string.
///
template<>
struct Reflector<arude::config::binary>
{
  // reflect-cpp finds this member type by name, so it keeps reflect-cpp's
  // spelling rather than the one docs/cpp-conventions.md asks for.
  // NOLINTNEXTLINE(readability-identifier-naming)
  using ReflType = std::string;

  ///
  /// Builds a payload from the text read out of a configuration file.
  ///
  /// \param val Base64 text.
  /// \return The decoded payload.
  /// \throws arude::exception If the text is not valid base64.
  ///
  [[nodiscard]] static constexpr auto to(const ReflType& val) -> arude::config::binary;

  ///
  /// Renders a payload as the text written to a configuration file.
  ///
  /// \param val Payload to render.
  /// \return The base64 text.
  ///
  [[nodiscard]] static constexpr auto from(const arude::config::binary& val) -> ReflType;
};

///
///
constexpr auto Reflector<arude::config::binary>::to(const ReflType& val) -> arude::config::binary
{
  auto payload = arude::config::binary{};
  payload.base64(val);

  return payload;
}

///
///
constexpr auto Reflector<arude::config::binary>::from(const arude::config::binary& val) -> ReflType
{
  return val.base64();
}

} // namespace rfl
