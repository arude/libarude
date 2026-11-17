///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// Read access to an endpoint's tree, which is how a section reads itself out
/// of a document without knowing which endpoint parsed it.
///
/// A section knows its type and nothing of the endpoint; a document knows the
/// endpoint and nothing of the section's type. They meet here: the document
/// asks the endpoint for a tree_reader over the tree it parsed, and each
/// section runs reflect-cpp's parser for its own type over that reader. The
/// interface is primitives only — walk a node, say what kind it is, convert a
/// leaf — so what a document promises about presence, the root's view and
/// strictness is written once, in libarude, on top of it, and holds for every
/// endpoint alike.
///
/// Nothing reflect-cpp declares appears in tree_reader itself, so an endpoint
/// written against it does not break when reflect-cpp changes. The adapter
/// that speaks reflect-cpp's language, detail::rfl_tree_reader, is libarude's.
///
#pragma once

#include "arude/config/common.hpp"
#include "arude/non_owning_t.hpp"

#include <rfl/always_false.hpp>
#include <rfl/Result.hpp>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace arude::config
{

///
/// A node of a tree being read.
/// Opaque: what it points at is the reader's business. A reader hands out the
/// same handle every time for the same node, so two handles name one node
/// exactly when they are equal.
///
struct tree_node
{
  ///
  /// What the reader identifies the node by; null for no node at all.
  ///
  const void* address = nullptr;

  ///
  /// Compares two handles by the node they name.
  /// \return true if both name the same node.
  ///
  friend auto operator==(tree_node, tree_node) -> bool = default;
};

///
/// What a node is, as far as the rules a document applies need to know.
///
enum class node_kind : std::uint8_t
{
  ///
  /// A table: keys, each with a node.
  ///
  table,

  ///
  /// An array: nodes in order.
  ///
  array,

  ///
  /// Any scalar the format has, dates and times included.
  ///
  value,

  ///
  /// A null, in a format that has one. TOML has none, and never reports it.
  ///
  null
};

///
/// What an endpoint asks of every section read through its reader.
/// Passive, so that a further option is a member with a default rather than a
/// change to the interface. Each boolean here doubles what is instantiated
/// per configuration type, which is acceptable for one or two.
///
struct tree_read_options
{
  ///
  /// Whether a section read fails on a field its type does not declare.
  /// For a named section that is any key of its table. For the root section it
  /// is any key of the top level that no other section is registered under and
  /// that is neither a table nor an array of tables: those the root takes to be
  /// somebody else's, by the same rule that decides whether the root is in the
  /// document at all.
  ///
  bool reject_unknown_fields = false;
};

///
/// Read access to one parsed tree, which an endpoint supplies and sections
/// read through.
///
/// Primitives only: whether a section is present, what the root section sees
/// and how strictly it reads are decided by libarude on top of these. A
/// conversion that does not fit the node is an expected failure, and comes back
/// as a message. Anything thrown is a fault of the reader: libarude catches it,
/// also where it is thrown through reflect-cpp's noexcept code, and fails the
/// load with an exception_t whose report carries it.
///
/// A reader is built for one load, used by one thread, and destroyed before the
/// tree it reads is moved or changed.
///
class tree_reader
{
public: // Typedefs
  ///
  /// Receives the fields of a table, in the reader's order.
  ///
  class field_visitor
  {
  public: // Structors
    field_visitor(const field_visitor&) = delete;
    field_visitor(field_visitor&&) = delete;
    auto operator=(const field_visitor&) -> field_visitor& = delete;
    auto operator=(field_visitor&&) -> field_visitor& = delete;

  public: // Methods
    ///
    /// Receives one field.
    ///
    /// \param key The field's key, valid for the call only.
    /// \param value The field's node.
    ///
    virtual auto visit(std::string_view key, tree_node value) const -> void = 0;

  protected: // Structors
    ///
    /// Constructs the base. The class is abstract, so only a visitor can.
    ///
    field_visitor() = default;

    ///
    /// Destroys the visitor. Protected and not virtual, as visitors live on
    /// the stack and are never deleted through this class.
    ///
    ~field_visitor() = default;
  };

  ///
  /// Receives the elements of an array, in order, until it asks to stop.
  ///
  class element_visitor
  {
  public: // Structors
    element_visitor(const element_visitor&) = delete;
    element_visitor(element_visitor&&) = delete;
    auto operator=(const element_visitor&) -> element_visitor& = delete;
    auto operator=(element_visitor&&) -> element_visitor& = delete;

  public: // Methods
    ///
    /// Receives one element.
    ///
    /// \param value The element's node.
    /// \return false to stop the walk here, true to go on.
    ///
    [[nodiscard]] virtual auto visit(tree_node value) const -> bool = 0;

  protected: // Structors
    ///
    /// Constructs the base. The class is abstract, so only a visitor can.
    ///
    element_visitor() = default;

    ///
    /// Destroys the visitor. Protected and not virtual, as visitors live on
    /// the stack and are never deleted through this class.
    ///
    ~element_visitor() = default;
  };

public: // Structors
  // A reader is a view of one tree for one load, built in place and never
  // handed on: copying one would only make a second view to keep in step.
  tree_reader(const tree_reader&) = delete;
  tree_reader(tree_reader&&) = delete;
  auto operator=(const tree_reader&) -> tree_reader& = delete;
  auto operator=(tree_reader&&) -> tree_reader& = delete;

public: // Accessors
  ///
  /// Returns the top level of the tree, which is a table.
  /// \return The root node.
  ///
  [[nodiscard]] virtual auto root() const -> tree_node = 0;

  ///
  /// Says what a node is.
  /// \param node A node of this tree.
  /// \return Its kind.
  ///
  [[nodiscard]] virtual auto kind(tree_node node) const -> node_kind = 0;

  ///
  /// Looks a key up in a table. The key is a key and never a path: a dot or a
  /// bracket in it is part of the key.
  ///
  /// \param key The key.
  /// \param table A node of this tree.
  /// \return The node under the key, or std::nullopt where there is none or
  ///         the node is not a table.
  ///
  [[nodiscard]] virtual auto field(std::string_view key, tree_node table) const -> std::optional<tree_node> = 0;

  ///
  /// Looks an element of an array up by its position.
  ///
  /// \param index The position, from zero.
  /// \param array A node of this tree.
  /// \return The element, or std::nullopt where there is none or the node is
  ///         not an array.
  ///
  [[nodiscard]] virtual auto element(std::size_t index, tree_node array) const -> std::optional<tree_node> = 0;

  ///
  /// Hands every field of a table to a visitor, in the reader's order.
  /// Nothing is visited where the node is not a table.
  ///
  /// \param table A node of this tree.
  /// \param visitor What receives the fields. Not retained.
  ///
  virtual auto for_each_field(tree_node table, const field_visitor& visitor) const -> void = 0;

  ///
  /// Hands the elements of an array to a visitor, in order, until it asks to
  /// stop. Nothing is visited where the node is not an array.
  ///
  /// \param array A node of this tree.
  /// \param visitor What receives the elements. Not retained.
  ///
  virtual auto for_each_element(tree_node array, const element_visitor& visitor) const -> void = 0;

  ///
  /// Converts a leaf to a string.
  /// \param node A node of this tree.
  /// \return The string, or a message saying why the node is not one.
  ///
  [[nodiscard]] virtual auto to_string(tree_node node) const -> std::expected<std::string, std::string> = 0;

  ///
  /// Converts a leaf to a boolean.
  /// \param node A node of this tree.
  /// \return The boolean, or a message saying why the node is not one.
  ///
  [[nodiscard]] virtual auto to_bool(tree_node node) const -> std::expected<bool, std::string> = 0;

  ///
  /// Converts a leaf to a signed integer. A section reads every signed integer
  /// type through this, narrowing it with a static_cast.
  ///
  /// \param node A node of this tree.
  /// \return The integer, or a message saying why the node is not one.
  ///
  [[nodiscard]] virtual auto to_int64(tree_node node) const -> std::expected<std::int64_t, std::string> = 0;

  ///
  /// Converts a leaf to an unsigned integer. A section reads every unsigned
  /// integer type through this, narrowing it with a static_cast. Separate from
  /// to_int64() so that a format which keeps the two apart can.
  ///
  /// \param node A node of this tree.
  /// \return The integer, or a message saying why the node is not one.
  ///
  [[nodiscard]] virtual auto to_uint64(tree_node node) const -> std::expected<std::uint64_t, std::string> = 0;

  ///
  /// Converts a leaf to a floating-point number. A section reads every
  /// floating-point type through this, narrowing it with a static_cast.
  ///
  /// \param node A node of this tree.
  /// \return The number, or a message saying why the node is not one.
  ///
  [[nodiscard]] virtual auto to_double(tree_node node) const -> std::expected<double, std::string> = 0;

  ///
  /// Returns what the endpoint asks of every section read through this reader.
  /// \return The options; the defaults unless a reader says otherwise.
  ///
  [[nodiscard]] virtual auto options() const -> tree_read_options { return {}; }

protected: // Structors
  ///
  /// Constructs the base. The class is abstract, so only an endpoint's reader can.
  ///
  tree_reader() = default;

  ///
  /// Destroys the reader. Protected and not virtual, as readers live on the
  /// stack and are never deleted through this class.
  ///
  ~tree_reader() = default;
};

} // namespace arude::config

