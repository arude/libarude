///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
#pragma once

#include "arude/exception.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace arude::config::detail
{

///
/// The base64 alphabet, RFC 4648 section 4.
/// The standard alphabet rather than the URL-safe one: a configuration file is
/// not a URL, and this is what every other tool will assume when it reads the
/// value back.
///
inline constexpr auto base64_alphabet =
  std::string_view{"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"};

///
/// The character padding a final group of fewer than three bytes.
///
inline constexpr auto base64_pad = '=';

///
/// Table entry for a character outside the alphabet.
///
inline constexpr auto base64_invalid = std::int8_t{-1};

///
/// Builds the reverse lookup table used for decoding.
/// Every character outside the alphabet maps to base64_invalid, so decoding
/// costs one indexed load per character rather than a search. consteval
/// because calling this at run time would be a mistake: the table is a
/// constant, and the one instance below is all anyone needs.
///
/// \return The table, indexed by the numeric value of a character.
///
[[nodiscard]] consteval auto make_base64_decode_table() -> std::array<std::int8_t, 256>;

///
///
consteval auto make_base64_decode_table() -> std::array<std::int8_t, 256>
{
  auto table = std::array<std::int8_t, 256>{};
  table.fill(base64_invalid);

  for(auto index = decltype(base64_alphabet.size()){0}; index < base64_alphabet.size(); ++index)
  {
    table.at(static_cast<unsigned char>(base64_alphabet.at(index))) = static_cast<std::int8_t>(index);
  }

  return table;
}

///
/// The reverse lookup table used for decoding.
///
inline constexpr auto base64_decode_table = make_base64_decode_table();

///
/// One group of four base64 characters, decoded.
///
struct base64_group
{
  ///
  /// The 24 bits the group carries, in the low three bytes.
  ///
  std::uint32_t bits = 0;

  ///
  /// How many of the three bytes are padding rather than data, so zero, one or two.
  ///
  std::size_t padding = 0;
};

///
/// Decodes one group of four base64 characters.
///
/// \param group The four characters.
/// \param last Whether this is the last group of the text, which is the only one padding may appear in.
/// \return The bits the group carries, and how much of it was padding.
/// \throws arude::exception If the group holds a character outside the alphabet, or misplaced padding.
///
[[nodiscard]] constexpr auto decode_base64_group(std::string_view group, bool last) -> base64_group;

///
///
constexpr auto decode_base64_group(const std::string_view group, const bool last) -> base64_group
{
  auto decoded = base64_group{};

  for(const auto character : group)
  {
    if(character == base64_pad)
    {
      // Padding closes the last group and nothing else: at most two
      // characters, and never before the third position of that group.
      if(!last || decoded.padding == 2)
      {
        throw exception{"arude::config: base64 padding outside the last two characters of the text."};
      }

      ++decoded.padding;
      decoded.bits <<= 6U;
    }
    else if(decoded.padding != 0)
    {
      throw exception{"arude::config: base64 padding followed by further data."};
    }
    else
    {
      const auto value = base64_decode_table.at(static_cast<unsigned char>(character));

      if(value == base64_invalid)
      {
        throw exception{std::format("arude::config: '{}' is not a base64 character.", character)};
      }

      decoded.bits = (decoded.bits << 6U) | static_cast<std::uint32_t>(value);
    }
  }

  return decoded;
}

} // namespace arude::config::detail

