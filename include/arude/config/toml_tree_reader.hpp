///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
#pragma once

#include "arude/config/tree_reader.hpp"
#include "arude/non_owning_t.hpp"

#include <rfl/toml/Reader.hpp>
#include <toml++/toml.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace arude::config
{

///
/// The TOML endpoint's tree_reader: toml++'s nodes, with each leaf converted
/// by reflect-cpp's own TOML reader, so a section reads a value exactly as
/// rfl::toml::read would, with the same messages where it does not fit.
///
/// A handle is the address of a toml::node, and a table or an array is one.
/// Every date and time kind, and every other TOML value, is a
/// node_kind::value. Public, so that an endpoint wrapping endpoint_toml can
/// name what its make_reader() returns.
///
class toml_tree_reader final : public tree_reader
{
public: // Structors
  ///
  /// Constructs a reader over a table.
  ///
  /// \param document The table. Retained, and must outlive this.
  /// \param options What every section read through this reader is asked. Copied.
  ///
  toml_tree_reader(const toml::table& document, tree_read_options options);

public: // Accessors
  ///
  /// \see tree_reader::root
  ///
  [[nodiscard]] auto root() const -> tree_node override;

  ///
  /// \see tree_reader::kind
  ///
  [[nodiscard]] auto kind(tree_node node) const -> node_kind override;

  ///
  /// \see tree_reader::field
  ///
  [[nodiscard]] auto field(std::string_view key, tree_node table) const -> std::optional<tree_node> override;

  ///
  /// \see tree_reader::element
  ///
  [[nodiscard]] auto element(std::size_t index, tree_node array) const -> std::optional<tree_node> override;

  ///
  /// \see tree_reader::for_each_field
  ///
  auto for_each_field(tree_node table, const field_visitor& visitor) const -> void override;

  ///
  /// \see tree_reader::for_each_element
  ///
  auto for_each_element(tree_node array, const element_visitor& visitor) const -> void override;

  ///
  /// \see tree_reader::to_string
  ///
  [[nodiscard]] auto to_string(tree_node node) const -> std::expected<std::string, std::string> override;

  ///
  /// \see tree_reader::to_bool
  ///
  [[nodiscard]] auto to_bool(tree_node node) const -> std::expected<bool, std::string> override;

  ///
  /// \see tree_reader::to_int64
  ///
  [[nodiscard]] auto to_int64(tree_node node) const -> std::expected<std::int64_t, std::string> override;

  ///
  /// \see tree_reader::to_uint64
  /// TOML has no unsigned integers: this is the signed one, cast, as
  /// rfl::toml::read does it, so a negative number wraps.
  ///
  [[nodiscard]] auto to_uint64(tree_node node) const -> std::expected<std::uint64_t, std::string> override;

  ///
  /// \see tree_reader::to_double
  ///
  [[nodiscard]] auto to_double(tree_node node) const -> std::expected<double, std::string> override;

  ///
  /// \see tree_reader::options
  ///
  [[nodiscard]] auto options() const -> tree_read_options override;

private: // Helpers
  ///
  /// Returns the node a handle names.
  /// \param node The handle.
  /// \return The node.
  ///
  [[nodiscard]] static auto node_of(tree_node node) -> const toml::node*;

  ///
  /// Returns the handle of a node.
  /// \param node The node.
  /// \return The handle.
  ///
  [[nodiscard]] static auto handle_of(const toml::node* node) -> tree_node;

  ///
  /// Converts a leaf through rfl::toml::Reader, keeping its message.
  ///
  /// \tparam T The type to convert to.
  /// \param node The leaf.
  /// \return The value, or the reader's message.
  ///
  template<typename T>
  [[nodiscard]] static auto converted(tree_node node) -> std::expected<T, std::string>;

private: // Variables
  non_owning_t<const toml::table> document_;
  tree_read_options options_;
};

///
///
inline toml_tree_reader::toml_tree_reader(const toml::table& document, const tree_read_options options)
  : document_{&document}
  , options_{options}
{
}

///
///
inline auto toml_tree_reader::root() const -> tree_node
{
  return handle_of(document_);
}

///
///
inline auto toml_tree_reader::kind(const tree_node node) const -> node_kind
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
inline auto toml_tree_reader::field(const std::string_view key, const tree_node table) const -> std::optional<tree_node>
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
inline auto toml_tree_reader::element(const std::size_t index, const tree_node array) const -> std::optional<tree_node>
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
inline auto toml_tree_reader::for_each_field(const tree_node table, const field_visitor& visitor) const -> void
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
inline auto toml_tree_reader::for_each_element(const tree_node array, const element_visitor& visitor) const -> void
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
inline auto toml_tree_reader::to_string(const tree_node node) const -> std::expected<std::string, std::string>
{
  return converted<std::string>(node);
}

///
///
inline auto toml_tree_reader::to_bool(const tree_node node) const -> std::expected<bool, std::string>
{
  return converted<bool>(node);
}

///
///
inline auto toml_tree_reader::to_int64(const tree_node node) const -> std::expected<std::int64_t, std::string>
{
  return converted<std::int64_t>(node);
}

///
///
inline auto toml_tree_reader::to_uint64(const tree_node node) const -> std::expected<std::uint64_t, std::string>
{
  return converted<std::uint64_t>(node);
}

///
///
inline auto toml_tree_reader::to_double(const tree_node node) const -> std::expected<double, std::string>
{
  return converted<double>(node);
}

///
///
inline auto toml_tree_reader::options() const -> tree_read_options
{
  return options_;
}

///
///
inline auto toml_tree_reader::node_of(const tree_node node) -> const toml::node*
{
  return static_cast<const toml::node*>(node.address);
}

///
///
inline auto toml_tree_reader::handle_of(const toml::node* const node) -> tree_node
{
  return tree_node{.address = node};
}

///
///
template<typename T>
auto toml_tree_reader::converted(const tree_node node) -> std::expected<T, std::string>
{
  // rfl::toml::Reader takes a toml::node* where a pointer to const would do:
  // to_basic_type() only calls as<>() on it and dereferences what that
  // returns, so the node is never written through the pointer.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
  auto* const writable = const_cast<toml::node*>(node_of(node));

  auto result = rfl::toml::Reader{}.to_basic_type<T>(writable);

  if(!result.has_value())
  {
    return std::unexpected{result.error().what()};
  }

  return std::move(result).value();
}

} // namespace arude::config