namespace arude::config::detail
{

///
/// A field visitor that calls a callable with every field.
/// \tparam F The callable, taking a std::string_view key and a tree_node.
///
template<typename F>
class field_visitor_for final : public tree_reader::field_visitor
{
public: // Structors
  ///
  /// Constructs a visitor.
  /// \param call What every field is handed to. Retained, and must outlive this.
  ///
  explicit field_visitor_for(const F& call);

public: // Methods
  ///
  /// \see tree_reader::field_visitor::visit
  ///
  auto visit(std::string_view key, tree_node value) const -> void override;

private: // Variables
  non_owning_t<const F> call_;
};

///
/// An element visitor that calls a callable with every element.
/// \tparam F The callable, taking a tree_node and returning false to stop.
///
template<typename F>
class element_visitor_for final : public tree_reader::element_visitor
{
public: // Structors
  ///
  /// Constructs a visitor.
  /// \param call What every element is handed to. Retained, and must outlive this.
  ///
  explicit element_visitor_for(const F& call);

public: // Methods
  ///
  /// \see tree_reader::element_visitor::visit
  ///
  [[nodiscard]] auto visit(tree_node value) const -> bool override;

private: // Variables
  non_owning_t<const F> call_;
};

///
/// Hands the fields of a table on to one of reflect-cpp's object readers,
/// leaving out the keys it is told to hide.
///
/// \tparam ObjectReader reflect-cpp's object reader.
///
template<typename ObjectReader>
class rfl_field_adapter final : public tree_reader::field_visitor
{
public: // Structors
  ///
  /// Constructs an adapter.
  ///
  /// \param reader What receives the fields. Retained, and must outlive this.
  /// \param hidden The keys to leave out, or null for none. Retained.
  ///
  rfl_field_adapter(const ObjectReader& reader, non_owning_t<const key_set_t> hidden);

public: // Methods
  ///
  /// \see tree_reader::field_visitor::visit
  ///
  auto visit(std::string_view key, tree_node value) const -> void override;

private: // Variables
  non_owning_t<const ObjectReader> reader_;
  non_owning_t<const key_set_t> hidden_; // Null where nothing is hidden.
};

///
/// Hands the elements of an array on to one of reflect-cpp's array readers,
/// and keeps the first error it reports, at which the walk stops.
///
/// \tparam ArrayReader reflect-cpp's array reader.
///
template<typename ArrayReader>
class rfl_element_adapter final : public tree_reader::element_visitor
{
public: // Structors
  ///
  /// Constructs an adapter.
  /// \param reader What receives the elements. Retained, and must outlive this.
  ///
  explicit rfl_element_adapter(const ArrayReader& reader);

public: // Accessors
  ///
  /// Returns the error the array reader reported, if it reported one.
  /// \return The error, or std::nullopt.
  ///
  [[nodiscard]] auto error() const -> const std::optional<rfl::Error>&;

public: // Methods
  ///
  /// \see tree_reader::element_visitor::visit
  ///
  [[nodiscard]] auto visit(tree_node value) const -> bool override;

private: // Variables
  non_owning_t<const ArrayReader> reader_;
  mutable std::optional<rfl::Error> error_; // Set from visit(), which is const as the interface has it.
};

///
/// reflect-cpp's Reader, in the sense of rfl::parsing::IsReader, over a
/// tree_reader: what lets reflect-cpp's own parser for a configuration type
/// read through an endpoint it was never compiled against.
///
/// Where it reads the tree's root, it does not see the keys it is told to
/// hide, which is how the root section is kept from reading the registered
/// named sections as fields of its own. No tree is copied to do it.
///
/// Its members are called from reflect-cpp's noexcept code, so it throws
/// nothing. A throw from the tree is kept in the failure it was given, the
/// call that met it reports an error to reflect-cpp, and whoever ran the
/// parser rethrows it once reflect-cpp has returned.
///
/// The type names and member names are reflect-cpp's, which is what it looks
/// them up by.
///
// NOLINTBEGIN(readability-identifier-naming)
class rfl_tree_reader final
{
public: // Typedefs / Constants
  ///
  /// Whether reflect-cpp may hand a node to a type's own constructor. Never:
  /// such a constructor takes a node of one format, and the tree here could
  /// be any.
  ///
  /// \tparam T The type reflect-cpp asks about.
  ///
  template<typename T>
  static constexpr bool has_custom_constructor = false;

