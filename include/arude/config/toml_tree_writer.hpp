///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
#pragma once

#include "arude/config/tree_writer.hpp"
#include "arude/non_owning_t.hpp"

#include <rfl/toml/Writer.hpp>
#include <toml++/toml.hpp>

#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

namespace arude::config
{

///
/// The TOML endpoint's tree_writer: tables and arrays made by toml++, each
/// leaf written by reflect-cpp's own TOML writer, so a section writes a value
/// exactly as rfl::toml::write would.
///
/// A branch is the address of a toml::node. Public, so that an endpoint
/// wrapping endpoint_toml can name what its make_writer() returns.
///
class toml_tree_writer final : public tree_writer
{
public: // Structors
  ///
  /// Constructs a writer over a table.
  /// \param document The table. Retained, and must outlive this.
  ///
  explicit toml_tree_writer(toml::table& document);

public: // Methods
  ///
  /// \see tree_writer::root
  ///
  [[nodiscard]] auto root() -> tree_branch override;

  ///
  /// \see tree_writer::table
  ///
  [[nodiscard]] auto table(tree_branch parent, std::string_view key) -> tree_branch override;

  ///
  /// \see tree_writer::erase
  ///
  auto erase(tree_branch parent, std::string_view key) -> void override;

  ///
  /// \see tree_writer::add_table
  ///
  [[nodiscard]] auto add_table(tree_branch parent, std::string_view key) -> tree_branch override;

  ///
  /// \see tree_writer::add_array
  ///
  [[nodiscard]] auto add_array(tree_branch parent, std::string_view key) -> tree_branch override;

  ///
  /// \see tree_writer::append_table
  ///
  [[nodiscard]] auto append_table(tree_branch parent) -> tree_branch override;

  ///
  /// \see tree_writer::append_array
  ///
  [[nodiscard]] auto append_array(tree_branch parent) -> tree_branch override;

  ///
  /// \see tree_writer::add_value
  /// An unsigned integer goes to toml++ as one, which stores it as a signed
  /// 64-bit integer, as rfl::toml::write does: one above INT64_MAX wraps.
  ///
  auto add_value(tree_branch parent, std::string_view key, const tree_scalar_t& value) -> void override;

  ///
  /// \see tree_writer::append_value
  ///
  auto append_value(tree_branch parent, const tree_scalar_t& value) -> void override;

  ///
  /// \see tree_writer::add_null
  /// TOML has no null; this writes an empty string, as rfl::toml::write does.
  ///
  auto add_null(tree_branch parent, std::string_view key) -> void override;

  ///
  /// \see tree_writer::append_null
  /// TOML has no null; this appends an empty string, as rfl::toml::write does.
  ///
  auto append_null(tree_branch parent) -> void override;

private: // Helpers
  ///
  /// Returns the table a branch names.
  /// \param branch The branch.
  /// \return The table.
  ///
  [[nodiscard]] static auto table_of(tree_branch branch) -> toml::table&;

  ///
  /// Returns the array a branch names.
  /// \param branch The branch.
  /// \return The array.
  ///
  [[nodiscard]] static auto array_of(tree_branch branch) -> toml::array&;

  ///
  /// Returns the branch of a node.
  /// \param node The node.
  /// \return The branch.
  ///
  [[nodiscard]] static auto branch_of(toml::node& node) -> tree_branch;

