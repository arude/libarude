///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// The writer the configuration support renders TOML through: reflect-cpp's
/// TOML writer carrying a per-call context for the values TOML cannot hold,
/// with three defects of the base fixed by hiding its members.
///
/// Writing cannot fail in reflect-cpp, so a value that cannot be written as
/// TOML is not an error result but a note in the context; write_root() turns
/// the first note into an invalid_payload before any byte reaches a
/// transport.
///
#pragma once

#include <rfl/parsing/Parent.hpp>
#include <rfl/toml/Writer.hpp>
#include <toml++/toml.hpp>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace arude::config::detail
{

///
/// Per-call state of a write. One per write, used by one thread.
///
class write_context final
{
public: // Methods
  ///
  /// Records why a value cannot be written as TOML. The first one recorded
  /// stands, so the failure a caller sees is the first value the writer met
  /// that it could not write.
  ///
  /// \param message The reason, without a prefix.
  ///
  auto fail(std::string message) -> void;

public: // Accessors
  ///
  /// Returns the failure recorded, or none.
  /// \return The reason a value could not be written.
  ///
  [[nodiscard]] auto failure() const -> const std::optional<std::string>&;

private: // Variables
  ///
  /// The reason the first unwritable value could not be written.
  ///
  std::optional<std::string> failure_;
};

///
/// rfl::toml::Writer with the per-call context and its defects fixed.
///
/// The hidden members below each replace a base member of the same name:
///
///   - add_array_to_object() and add_object_to_object() insert the new node
///     with insert_or_assign() rather than the base's emplace() followed by
///     at_path(), which reads a dot in a key such as "example.com" as path
///     syntax, finds nothing, and the next write dereferences null.
///   - add_value_to_array() and add_value_to_object() refuse an unsigned
///     value beyond INT64_MAX, which the base casts to a negative number.
///   - add_null_to_array() and add_null_to_object() refuse a null, which the
///     base writes as the empty string that does not read back.
///
class toml_writer final : public rfl::toml::Writer
{
public: // Structors
  ///
  /// Constructs a writer over one write's context.
  /// \param root The table to write into. Retained, and must outlive the writer.
  /// \param context The context, which records what cannot be written and
  ///                must outlive the writer.
  ///
  toml_writer(toml::table& root, write_context& context);

public: // Accessors
  ///
  /// Returns the context of the write.
  /// \return The context.
  ///
  [[nodiscard]] auto context() const -> write_context&;

public: // Methods
  ///
  /// Adds an array to a table, keeping a key holding dots one key.
  /// \see toml_writer for why this hides the base member.
  ///
  /// \param name The key. May hold dots.
  /// \param size Reserved size, unused.
  /// \param parent The table to add to. Must not be null.
  /// \return The array added.
  ///
  auto add_array_to_object(std::string_view name, std::size_t size, OutputObjectType* parent) const noexcept
    -> OutputArrayType;

  ///
  /// Adds a table to a table, keeping a key holding dots one key.
  /// \see toml_writer for why this hides the base member.
  ///
  /// \param name The key. May hold dots.
  /// \param size Reserved size, unused.
  /// \param parent The table to add to. Must not be null.
  /// \return The table added.
  ///
  auto add_object_to_object(std::string_view name, std::size_t size, OutputObjectType* parent) const noexcept
    -> OutputObjectType;

  ///
  /// Adds a value to an array, refusing an unsigned value beyond INT64_MAX.
  /// \see toml_writer for why this hides the base member.
  ///
  /// \tparam T The value's type.
  /// \param var The value. Not retained.
  /// \param parent The array to add to. Must not be null.
  /// \return The base writer's result, which is empty; where the value is
  ///         refused, nothing is added and the reason is recorded through
  ///         the context.
  ///
  template<typename T>
  auto add_value_to_array(const T& var, OutputArrayType* parent) const noexcept -> OutputVarType;

  ///
  /// Adds a value to a table, refusing an unsigned value beyond INT64_MAX.
  /// \see toml_writer for why this hides the base member.
  ///
  /// \tparam T The value's type.
  /// \param name The key. May hold dots.
  /// \param var The value. Not retained.
  /// \param parent The table to add to. Must not be null.
  /// \return The base writer's result, which is empty; where the value is
  ///         refused, nothing is added and the reason is recorded through
  ///         the context.
  ///
  template<typename T>
  auto add_value_to_object(std::string_view name, const T& var, OutputObjectType* parent) const noexcept
    -> OutputVarType;

  ///
  /// Refuses a null in an array, which TOML cannot hold.
  /// \see toml_writer for why this hides the base member.
  ///
  /// \param parent The array, unused.
  /// \return Nothing; the reason is recorded through the context.
  ///
  auto add_null_to_array(OutputArrayType* parent) const noexcept -> OutputVarType;

  ///
  /// Refuses a null as a map entry's value, which TOML cannot hold.
  /// \see toml_writer for why this hides the base member.
  ///
  /// \param name The key, unused.
  /// \param parent The table, unused.
  /// \return Nothing; the reason is recorded through the context.
  ///
  auto add_null_to_object(std::string_view name, OutputObjectType* parent) const noexcept -> OutputVarType;

private: // Variables
  ///
  /// The context of the write. Never null.
  ///
  write_context* context_;
};

///
/// How a Parser's parent names the value being written: "'key'" for an
/// object field or a map entry, "an array element" for an array element.
///
/// \tparam P The parent type, as rfl::parsing::Parent<toml_writer> spells it.
/// \param parent The parent.
/// \return The label, for a message.
///
template<typename P>
[[nodiscard]] auto value_label(const P& parent) -> std::string;

///
/// Reports whether text is well-formed UTF-8, which TOML strings hold.
/// toml++ renders bytes that are not valid UTF-8 as escaped code points,
/// which do not read back as the same bytes, so such text is refused
/// rather than silently changed.
///
/// \param text The text. Not retained.
/// \return true if the text is well-formed UTF-8.
///
[[nodiscard]] auto is_valid_utf8(std::string_view text) -> bool;

///
///
template<typename T>
auto toml_writer::add_value_to_array(const T& var, OutputArrayType* const parent) const noexcept -> OutputVarType
{
  using value_t = std::remove_cvref_t<T>;

  if constexpr(std::same_as<value_t, std::string>)
  {
    if(!is_valid_utf8(var))
    {
      context_->fail(std::format("an array element holds text that is not valid UTF-8, which TOML cannot hold."));

      return {};
    }
  }

  if constexpr(std::unsigned_integral<T> && !std::same_as<T, bool>)
  {
    if(!std::in_range<std::int64_t>(var))
    {
      const auto label = value_label(typename rfl::parsing::Parent<toml_writer>::Array{parent});
      context_->fail(std::format("{} holds {}, beyond the largest integer TOML holds.", label, var));

      return {};
    }

    return rfl::toml::Writer::add_value_to_array(static_cast<std::int64_t>(var), parent);
  }
  else
  {
    return rfl::toml::Writer::add_value_to_array(var, parent);
  }
}

///
///
template<typename T>
auto toml_writer::add_value_to_object(
  const std::string_view name, const T& var, OutputObjectType* const parent) const noexcept -> OutputVarType
{
  using value_t = std::remove_cvref_t<T>;

  // A map key reaches here as name, so the key is checked wherever a value
  // kind flows: here for a scalar value, and in add_array_to_object() and
  // add_object_to_object() for an array or a table.
  if(!is_valid_utf8(name))
  {
    context_->fail(std::format("the key '{}' is not valid UTF-8, which TOML cannot hold.", name));
  }

  if constexpr(std::same_as<value_t, std::string>)
  {
    if(!is_valid_utf8(var))
    {
      context_->fail(std::format("'{}' holds text that is not valid UTF-8, which TOML cannot hold.", name));

      return {};
    }
  }

  if constexpr(std::unsigned_integral<T> && !std::same_as<T, bool>)
  {
    if(!std::in_range<std::int64_t>(var))
    {
      context_->fail(std::format("'{}' holds {}, beyond the largest integer TOML holds.", name, var));

      return {};
    }

    return rfl::toml::Writer::add_value_to_object(name, static_cast<std::int64_t>(var), parent);
  }
  else
  {
    return rfl::toml::Writer::add_value_to_object(name, var, parent);
  }
}

///
///
template<typename P>
auto value_label(const P& parent) -> std::string
{
  using parent_t = std::remove_cvref_t<P>;

  if constexpr(std::same_as<parent_t, typename rfl::parsing::Parent<toml_writer>::Object>)
  {
    return std::format("'{}'", parent.name_);
  }
  else
  {
    return std::string{"an array element"};
  }
}

} // namespace arude::config::detail
