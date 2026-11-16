///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// Write access to an endpoint's tree, which is how a section writes itself
/// into a document without knowing which endpoint will emit it.
///
/// The counterpart of arude/config/tree_reader.hpp, and built on the same
/// idea: the endpoint supplies primitives — find or make a table, add, append
/// and erase — and libarude applies the rule a store keeps on top of them, that
/// a section replaces the keys it owns and nothing else. Nothing reflect-cpp
/// declares appears in tree_writer itself; detail::rfl_tree_writer is the
/// adapter reflect-cpp's parser writes through.
///
#pragma once

#include "arude/config/common.hpp"
#include "arude/non_owning_t.hpp"

#include <rfl/always_false.hpp>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace arude::config
{

///
/// A table or an array of a tree being written.
/// Opaque, like tree_node: what it points at is the writer's business. A
/// branch stays valid while what is written goes into it or below it, and
/// may not survive a write into the table or array that holds it.
///
struct tree_branch
{
  ///
  /// What the writer identifies the branch by; null for no branch at all.
  ///
  void* address = nullptr;
};

///
/// One value, in the kinds reflect-cpp writes. A string is a view, valid for
/// the call it is passed to only. Unsigned integers are a kind of their own so
/// that a format which keeps them apart can; TOML has no unsigned integers,
/// and its writer stores one as reflect-cpp's TOML writer does.
///
using tree_scalar_t = std::variant<bool, std::int64_t, std::uint64_t, double, std::string_view>;

///
/// Write access to one tree, which an endpoint supplies and sections write
/// through.
///
/// Primitives only: the rule that a section replaces exactly the keys it
/// owns is applied by libarude on top of these. An add_*() call is only ever
/// made for a key the table does not hold, so an endpoint need not decide what
/// adding over an existing key means. Anything thrown fails the store, or the
/// call to write_section(), that the writer was built for. An exception_t is
/// passed on as it is; anything else becomes an exception_t with io_error
/// whose report carries it.
///
/// A writer is built for one store, used by one thread, and destroyed before
/// the tree it writes is emitted.
///
class tree_writer
{
public: // Structors
  // A writer is a view of one tree for one store, built in place and never
  // handed on: copying one would only make a second view to keep in step.
  tree_writer(const tree_writer&) = delete;
  tree_writer(tree_writer&&) = delete;
  auto operator=(const tree_writer&) -> tree_writer& = delete;
  auto operator=(tree_writer&&) -> tree_writer& = delete;

public: // Methods
  ///
  /// Returns the top level of the tree, which is a table.
  /// \return The root branch.
  ///
  [[nodiscard]] virtual auto root() -> tree_branch = 0;

  ///
  /// Returns the table under a key, putting an empty one there first where
  /// the key is absent or holds anything other than a table. The key is a key
  /// and never a path.
  ///
  /// \param parent A table of this tree.
  /// \param key The key.
  /// \return The table.
  ///
  [[nodiscard]] virtual auto table(tree_branch parent, std::string_view key) -> tree_branch = 0;

  ///
  /// Removes a key from a table, with whatever it holds. Does nothing where
  /// the table does not hold the key.
  ///
  /// \param parent A table of this tree.
  /// \param key The key.
  ///
  virtual auto erase(tree_branch parent, std::string_view key) -> void = 0;

  ///
  /// Adds an empty table under a key the table does not hold.
  ///
  /// \param parent A table of this tree.
  /// \param key The key.
  /// \return The new table.
  ///
  [[nodiscard]] virtual auto add_table(tree_branch parent, std::string_view key) -> tree_branch = 0;

  ///
  /// Adds an empty array under a key the table does not hold.
  ///
  /// \param parent A table of this tree.
  /// \param key The key.
  /// \return The new array.
  ///
  [[nodiscard]] virtual auto add_array(tree_branch parent, std::string_view key) -> tree_branch = 0;

  ///
  /// Appends an empty table to an array.
  /// \param parent An array of this tree.
  /// \return The new table.
  ///
  [[nodiscard]] virtual auto append_table(tree_branch parent) -> tree_branch = 0;

  ///
  /// Appends an empty array to an array.
  /// \param parent An array of this tree.
  /// \return The new array.
  ///
  [[nodiscard]] virtual auto append_array(tree_branch parent) -> tree_branch = 0;

  ///
  /// Adds a value under a key the table does not hold.
  ///
  /// \param parent A table of this tree.
  /// \param key The key.
  /// \param value The value. Not retained.
  ///
  virtual auto add_value(tree_branch parent, std::string_view key, const tree_scalar_t& value) -> void = 0;

  ///
  /// Appends a value to an array.
  ///
  /// \param parent An array of this tree.
  /// \param value The value. Not retained.
  ///
  virtual auto append_value(tree_branch parent, const tree_scalar_t& value) -> void = 0;

  ///
  /// Adds a null under a key the table does not hold, in whatever form the
  /// format writes one. reflect-cpp writes a null only where a std::optional
  /// is disengaged and the rfl::NoOptionals processor is in use, which no
  /// section uses.
  ///
  /// \param parent A table of this tree.
  /// \param key The key.
  ///
  virtual auto add_null(tree_branch parent, std::string_view key) -> void = 0;

  ///
  /// Appends a null to an array, in whatever form the format writes one.
  /// \param parent An array of this tree.
  ///
  virtual auto append_null(tree_branch parent) -> void = 0;

protected: // Structors
  ///
  /// Constructs the base. The class is abstract, so only an endpoint's writer can.
  ///
  tree_writer() = default;

  ///
  /// Destroys the writer. Protected and not virtual, as writers live on the
  /// stack and are never deleted through this class.
  ///
  ~tree_writer() = default;
};

} // namespace arude::config