namespace arude::config
{

///
/// The string type used for base64 text.
///
using base64_string_t = std::string;

///
/// The byte type used for base64 data.
///
using base64_byte_t = std::byte;

///
/// The byte owning container type used for base64 data.
///
using base64_bytes_t = std::vector<base64_byte_t>;

///
/// The size type used for base64 operations.
///
using base64_size_t = std::size_t;

///
/// The view type used for base64 text.
///
using base64_view_t = std::string_view;

///
/// The span type used for base64 data.
///
using base64_span_t = std::span<const base64_byte_t>;

///
/// Number of base64 characters a given number of bytes encodes to.
/// Counts the padding, so the result is always a multiple of four.
///
/// \param size Number of bytes to encode.
/// \return Length of the encoded text, in characters.
///
[[nodiscard]] constexpr auto base64_encoded_size(base64_size_t size) -> base64_size_t;

///
/// Number of bytes a base64 text decodes to.
/// Derived from the length and the trailing padding alone: the text is not
/// validated, so malformed input yields a plausible number rather than an
/// error. base64_decode() is what rejects such input.
///
/// \param text Encoded text. Not retained.
/// \return Number of bytes the text would decode to.
///
[[nodiscard]] constexpr auto base64_decoded_size(base64_view_t text) -> base64_size_t;

///
/// Encodes bytes as base64, per RFC 4648 section 4.
/// The output is padded and carries no line breaks.
///
/// \param data Bytes to encode.
/// \return The encoded text, its length a multiple of four.
///
[[nodiscard]] constexpr auto base64_encode(base64_span_t data) -> base64_string_t;

///
/// Decodes base64 text back into bytes.
/// Strict on purpose: the length must be a multiple of four, every character
/// must be in the alphabet, and padding may only close the final group. Whitespace and
/// line breaks are not accepted, because base64_encode() never produces any.
///
/// \param text Encoded text.
/// \return The decoded bytes.
/// \throws arude::exception If the text is not valid base64.
///
[[nodiscard]] constexpr auto base64_decode(base64_view_t text) -> base64_bytes_t;

///
///
constexpr auto base64_encoded_size(const base64_size_t size) -> base64_size_t
{
  return ((size + 2) / 3) * 4;
}

///
///
constexpr auto base64_decoded_size(const base64_view_t text) -> base64_size_t
{
  if(text.size() < 4)
  {
    return 0;
  }

  const auto groups = (text.size() / 4) * 3;

  if(text.ends_with("=="))
  {
    return groups - 2;
  }

  if(text.back() == detail::base64_pad)
  {
    return groups - 1;
  }

  return groups;
}

///
///
constexpr auto base64_encode(const base64_span_t data) -> base64_string_t
{
  auto text = base64_string_t{};
  text.reserve(base64_encoded_size(data.size()));

  // Three bytes become four characters. The last group is short where the
  // input does not divide by three, and the missing bytes are encoded as zero
  // bits and then written as padding rather than as alphabet characters.
  for(auto pos = decltype(data.size()){0}; pos < data.size(); pos += 3)
  {
    const auto remaining = data.size() - pos;
    const auto first = std::to_integer<std::uint32_t>(data[pos]);
    const auto second = remaining > 1 ? std::to_integer<std::uint32_t>(data[pos + 1]) : 0U;
    const auto third = remaining > 2 ? std::to_integer<std::uint32_t>(data[pos + 2]) : 0U;
    const auto group = (first << 16U) | (second << 8U) | third;

    text += detail::base64_alphabet.at((group >> 18U) & 0x3FU);
    text += detail::base64_alphabet.at((group >> 12U) & 0x3FU);
    text += remaining > 1 ? detail::base64_alphabet.at((group >> 6U) & 0x3FU) : detail::base64_pad;
    text += remaining > 2 ? detail::base64_alphabet.at(group & 0x3FU) : detail::base64_pad;
  }

  return text;
}

///
///
constexpr auto base64_decode(const base64_view_t text) -> base64_bytes_t
{
  if(text.size() % 4 != 0)
  {
    throw exception{
      std::format("arude::config: base64 text of length {} is not a multiple of four characters.", text.size())};
  }

  auto data = base64_bytes_t{};
  data.reserve(base64_decoded_size(text));

  // Four characters become three bytes, one fewer for each padding character
  // the last group carries.
  for(auto pos = decltype(text.size()){0}; pos < text.size(); pos += 4)
  {
    const auto group = detail::decode_base64_group(text.substr(pos, 4), pos + 4 == text.size());

    data.push_back(static_cast<base64_byte_t>((group.bits >> 16U) & 0xFFU));

    if(group.padding < 2)
    {
      data.push_back(static_cast<base64_byte_t>((group.bits >> 8U) & 0xFFU));
    }

    if(group.padding < 1)
    {
      data.push_back(static_cast<base64_byte_t>(group.bits & 0xFFU));
    }
  }

  return data;
}

} // namespace arude::config