  ///
  /// What reflect-cpp reads an array through.
  ///
  using InputArrayType = tree_node;

  ///
  /// What reflect-cpp reads a table through.
  ///
  using InputObjectType = tree_node;

  ///
  /// What reflect-cpp reads any node through.
  ///
  using InputVarType = tree_node;

public: // Structors
  ///
  /// Constructs a reader.
  ///
  /// \param tree The tree to read. Retained, and must outlive this.
  /// \param hidden The keys of the root not to see. Retained, and must outlive this.
  /// \param failure Where a throw from the tree is kept. Retained, and must outlive this.
  ///
  rfl_tree_reader(const tree_reader& tree, const key_set_t& hidden, std::exception_ptr& failure);

public: // Accessors
  ///
  /// Looks an element of an array up by its position.
  ///
  /// \param index The position.
  /// \param array The array.
  /// \return The element, or an error.
  ///
  [[nodiscard]] auto get_field_from_array(std::size_t index, InputArrayType array) const noexcept
    -> rfl::Result<InputVarType>;

  ///
  /// Looks a key up in a table, as though a hidden key of the root were absent.
  ///
  /// \param name The key.
  /// \param object The table.
  /// \return The node under the key, or an error.
  ///
  [[nodiscard]] auto get_field_from_object(const std::string& name, const InputObjectType& object) const noexcept
    -> rfl::Result<InputVarType>;