namespace arude::config::detail
{

///
/// reflect-cpp's Writer, in the sense of rfl::parsing::IsWriter, over a
/// tree_writer, rooted at one section's table.
///
/// Every field it writes at the top of that table first erases the key and is
/// recorded among the keys written, so a field replaces whatever the table held
/// under its name, and the caller can tell which owned keys the value no longer
/// writes. Below the top level everything is new, and only ever added.
///
/// Its members are called from reflect-cpp's noexcept code, so it throws
/// nothing. A throw from the tree is kept in the failure it was given, every
/// later call does nothing, and whoever ran the parser rethrows it once
/// reflect-cpp has returned.
///
/// The type names and member names are reflect-cpp's, which is what it looks
/// them up by.
///
// NOLINTBEGIN(readability-identifier-naming)
class rfl_tree_writer final
{
public: // Typedefs
  ///
  /// What reflect-cpp writes an array through.
  ///
  struct OutputArrayType
  {
    ///
    /// The array.
    ///
    tree_branch branch;
  };

  ///
  /// What reflect-cpp writes a table through.
  ///
  struct OutputObjectType
  {
    ///
    /// The table.
    ///
    tree_branch branch;

    ///
    /// Whether it is the section's own table, where a field replaces a key.
    ///
    bool top = false;
  };

  ///
  /// What reflect-cpp is handed back for a value. Carries nothing.
  ///
  struct OutputVarType
  {};

public: // Structors
  ///
  /// Constructs a writer.
  ///
  /// \param tree The tree to write. Retained, and must outlive this.
  /// \param target The section's table. Copied.
  /// \param written Where the keys written at the top of the table are
  ///                recorded. Retained, and must outlive this.
  /// \param failure Where a throw from the tree is kept. Retained, and must outlive this.
  ///
  rfl_tree_writer(tree_writer& tree, tree_branch target, key_set_t& written, std::exception_ptr& failure);

public: // Methods
  ///
  /// Required by rfl::parsing::IsWriter. A configuration is a table, so
  /// reflect-cpp never writes an array as the root.
  ///
  /// \tparam T The type of the size.
  /// \param size How many elements.
  /// \return Nothing usable; instantiating this is an error.
  ///
  template<typename T>
  [[nodiscard]] auto array_as_root(T size) const noexcept -> OutputArrayType;

  ///
  /// Returns the section's table as the root of what reflect-cpp writes.
  /// \param size How many fields; unused.
  /// \return The section's table.
  ///
  [[nodiscard]] auto object_as_root(std::size_t size) const noexcept -> OutputObjectType;

  ///
  /// Required by rfl::parsing::IsWriter. A configuration is a table, so
  /// reflect-cpp never writes a null as the root, and this writes nothing.
  ///
  /// \return Nothing.
  ///
  [[nodiscard]] auto null_as_root() const noexcept -> OutputVarType;

  ///
  /// Required by rfl::parsing::IsWriter. A configuration is a table, so
  /// reflect-cpp never writes a value as the root.
  ///
  /// \tparam T The type of the value.
  /// \param var The value.
  /// \return Nothing usable; instantiating this is an error.
  ///
  template<typename T>
  [[nodiscard]] auto value_as_root(const T& var) const noexcept -> OutputVarType;

  ///
  /// Appends an empty array to an array.
  ///
  /// \param size How many elements it will hold; unused.
  /// \param parent The array.
  /// \return The new array.
  ///
  auto add_array_to_array(std::size_t size, OutputArrayType* parent) const noexcept -> OutputArrayType;

