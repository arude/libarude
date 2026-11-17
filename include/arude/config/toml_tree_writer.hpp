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

#include <string_view>

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

} // namespace arude::config