  ///
  /// Reports whether a node is empty: no node at all, or a null.
  /// \param var The node.
  /// \return true if it is empty.
  ///
  [[nodiscard]] auto is_empty(const InputVarType& var) const noexcept -> bool;

  ///
  /// Converts a leaf to a string, a boolean, an integer or a floating-point
  /// number, through the tree's conversion for its kind.
  ///
  /// \tparam T The type to convert to.
  /// \param var The node.
  /// \return The value, or an error.
  ///
  template<typename T>
  [[nodiscard]] auto to_basic_type(const InputVarType& var) const noexcept -> rfl::Result<T>;

  ///
  /// Takes a node as an array.
  /// \param var The node.
  /// \return The node, or an error where it is not an array.
  ///
  [[nodiscard]] auto to_array(const InputVarType& var) const noexcept -> rfl::Result<InputArrayType>;

  ///
  /// Takes a node as a table.
  /// \param var The node.
  /// \return The node, or an error where it is not a table.
  ///
  [[nodiscard]] auto to_object(const InputVarType& var) const noexcept -> rfl::Result<InputObjectType>;

  ///
  /// Hands every element of an array to reflect-cpp's array reader.
  ///
  /// \tparam ArrayReader reflect-cpp's array reader.
  /// \param reader What receives the elements. Not retained.
  /// \param array The array.
  /// \return The first error the reader reported, or std::nullopt.
  ///
  template<typename ArrayReader>
  [[nodiscard]] auto read_array(const ArrayReader& reader, const InputArrayType& array) const noexcept
    -> std::optional<rfl::Error>;