  ///
  /// Adds an empty array to a table.
  ///
  /// \param name The key.
  /// \param size How many elements it will hold; unused.
  /// \param parent The table.
  /// \return The new array.
  ///
  auto add_array_to_object(std::string_view name, std::size_t size, OutputObjectType* parent) const noexcept
    -> OutputArrayType;

  ///
  /// Appends an empty table to an array.
  ///
  /// \param size How many fields it will hold; unused.
  /// \param parent The array.
  /// \return The new table.
  ///
  auto add_object_to_array(std::size_t size, OutputArrayType* parent) const noexcept -> OutputObjectType;

  ///
  /// Adds an empty table to a table.
  ///
  /// \param name The key.
  /// \param size How many fields it will hold; unused.
  /// \param parent The table.
  /// \return The new table.
  ///
  auto add_object_to_object(std::string_view name, std::size_t size, OutputObjectType* parent) const noexcept
    -> OutputObjectType;

  ///
  /// Appends a value to an array.
  ///
  /// \tparam T The type of the value.
  /// \param var The value.
  /// \param parent The array.
  /// \return Nothing.
  ///
  template<typename T>
  auto add_value_to_array(const T& var, OutputArrayType* parent) const noexcept -> OutputVarType;

  ///
  /// Adds a value to a table.
  ///
  /// \tparam T The type of the value.
  /// \param name The key.
  /// \param var The value.
  /// \param parent The table.
  /// \return Nothing.
  ///
  template<typename T>
  auto add_value_to_object(std::string_view name, const T& var, OutputObjectType* parent) const noexcept
    -> OutputVarType;

  ///
  /// Appends a null to an array.
  /// \param parent The array.
  /// \return Nothing.
  ///
  auto add_null_to_array(OutputArrayType* parent) const noexcept -> OutputVarType;

  ///
  /// Adds a null to a table.
  ///
  /// \param name The key.
  /// \param parent The table.
  /// \return Nothing.
  ///
  auto add_null_to_object(std::string_view name, OutputObjectType* parent) const noexcept -> OutputVarType;

  ///
  /// Marks the end of an array, which needs nothing doing.
  /// \param array The array.
  ///
  auto end_array(OutputArrayType* array) const noexcept -> void;

  ///
  /// Marks the end of a table, which needs nothing doing.
  /// \param object The table.
  ///
  auto end_object(OutputObjectType* object) const noexcept -> void;

private: // Helpers
  ///
  /// Converts a value reflect-cpp writes to the kind a tree_writer takes.
  ///
  /// \tparam T The type of the value.
  /// \param var The value. Retained by the result where it is a string.
  /// \return The value: a signed integer widened to std::int64_t, an unsigned
  ///         one to std::uint64_t, a floating-point number to double, and a
  ///         string viewed.
  ///
  template<typename T>
  [[nodiscard]] static auto to_scalar(const T& var) -> tree_scalar_t;

  ///
  /// Makes a key of the section's own table the value's: erases whatever the
  /// table holds under it, and records it as written. Does nothing below the
  /// top level.
  ///
  /// \param name The key.
  /// \param parent The table it goes into.
  ///
  auto claim(std::string_view name, const OutputObjectType& parent) const -> void;

