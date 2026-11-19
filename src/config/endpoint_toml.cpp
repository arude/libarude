///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///

#include "arude/config/endpoint_toml.hpp"
#include "arude/config/common.hpp"
#include "arude/config/endpoint.hpp"
#include "arude/config/migration.hpp"
#include "arude/config/toml_tree_reader.hpp"
#include "arude/config/toml_tree_writer.hpp"
#include "arude/config/tree_reader.hpp"

#include <toml++/toml.hpp>

#include <format>
#include <source_location>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace arude::config::detail
{

///
///
auto read_toml_document(const std::string_view text, const std::string_view source, const std::source_location loc)
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
      std::format("arude::config: '{}' is not valid TOML: {}", source, detail::nested_message()),
      config_error::invalid_payload,
      loc};
  }
}

///
///
auto write_toml_document(const toml::table& document) -> std::string
{
  auto stream = std::ostringstream{};
  stream << document;

  return std::move(stream).str();
}

} // namespace arude::config::detail

namespace arude::config
{

///
///
endpoint_toml::endpoint_toml(const endpoint_toml_options options)
  : options_{options}
{
}

///
///
auto endpoint_toml::make_reader(const document_t& document) const -> toml_tree_reader
{
  return toml_tree_reader{document, tree_read_options{.reject_unknown_fields = options_.reject_unknown_fields}};
}

///
///
// NOLINTNEXTLINE(readability-convert-member-functions-to-static): the contract is an instance operation.
auto endpoint_toml::make_writer(document_t& document) const -> toml_tree_writer
{
  return toml_tree_writer{document};
}

///
///
auto toml_version(const std::string_view text, const std::source_location loc) -> version_t
{
  const auto document = detail::read_toml_document(text, detail::text_source, loc);
  const auto tree = endpoint_toml{}.make_reader(document);

  try
  {
    return detail::read_tree<detail::endpoint_probe>(tree, tree.root(), {}, false).version;
  }
  catch(...)
  {
    throw exception_t{
      std::format(
        "arude::config: '{}' declares no configuration version: {}", detail::text_source, detail::nested_message()),
      config_error::invalid_payload,
      loc};
  }
}

} // namespace arude::config