  ///
  /// Hands every field of a table to reflect-cpp's object reader, leaving out
  /// the hidden keys where the table is the root.
  ///
  /// \tparam ObjectReader reflect-cpp's object reader.
  /// \param reader What receives the fields. Not retained.
  /// \param object The table.
  /// \return An error where the tree failed, or std::nullopt.
  ///
  template<typename ObjectReader>
  [[nodiscard]] auto read_object(const ObjectReader& reader, const InputObjectType& object) const noexcept
    -> std::optional<rfl::Error>;

  ///
  /// Required by rfl::parsing::IsReader, and never called, as
  /// has_custom_constructor is false for every type.
  ///
  /// \tparam T The type reflect-cpp asks about.
  /// \param var The node.
  /// \return An error.
  ///
  template<typename T>
  [[nodiscard]] auto use_custom_constructor(InputVarType var) const noexcept -> rfl::Result<T>;

private: // Helpers
  ///
  /// Runs one call into the tree, keeping whatever it throws.
  ///
  /// \tparam TResult What the call returns to reflect-cpp.
  /// \param call The call. Not retained.
  /// \return What the call returned, or an error where it threw.
  ///
  template<typename TResult>
  [[nodiscard]] auto shielded(const std::invocable auto& call) const noexcept -> TResult;