  ///
  /// Runs one call into the tree, unless an earlier one threw, keeping
  /// whatever it throws.
  ///
  /// \tparam TResult What the call returns to reflect-cpp.
  /// \param call The call. Not retained.
  /// \return What the call returned, or a value-initialised result where it
  ///         threw or was skipped.
  ///
  template<typename TResult>
  [[nodiscard]] auto shielded(const std::invocable auto& call) const noexcept -> TResult;

private: // Variables
  non_owning_t<tree_writer> tree_;
  tree_branch target_;
  non_owning_t<key_set_t> written_;
  non_owning_t<std::exception_ptr> failure_;
};
// NOLINTEND(readability-identifier-naming)

///
///
inline rfl_tree_writer::rfl_tree_writer(
  tree_writer& tree, const tree_branch target, key_set_t& written, std::exception_ptr& failure)
  : tree_{&tree}
  , target_{target}
  , written_{&written}
  , failure_{&failure}
{
}

///
///
template<typename T>
auto rfl_tree_writer::array_as_root([[maybe_unused]] const T size) const noexcept -> OutputArrayType
{
  static_assert(rfl::always_false_v<T>, "A configuration is a table, so it is never written as an array.");
  return {};
}

///
///
inline auto rfl_tree_writer::object_as_root([[maybe_unused]] const std::size_t size) const noexcept -> OutputObjectType
{
  return {.branch = target_, .top = true};
}

///
///
// NOLINTNEXTLINE(readability-convert-member-functions-to-static): reflect-cpp calls it on an instance.
inline auto rfl_tree_writer::null_as_root() const noexcept -> OutputVarType
{
  return {};
}

///
///
template<typename T>
auto rfl_tree_writer::value_as_root([[maybe_unused]] const T& var) const noexcept -> OutputVarType
{
  static_assert(rfl::always_false_v<T>, "A configuration is a table, so it is never written as a value.");
  return {};
}

///
///
inline auto rfl_tree_writer::add_array_to_array(
  [[maybe_unused]] const std::size_t size, OutputArrayType* const parent) const noexcept -> OutputArrayType
{
  return shielded<OutputArrayType>(
    [this, parent] -> OutputArrayType { return {.branch = tree_->append_array(parent->branch)}; });
}

///
///
inline auto rfl_tree_writer::add_array_to_object(
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
inline auto rfl_tree_writer::add_object_to_array(
  [[maybe_unused]] const std::size_t size, OutputArrayType* const parent) const noexcept -> OutputObjectType
{
  return shielded<OutputObjectType>(
    [this, parent] -> OutputObjectType { return {.branch = tree_->append_table(parent->branch)}; });
}

///
///
inline auto rfl_tree_writer::add_object_to_object(
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
template<typename T>
auto rfl_tree_writer::add_value_to_array(const T& var, OutputArrayType* const parent) const noexcept -> OutputVarType
{
  return shielded<OutputVarType>(
    [this, &var, parent] -> OutputVarType
    {
      tree_->append_value(parent->branch, to_scalar(var));
      return {};
    });
}

///
///
template<typename T>
auto rfl_tree_writer::add_value_to_object(
  const std::string_view name, const T& var, OutputObjectType* const parent) const noexcept -> OutputVarType
{
  return shielded<OutputVarType>(
    [this, name, &var, parent] -> OutputVarType
    {
      claim(name, *parent);
      tree_->add_value(parent->branch, name, to_scalar(var));
      return {};
    });
}

///
///
inline auto rfl_tree_writer::add_null_to_array(OutputArrayType* const parent) const noexcept -> OutputVarType
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
inline auto rfl_tree_writer::add_null_to_object(
  const std::string_view name, OutputObjectType* const parent) const noexcept -> OutputVarType
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
// NOLINTNEXTLINE(readability-convert-member-functions-to-static): reflect-cpp calls it on an instance.
inline auto rfl_tree_writer::end_array([[maybe_unused]] OutputArrayType* const array) const noexcept -> void
{
}

///
///
// NOLINTNEXTLINE(readability-convert-member-functions-to-static): reflect-cpp calls it on an instance.
inline auto rfl_tree_writer::end_object([[maybe_unused]] OutputObjectType* const object) const noexcept -> void
{
}

///
///
template<typename T>
auto rfl_tree_writer::to_scalar(const T& var) -> tree_scalar_t
{
  if constexpr(std::same_as<T, bool>)
  {
    return tree_scalar_t{std::in_place_type<bool>, var};
  }
  else if constexpr(std::convertible_to<const T&, std::string_view>)
  {
    return tree_scalar_t{std::in_place_type<std::string_view>, var};
  }
  else if constexpr(std::floating_point<T>)
  {
    return tree_scalar_t{std::in_place_type<double>, static_cast<double>(var)};
  }
  else if constexpr(std::unsigned_integral<T>)
  {
    return tree_scalar_t{std::in_place_type<std::uint64_t>, static_cast<std::uint64_t>(var)};
  }
  else if constexpr(std::signed_integral<T>)
  {
    return tree_scalar_t{std::in_place_type<std::int64_t>, static_cast<std::int64_t>(var)};
  }
  else
  {
    static_assert(rfl::always_false_v<T>, "reflect-cpp wrote a basic type that a tree_writer does not take.");
  }
}

///
///
inline auto rfl_tree_writer::claim(const std::string_view name, const OutputObjectType& parent) const -> void
{
  if(!parent.top)
  {
    return;
  }

  tree_->erase(parent.branch, name);
  written_->emplace(name);
}

///
///
template<typename TResult>
auto rfl_tree_writer::shielded(const std::invocable auto& call) const noexcept -> TResult
{
  // Once a call has thrown, the branches reflect-cpp holds may be null, and
  // nothing more can be written that would make the tree any more right.
  if(*failure_)
  {
    return TResult{};
  }

  try
  {
    return call();
  }
  catch(...)
  {
    *failure_ = std::current_exception();

    return TResult{};
  }
}

} // namespace arude::config::detail
