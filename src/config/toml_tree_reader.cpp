///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///

#include "arude/config/toml_tree_reader.hpp"
#include "arude/config/tree_reader.hpp"

#include <toml++/toml.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

namespace arude::config
{

///
///
toml_tree_reader::toml_tree_reader(const toml::table& document, const tree_read_options options)
  : document_{&document}
  , options_{options}
{
}

///
///
auto toml_tree_reader::root() const -> tree_node
{
  return handle_of(document_);
}

///
///
auto toml_tree_reader::kind(const tree_node node) const -> node_kind
{
  const auto* const found = node_of(node);

  if(found->is_table())
  {
    return node_kind::table;
  }

  if(found->is_array())
  {
    return node_kind::array;
  }

  return node_kind::value;
}

///
///
auto toml_tree_reader::field(const std::string_view key, const tree_node table) const -> std::optional<tree_node>
{
  const auto* const found = node_of(table)->as_table();

  if(found == nullptr)
  {
    return std::nullopt;
  }

  // get() looks the key up as a key; at_path() would read a dot in it as a
  // separator and find a different node, or none.
  if(const auto* const value = found->get(key); value != nullptr)
  {
    return handle_of(value);
  }

  return std::nullopt;
}

///
///
auto toml_tree_reader::element(const std::size_t index, const tree_node array) const -> std::optional<tree_node>
{
  const auto* const found = node_of(array)->as_array();

  if(found == nullptr)
  {
    return std::nullopt;
  }

  if(const auto* const value = found->get(index); value != nullptr)
  {
    return handle_of(value);
  }

  return std::nullopt;
}

///
///
auto toml_tree_reader::for_each_field(const tree_node table, const field_visitor& visitor) const -> void
{
  const auto* const found = node_of(table)->as_table();

  if(found == nullptr)
  {
    return;
  }

  for(const auto& [key, value] : *found)
  {
    visitor.visit(key.str(), handle_of(&value));
  }
}

///
///
auto toml_tree_reader::for_each_element(const tree_node array, const element_visitor& visitor) const -> void
{
  const auto* const found = node_of(array)->as_array();

  if(found == nullptr)
  {
    return;
  }

  for(const auto& value : *found)
  {
    if(!visitor.visit(handle_of(&value)))
    {
      return;
    }
  }
}

///
///
auto toml_tree_reader::to_string(const tree_node node) const -> std::expected<std::string, std::string>
{
  return converted<std::string>(node);
}

///
///
auto toml_tree_reader::to_bool(const tree_node node) const -> std::expected<bool, std::string>
{
  return converted<bool>(node);
}

///
///
auto toml_tree_reader::to_int64(const tree_node node) const -> std::expected<std::int64_t, std::string>
{
  return converted<std::int64_t>(node);
}

///
///
auto toml_tree_reader::to_uint64(const tree_node node) const -> std::expected<std::uint64_t, std::string>
{
  return converted<std::uint64_t>(node);
}

///
///
auto toml_tree_reader::to_double(const tree_node node) const -> std::expected<double, std::string>
{
  return converted<double>(node);
}

///
///
auto toml_tree_reader::options() const -> tree_read_options
{
  return options_;
}

///
///
auto toml_tree_reader::node_of(const tree_node node) -> const toml::node*
{
  return static_cast<const toml::node*>(node.address);
}

///
///
auto toml_tree_reader::handle_of(const toml::node* const node) -> tree_node
{
  return tree_node{.address = node};
}

} // namespace arude::config