  ///
  /// Turns a conversion's result into reflect-cpp's.
  ///
  /// \tparam T The type reflect-cpp asked for.
  /// \param converted The conversion's result.
  /// \return The value, cast to T, or the message as an error.
  ///
  template<typename T>
  [[nodiscard]] static auto converted_to(auto converted) -> rfl::Result<T>;

private: // Variables
  non_owning_t<const tree_reader> tree_;
  non_owning_t<const key_set_t> hidden_;
  non_owning_t<std::exception_ptr> failure_;
  tree_node root_; // tree_->root(), taken once.
};
// NOLINTEND(readability-identifier-naming)

///
///
template<typename F>
field_visitor_for<F>::field_visitor_for(const F& call)
  : call_{&call}
{
}

///
///
template<typename F>
auto field_visitor_for<F>::visit(const std::string_view key, const tree_node value) const -> void
{
  (*call_)(key, value);
}

///
///
template<typename F>
element_visitor_for<F>::element_visitor_for(const F& call)
  : call_{&call}
{
}

///
///
template<typename F>
auto element_visitor_for<F>::visit(const tree_node value) const -> bool
{
  return (*call_)(value);
}

///
///
template<typename ObjectReader>
rfl_field_adapter<ObjectReader>::rfl_field_adapter(
  const ObjectReader& reader, const non_owning_t<const key_set_t> hidden)
  : reader_{&reader}
  , hidden_{hidden}
{
}

///
///
template<typename ObjectReader>
auto rfl_field_adapter<ObjectReader>::visit(const std::string_view key, const tree_node value) const -> void
{
  if(hidden_ != nullptr && hidden_->contains(key))
  {
    return;
  }

  // reflect-cpp's object reader takes the node by non-const reference.
  auto node = value;
  reader_->read(key, node);
}

///
///
template<typename ArrayReader>
rfl_element_adapter<ArrayReader>::rfl_element_adapter(const ArrayReader& reader)
  : reader_{&reader}
{
}

///
///
template<typename ArrayReader>
auto rfl_element_adapter<ArrayReader>::error() const -> const std::optional<rfl::Error>&
{
  return error_;
}

///
///
template<typename ArrayReader>
auto rfl_element_adapter<ArrayReader>::visit(const tree_node value) const -> bool
{
  // reflect-cpp's array reader takes the node by non-const reference.
  auto node = value;
  error_ = reader_->read(node);

  return !error_.has_value();
}

///
///
template<typename T>
auto rfl_tree_reader::to_basic_type(const InputVarType& var) const noexcept -> rfl::Result<T>
{
  using value_t = std::remove_cvref_t<T>;

  return shielded<rfl::Result<T>>(
    [this, &var] -> rfl::Result<T>
    {
      // The same split as rfl::toml::Reader's, with unsigned integers given a
      // conversion of their own.
      if constexpr(std::same_as<value_t, std::string>)
      {
        return converted_to<T>(tree_->to_string(var));
      }
      else if constexpr(std::same_as<value_t, bool>)
      {
        return converted_to<T>(tree_->to_bool(var));
      }
      else if constexpr(std::floating_point<value_t>)
      {
        return converted_to<T>(tree_->to_double(var));
      }
      else if constexpr(std::unsigned_integral<value_t>)
      {
        return converted_to<T>(tree_->to_uint64(var));
      }
      else if constexpr(std::signed_integral<value_t>)
      {
        return converted_to<T>(tree_->to_int64(var));
      }
      else
      {
        static_assert(
          rfl::always_false_v<T>, "reflect-cpp asked for a basic type that a tree_reader cannot convert to.");
      }
    });
}

///
///
template<typename ArrayReader>
auto rfl_tree_reader::read_array(const ArrayReader& reader, const InputArrayType& array) const noexcept
  -> std::optional<rfl::Error>
{
  return shielded<std::optional<rfl::Error>>(
    [this, &reader, &array] -> std::optional<rfl::Error>
    {
      const auto adapter = rfl_element_adapter<ArrayReader>{reader};
      tree_->for_each_element(array, adapter);

      return adapter.error();
    });
}

///
///
template<typename ObjectReader>
auto rfl_tree_reader::read_object(const ObjectReader& reader, const InputObjectType& object) const noexcept
  -> std::optional<rfl::Error>
{
  return shielded<std::optional<rfl::Error>>(
    [this, &reader, &object] -> std::optional<rfl::Error>
    {
      const auto adapter = rfl_field_adapter<ObjectReader>{reader, object == root_ ? hidden_ : nullptr};
      tree_->for_each_field(object, adapter);

      return std::nullopt;
    });
}

///
///
template<typename T>
auto rfl_tree_reader::use_custom_constructor([[maybe_unused]] const InputVarType var) const noexcept -> rfl::Result<T>
{
  return rfl::error("A tree_reader has no custom constructors.");
}

///
///
template<typename TResult>
auto rfl_tree_reader::shielded(const std::invocable auto& call) const noexcept -> TResult
{
  try
  {
    return call();
  }
  catch(...)
  {
    // The first throw is the one that explains the failure; whatever follows
    // it is reflect-cpp carrying on past an error it has been handed.
    if(!*failure_)
    {
      *failure_ = std::current_exception();
    }

    static constexpr auto message = std::string_view{"The tree being read failed; see the exception it threw."};

    if constexpr(std::same_as<TResult, std::optional<rfl::Error>>)
    {
      return rfl::Error{std::string{message}};
    }
    else
    {
      return rfl::error(std::string{message});
    }
  }
}

///
///
template<typename T>
auto rfl_tree_reader::converted_to(auto converted) -> rfl::Result<T>
{
  if(!converted.has_value())
  {
    return rfl::error(std::move(converted).error());
  }

  return static_cast<std::remove_cvref_t<T>>(std::move(converted).value());
}

} // namespace arude::config::detail
