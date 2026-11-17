///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///

#include "arude/config/tree_reader.hpp"
#include "arude/config/common.hpp"

#include <rfl/Result.hpp>

#include <cstddef>
#include <exception>
#include <format>
#include <string>

namespace arude::config::detail
{

///
///
rfl_tree_reader::rfl_tree_reader(const tree_reader& tree, const key_set_t& hidden, std::exception_ptr& failure)
  : tree_{&tree}
  , hidden_{&hidden}
  , failure_{&failure}
  , root_{tree.root()}
{
}

///
///
auto rfl_tree_reader::get_field_from_array(const std::size_t index, const InputArrayType array) const noexcept
  -> rfl::Result<InputVarType>
{
  return shielded<rfl::Result<InputVarType>>(
    [this, index, array] -> rfl::Result<InputVarType>
    {
      if(const auto found = tree_->element(index, array); found.has_value())
      {
        return found.value();
      }

      return rfl::error(std::format("Index {} is out of bounds.", index));
    });
}

///
///
auto rfl_tree_reader::get_field_from_object(const std::string& name, const InputObjectType& object) const noexcept
  -> rfl::Result<InputVarType>
{
  return shielded<rfl::Result<InputVarType>>(
    [this, &name, &object] -> rfl::Result<InputVarType>
    {
      if(object != root_ || !hidden_->contains(name))
      {
        if(const auto found = tree_->field(name, object); found.has_value())
        {
          return found.value();
        }
      }

      return rfl::error(std::format("Object contains no field named '{}'.", name));
    });
}

///
///
auto rfl_tree_reader::is_empty(const InputVarType& var) const noexcept -> bool
{
  if(var.address == nullptr)
  {
    return true;
  }

  try
  {
    return tree_->kind(var) == node_kind::null;
  }
  catch(...)
  {
    if(!*failure_)
    {
      *failure_ = std::current_exception();
    }

    return false;
  }
}

///
///
auto rfl_tree_reader::to_array(const InputVarType& var) const noexcept -> rfl::Result<InputArrayType>
{
  return shielded<rfl::Result<InputArrayType>>(
    [this, &var] -> rfl::Result<InputArrayType>
    {
      if(var.address != nullptr && tree_->kind(var) == node_kind::array)
      {
        return var;
      }

      return rfl::error("Could not cast to an array!");
    });
}

///
///
auto rfl_tree_reader::to_object(const InputVarType& var) const noexcept -> rfl::Result<InputObjectType>
{
  return shielded<rfl::Result<InputObjectType>>(
    [this, &var] -> rfl::Result<InputObjectType>
    {
      if(var.address != nullptr && tree_->kind(var) == node_kind::table)
      {
        return var;
      }

      return rfl::error("Could not cast to a table!");
    });
}

} // namespace arude::config::detail
