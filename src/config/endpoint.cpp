///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///

#include "arude/config/endpoint.hpp"
#include "arude/config/common.hpp"
#include "arude/config/tree_reader.hpp"
#include "arude/exception.hpp"

#include <format>
#include <optional>
#include <string_view>

namespace arude::config::detail
{

///
///
auto holds_tables(const tree_reader& tree, const tree_node node) -> bool
{
  const auto kind = tree.kind(node);

  if(kind != node_kind::array)
  {
    return kind == node_kind::table;
  }

  auto any = false;
  auto all_tables = true;

  const auto each = [&tree, &any, &all_tables](const tree_node element) -> bool
  {
    any = true;
    all_tables = tree.kind(element) == node_kind::table;

    return all_tables;
  };

  tree.for_each_element(node, element_visitor_for{each});

  return any && all_tables;
}

///
///
auto section_present(const tree_reader& tree, const std::string_view name, const key_set_t& foreign) -> bool
{
  const auto root = tree.root();

  if(!name.empty())
  {
    return tree.field(name, root).has_value();
  }

  auto present = false;

  const auto each = [&tree, &foreign, &present](const std::string_view key, const tree_node value) -> void
  {
    if(!present && !foreign.contains(key) && !holds_tables(tree, value))
    {
      present = true;
    }
  };

  tree.for_each_field(root, field_visitor_for{each});

  return present;
}

///
///
auto acquire_subtree(const tree_reader& tree, const std::string_view name) -> tree_node
{
  const auto root = tree.root();

  if(name.empty())
  {
    return root;
  }

  const auto found = tree.field(name, root);

  if(!found.has_value())
  {
    throw exception{std::format("arude::config: there is no section named '{}'.", name)};
  }

  return found.value();
}

///
///
auto probe_version(const tree_reader& tree, const tree_node node, const key_set_t& foreign) -> version_t
{
  // Never strict: the probe declares the version key alone, and every other
  // key is there for the type the node is read as next.
  return read_tree<endpoint_probe>(tree, node, foreign, false).version;
}

///
///
auto declared_version(const tree_reader& tree, const std::string_view name, const key_set_t& foreign)
  -> std::optional<version_t>
{
  if(!section_present(tree, name, foreign))
  {
    return std::nullopt;
  }

  // A section that is there but declares no readable version is not this
  // function's to report: whoever reads it next says what is wrong with it.
  try
  {
    return probe_version(tree, acquire_subtree(tree, name), foreign);
  }
  catch(...)
  {
    return std::nullopt;
  }
}

} // namespace arude::config::detail
