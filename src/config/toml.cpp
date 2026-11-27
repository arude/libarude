///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///

#include "arude/config/toml.hpp"
#include "arude/config/toml_reader.hpp"

#include <toml++/toml.hpp>

#include <format>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace arude::config::detail
{

///
///
auto parse_toml(const std::string_view text, const std::string_view source, const std::source_location loc)
  -> toml::table
{
  try
  {
    return toml::parse(text, source);
  }
  catch(...)
  {
    // toml++ reports text that is not TOML by throwing toml::parse_error,
    // which is not an arude::exception and would walk past every handler a
    // caller of this subsystem has.
    throw exception_t{
      std::format("arude::config: '{}' is not valid TOML: {}", source, nested_message()),
      config_error::invalid_payload,
      loc};
  }
}

///
///
auto render_toml(const toml::table& document) -> std::string
{
  auto stream = std::ostringstream{};
  stream << document;

  return std::move(stream).str();
}

///
///
auto raise_read_failure(
  const std::string_view source,
  const std::string_view type,
  const rfl::Error& error,
  const read_context& context,
  const std::source_location loc) -> void
{
  // The error text carries the field path and the recorded message, so the
  // invalid_version message names the member that is too new, and neither
  // the type nor the versions twice.
  if(context.code_for(error.what()) == config_error::invalid_version)
  {
    throw exception_t{
      std::format("arude::config: '{}' cannot be loaded: {}", source, without_prefix(error.what())),
      config_error::invalid_version,
      loc};
  }

  throw exception_t{
    std::format("arude::config: '{}' is not a valid {}: {}", source, type, without_prefix(error.what())),
    config_error::invalid_payload,
    loc};
}

///
///
auto raise_write_failure(const std::string_view type, const std::string_view message, const std::source_location loc)
  -> void
{
  throw exception_t{
    std::format("arude::config: the value is not a valid {}: {}", type, message), config_error::invalid_payload, loc};
}

} // namespace arude::config::detail

namespace arude::config
{

///
///
auto toml_version(const std::string_view text, const std::source_location loc) -> version_t
{
  const auto document = detail::parse_toml(text, detail::text_source, loc);
  const auto* const version_node = document.get("version");

  auto reason = std::string{};

  if(version_node == nullptr)
  {
    reason = "the version key is absent.";
  }
  else
  {
    const auto integer = detail::integer_of(*version_node);

    if(!integer.has_value())
    {
      reason = integer.error();
    }
    else
    {
      const auto version = detail::narrowed<version_t>(*integer);

      if(!version.has_value())
      {
        reason = version.error();
      }
      else
      {
        return *version;
      }
    }
  }

  throw exception_t{
    std::format("arude::config: '{}' declares no configuration version: {}", detail::text_source, reason),
    config_error::invalid_payload,
    loc};
}

} // namespace arude::config
