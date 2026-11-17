///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///

#include "arude/config/toml_tree_writer.hpp"
#include "arude/config/tree_writer.hpp"

#include <rfl/toml/Writer.hpp>
#include <toml++/toml.hpp>

#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

namespace arude::config
{

///
///
toml_tree_writer::toml_tree_writer(toml::table& document)
  : document_{&document}
{
}

///
///
auto toml_tree_writer::root() -> tree_branch
{
  return branch_of(*document_);
}

///
///
auto toml_tree_writer::table(const tree_branch parent, const std::string_view key) -> tree_branch
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
auto toml_tree_writer::erase(const tree_branch parent, const std::string_view key) -> void
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
auto toml_tree_writer::add_table(const tree_branch parent, const std::string_view key) -> tree_branch
{
  return branch_of(table_of(parent).insert_or_assign(key, toml::table{}).first->second);
}

///
///
auto toml_tree_writer::add_array(const tree_branch parent, const std::string_view key) -> tree_branch
{
  return branch_of(table_of(parent).insert_or_assign(key, toml::array{}).first->second);
}

///
///
auto toml_tree_writer::append_table(const tree_branch parent) -> tree_branch
{
  auto& holder = array_of(parent);
  holder.push_back(toml::table{});

  return branch_of(holder.back());
}

///
///
auto toml_tree_writer::append_array(const tree_branch parent) -> tree_branch
{
  auto& holder = array_of(parent);
  holder.push_back(toml::array{});

  return branch_of(holder.back());
}

///
///
auto toml_tree_writer::add_value(const tree_branch parent, const std::string_view key, const tree_scalar_t& value)
  -> void
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
auto toml_tree_writer::append_value(const tree_branch parent, const tree_scalar_t& value) -> void
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
auto toml_tree_writer::add_null(const tree_branch parent, const std::string_view key) -> void
{
  auto object = rfl::toml::Writer::TOMLObject{.val_ = &table_of(parent)};

  leaf_writer().add_null_to_object(key, &object);
}

///
///
auto toml_tree_writer::append_null(const tree_branch parent) -> void
{
  auto array = rfl::toml::Writer::TOMLArray{.val_ = &array_of(parent)};

  leaf_writer().add_null_to_array(&array);
}

///
///
auto toml_tree_writer::table_of(const tree_branch branch) -> toml::table&
{
  return *static_cast<toml::node*>(branch.address)->as_table();
}

///
///
auto toml_tree_writer::array_of(const tree_branch branch) -> toml::array&
{
  return *static_cast<toml::node*>(branch.address)->as_array();
}

///
///
auto toml_tree_writer::branch_of(toml::node& node) -> tree_branch
{
  return tree_branch{.address = &node};
}

///
///
auto toml_tree_writer::leaf_writer() const -> rfl::toml::Writer
{
  // The root is never used by the leaf operations, which write into the
  // table or array they are handed.
  return rfl::toml::Writer{document_};
}

} // namespace arude::config
