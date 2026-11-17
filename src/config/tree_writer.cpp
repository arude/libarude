///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///

#include "arude/config/tree_writer.hpp"
#include "arude/config/common.hpp"

#include <cstddef>
#include <exception>
#include <string_view>

namespace arude::config::detail
{

///
///
rfl_tree_writer::rfl_tree_writer(
  tree_writer& tree, const tree_branch target, key_set_t& written, std::exception_ptr& failure)
  : tree_{&tree}
  , target_{target}
  , written_{&written}
  , failure_{&failure}
{
}

///
///
auto rfl_tree_writer::object_as_root([[maybe_unused]] const std::size_t size) const noexcept -> OutputObjectType
{
  return {.branch = target_, .top = true};
}

///
///
auto rfl_tree_writer::add_array_to_array(
  [[maybe_unused]] const std::size_t size, OutputArrayType* const parent) const noexcept -> OutputArrayType
{
  return shielded<OutputArrayType>(
    [this, parent] -> OutputArrayType { return {.branch = tree_->append_array(parent->branch)}; });
}

///
///
auto rfl_tree_writer::add_array_to_object(
  const std::string_view name, [[maybe_unused]] const std::size_t size, OutputObjectType* const parent) const noexcept
  -> OutputArrayType
{
  return shielded<OutputArrayType>(
    [this, name, parent] -> OutputArrayType
    {
      claim(name, *parent);
      return {.branch = tree_->add_array(parent->branch, name)};
    });
}

///
///
auto rfl_tree_writer::add_object_to_array(
  [[maybe_unused]] const std::size_t size, OutputArrayType* const parent) const noexcept -> OutputObjectType
{
  return shielded<OutputObjectType>(
    [this, parent] -> OutputObjectType { return {.branch = tree_->append_table(parent->branch)}; });
}

///
///
auto rfl_tree_writer::add_object_to_object(
  const std::string_view name, [[maybe_unused]] const std::size_t size, OutputObjectType* const parent) const noexcept
  -> OutputObjectType
{
  return shielded<OutputObjectType>(
    [this, name, parent] -> OutputObjectType
    {
      claim(name, *parent);
      return {.branch = tree_->add_table(parent->branch, name)};
    });
}

///
///
auto rfl_tree_writer::add_null_to_array(OutputArrayType* const parent) const noexcept -> OutputVarType
{
  return shielded<OutputVarType>(
    [this, parent] -> OutputVarType
    {
      tree_->append_null(parent->branch);
      return {};
    });
}

///
///
auto rfl_tree_writer::add_null_to_object(const std::string_view name, OutputObjectType* const parent) const noexcept
  -> OutputVarType
{
  return shielded<OutputVarType>(
    [this, name, parent] -> OutputVarType
    {
      claim(name, *parent);
      tree_->add_null(parent->branch, name);
      return {};
    });
}

///
///
auto rfl_tree_writer::claim(const std::string_view name, const OutputObjectType& parent) const -> void
{
  if(!parent.top)
  {
    return;
  }

  tree_->erase(parent.branch, name);
  written_->emplace(name);
}

} // namespace arude::config::detail