  ///
  /// Returns reflect-cpp's TOML writer, for the leaves.
  /// \return The writer.
  ///
  [[nodiscard]] auto leaf_writer() const -> rfl::toml::Writer;

private: // Variables
  non_owning_t<toml::table> document_;
};

///
///
inline toml_tree_writer::toml_tree_writer(toml::table& document)
  : document_{&document}
{
}

///
///
inline auto toml_tree_writer::root() -> tree_branch
{
  return branch_of(*document_);
}

///
///
inline auto toml_tree_writer::table(const tree_branch parent, const std::string_view key) -> tree_branch
{
  auto& holder = table_of(parent);

  if(auto* const found = holder.get(key); found != nullptr && found->is_table())
  {
    return branch_of(*found);
  }

  return branch_of(holder.insert_or_assign(key, toml::table{}).first->second);
}

///
///
inline auto toml_tree_writer::erase(const tree_branch parent, const std::string_view key) -> void
{
  table_of(parent).erase(key);
}

///
///
// The containers are made here rather than by rfl::toml::Writer, whose
// add_object_to_object() and add_array_to_object() find the node they just
// added again with at_path(). That reads a dot or a bracket in the key as path
// syntax, finds nothing under a key such as "example.com", and the next write
// goes through a null pointer.
inline auto toml_tree_writer::add_table(const tree_branch parent, const std::string_view key) -> tree_branch
{
  return branch_of(table_of(parent).insert_or_assign(key, toml::table{}).first->second);
}

///
///
inline auto toml_tree_writer::add_array(const tree_branch parent, const std::string_view key) -> tree_branch
{
  return branch_of(table_of(parent).insert_or_assign(key, toml::array{}).first->second);
}

///
///
inline auto toml_tree_writer::append_table(const tree_branch parent) -> tree_branch
{
  auto& holder = array_of(parent);
  holder.push_back(toml::table{});

  return branch_of(holder.back());
}

///
///
inline auto toml_tree_writer::append_array(const tree_branch parent) -> tree_branch
{
  auto& holder = array_of(parent);
  holder.push_back(toml::array{});

  return branch_of(holder.back());
}

///
///
inline auto
toml_tree_writer::add_value(const tree_branch parent, const std::string_view key, const tree_scalar_t& value) -> void
{
  auto object = rfl::toml::Writer::TOMLObject{.val_ = &table_of(parent)};

  std::visit(
    [this, key, &object]<typename T>(const T& leaf) -> void
    {
      if constexpr(std::is_same_v<T, std::string_view>)
      {
        leaf_writer().add_value_to_object(key, std::string{leaf}, &object);
      }
      else
      {
        leaf_writer().add_value_to_object(key, leaf, &object);
      }
    },
    value);
}

///
///
inline auto toml_tree_writer::append_value(const tree_branch parent, const tree_scalar_t& value) -> void
{
  auto array = rfl::toml::Writer::TOMLArray{.val_ = &array_of(parent)};

  std::visit(
    [this, &array]<typename T>(const T& leaf) -> void
    {
      if constexpr(std::is_same_v<T, std::string_view>)
      {
        leaf_writer().add_value_to_array(std::string{leaf}, &array);
      }
      else
      {
        leaf_writer().add_value_to_array(leaf, &array);
      }
    },
    value);
}

///
///
inline auto toml_tree_writer::add_null(const tree_branch parent, const std::string_view key) -> void
{
  auto object = rfl::toml::Writer::TOMLObject{.val_ = &table_of(parent)};

  leaf_writer().add_null_to_object(key, &object);
}

///
///
inline auto toml_tree_writer::append_null(const tree_branch parent) -> void
{
  auto array = rfl::toml::Writer::TOMLArray{.val_ = &array_of(parent)};

  leaf_writer().add_null_to_array(&array);
}

///
///
inline auto toml_tree_writer::table_of(const tree_branch branch) -> toml::table&
{
  return *static_cast<toml::node*>(branch.address)->as_table();
}

///
///
inline auto toml_tree_writer::array_of(const tree_branch branch) -> toml::array&
{
  return *static_cast<toml::node*>(branch.address)->as_array();
}

///
///
inline auto toml_tree_writer::branch_of(toml::node& node) -> tree_branch
{
  return tree_branch{.address = &node};
}

///
///
inline auto toml_tree_writer::leaf_writer() const -> rfl::toml::Writer
{
  // The root is never used by the leaf operations, which write into the
  // table or array they are handed.
  return rfl::toml::Writer{document_};
}

} // namespace arude::config
