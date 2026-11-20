///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///

#include "arude/config/toml_writer.hpp"

#include <toml++/toml.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace arude::config::detail
{

///
///
auto write_context::fail(std::string message) -> void
{
  if(!failure_.has_value())
  {
    failure_ = std::move(message);
  }
}

///
///
auto write_context::failure() const -> const std::optional<std::string>&
{
  return failure_;
}

///
///
toml_writer::toml_writer(toml::table& root, write_context& context)
  : rfl::toml::Writer{&root}
  , context_{&context}
{
}

///
///
auto toml_writer::context() const -> write_context&
{
  return *context_;
}

///
///
// NOLINTNEXTLINE(readability-convert-member-functions-to-static): hides a base member, so it is an instance member.
auto toml_writer::add_array_to_object(
  const std::string_view name, [[maybe_unused]] const std::size_t size, OutputObjectType* const parent) const noexcept
  -> OutputArrayType
{
  if(!is_valid_utf8(name))
  {
    context_->fail(std::format("the key '{}' is not valid UTF-8, which TOML cannot hold.", name));
  }

  // insert_or_assign() keeps the key as one key, however many dots it holds;
  // at_path(), which the base uses to find the node again, would read them
  // as path separators and find nothing.
  return OutputArrayType{parent->val_->insert_or_assign(name, toml::array{}).first->second.as_array()};
}

///
///
// NOLINTNEXTLINE(readability-convert-member-functions-to-static): hides a base member, so it is an instance member.
auto toml_writer::add_object_to_object(
  const std::string_view name, [[maybe_unused]] const std::size_t size, OutputObjectType* const parent) const noexcept
  -> OutputObjectType
{
  if(!is_valid_utf8(name))
  {
    context_->fail(std::format("the key '{}' is not valid UTF-8, which TOML cannot hold.", name));
  }

  // \see add_array_to_object for why the base member is not used.
  return OutputObjectType{parent->val_->insert_or_assign(name, toml::table{}).first->second.as_table()};
}

///
///
auto toml_writer::add_null_to_array([[maybe_unused]] OutputArrayType* const parent) const noexcept -> OutputVarType
{
  context_->fail("TOML has no null, and an empty value in an array cannot be written.");

  return {};
}

///
///
auto toml_writer::add_null_to_object(
  [[maybe_unused]] const std::string_view name, [[maybe_unused]] OutputObjectType* const parent) const noexcept
  -> OutputVarType
{
  context_->fail("TOML has no null, and an empty value in a map entry cannot be written.");

  return {};
}

///
///
auto is_valid_utf8(const std::string_view text) -> bool
{
  static constexpr auto is_continuation = [](const unsigned char byte) -> bool { return byte >= 0x80 && byte <= 0xBF; };

  for(auto i = std::size_t{0}; i < text.size();)
  {
    const auto lead = static_cast<unsigned char>(text[i]);

    if(lead < 0x80)
    {
      ++i;
      continue;
    }

    // The lead byte decides how many continuations must follow. C0, C1 and
    // the five- and six-byte leads are refused outright; the per-lead
    // minimum and maximum then keep the decoded code point in range, out
    // of the surrogate block, and free of overlong spellings.
    auto length = std::size_t{0};
    auto minimum = std::uint32_t{0};
    auto maximum = std::uint32_t{0};

    if(lead >= 0xC2 && lead <= 0xDF)
    {
      length = 1;
      minimum = 0x80;
      maximum = 0x7FF;
    }
    else if(lead >= 0xE0 && lead <= 0xEF)
    {
      length = 2;
      minimum = 0x800;
      maximum = 0xFFFF;
    }
    else if(lead >= 0xF0 && lead <= 0xF4)
    {
      length = 3;
      minimum = 0x10000;
      maximum = 0x10FFFF;
    }
    else
    {
      return false;
    }

    if(i + length >= text.size())
    {
      return false;
    }

    auto value = std::uint32_t{lead & (0xFFU >> (length + 2))};

    for(auto extra = std::size_t{1}; extra <= length; ++extra)
    {
      const auto byte = static_cast<unsigned char>(text[i + extra]);

      if(!is_continuation(byte))
      {
        return false;
      }

      value = (value << 6) | (byte & 0x3FU);
    }

    if(value < minimum || value > maximum || (value >= 0xD800 && value <= 0xDFFF))
    {
      return false;
    }

    i += length + 1;
  }

  return true;
}

} // namespace arude::config::detail
