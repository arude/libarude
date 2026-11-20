///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// versioned<T>: a member of a configuration that carries its own version.
///
/// The root of a configuration file and every versioned member of it are
/// read through the Parser specialisation below, which is what keeps the
/// whole file on one version rule: a member's table carries a version key,
/// and on read the table is parsed as whichever version of T's previous_t
/// chain the key names, then migrated to T. The specialisation is selected
/// because reflect-cpp's parsers are templated on the reader type, and no
/// specialisation of its own overlaps versioned<T>.
///
/// versioned<T> is not an aggregate on purpose: a plain rfl::toml::read of
/// a root holding one fails to compile rather than silently writing
/// [member.value].
///
#pragma once

#include "arude/config/common.hpp"
#include "arude/config/load_policy.hpp"
#include "arude/config/migration.hpp"
#include "arude/config/toml_reader.hpp"
#include "arude/config/toml_writer.hpp"
#include "arude/exception_report.hpp"
#include "arude/type_name.hpp"

#include <rfl/named_tuple_t.hpp>
#include <rfl/parsing/Parent.hpp>
#include <rfl/parsing/Parser_base.hpp>
#include <rfl/parsing/schema/Type.hpp>
#include <rfl/Result.hpp>
#include <rfl/TaggedUnion.hpp>
#include <rfl/Variant.hpp>
#include <toml++/toml.hpp>

#include <concepts>
#include <cstdint>
#include <format>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace arude::config
{

///
/// A member that carries its own version.
/// A transparent wrapper: it converts from T, hands T out, and compares as
/// its value. On read the member's table is parsed as the version its key
/// names and migrated to T; on write the value is stamped with T's version,
/// whatever the value's version member holds.
///
/// \tparam T The configuration type held, normally the newest version of it.
///
template<configuration_c T>
class versioned final
{
public: // Typedefs
  ///
  /// The configuration type held.
  ///
  using value_t = T;

public: // Structors / Operators
  ///
  /// Constructs a wrapper holding T's default.
  ///
  constexpr versioned() = default;

  ///
  /// Constructs a wrapper holding the value given.
  /// Implicit, so a versioned<T> accepts a T wherever it is used.
  ///
  /// \param value Value to hold.
  ///
  constexpr versioned(T value);

  ///
  /// Compares two wrappers by the values they hold.
  /// \param other Wrapper to compare against.
  /// \return true if both hold equal values.
  ///
  [[nodiscard]] constexpr auto operator==(const versioned& other) const -> bool = default;

public: // Accessors
  ///
  /// Returns the value held.
  /// \return The value.
  ///
  [[nodiscard]] constexpr auto value() const -> const T&;

  ///
  /// Returns the value held, for modification.
  /// \return The value.
  ///
  [[nodiscard]] constexpr auto value() -> T&;

public: // Methods
  ///
  /// Replaces the value held.
  /// \param val Value to hold.
  ///
  constexpr auto value(T val) -> void;

private: // Variables
  ///
  /// The value held.
  ///
  T value_{};
};

///
///
template<configuration_c T>
constexpr versioned<T>::versioned(T value)
  : value_{std::move(value)}
{
}

///
///
template<configuration_c T>
constexpr auto versioned<T>::value() const -> const T&
{
  return value_;
}

///
///
template<configuration_c T>
constexpr auto versioned<T>::value() -> T&
{
  return value_;
}

///
///
template<configuration_c T>
constexpr auto versioned<T>::value(T val) -> void
{
  value_ = std::move(val);
}

} // namespace arude::config

namespace arude::config::detail
{

///
/// Whether T is a versioned<> wrapper.
///
template<typename T>
inline constexpr auto is_versioned_v = false;

///
/// \see is_versioned_v
///
template<configuration_c U>
inline constexpr auto is_versioned_v<versioned<U>> = true;

///
/// Whether T, or anything inside it, is a versioned<> member. Recurse
/// through reflect-cpp's field reflection, the containers a member may be
/// held in, and every variant alternative, so a versioned hidden inside an
/// alternative is found too.
///
template<typename T>
struct contains_versioned;

///
/// Whether any field of a reflected type contains a versioned<> member.
///
template<typename NamedTuple>
struct named_tuple_contains_versioned;

///
/// \see named_tuple_contains_versioned
///
template<typename... Fields>
struct named_tuple_contains_versioned<rfl::NamedTuple<Fields...>>
{
  ///
  /// true if any field's type contains one.
  ///
  static constexpr auto value = (contains_versioned<typename Fields::Type>::value || ...);
};

///
/// Whether any alternative of a variant-like type holds a versioned<> member
/// at any depth. A variant is not read through one type, so the version
/// rules cannot be applied to a table two alternatives could own; the ban in
/// read_root() and write_root() makes that case impossible to build.
///
template<typename T>
struct variant_alternatives
{
  ///
  /// Whether T is a variant-like type at all.
  ///
  static constexpr auto present = false;

  ///
  /// Whether any alternative holds a versioned<> member.
  ///
  static constexpr auto holds_versioned = false;
};

///
/// \see variant_alternatives
///
template<typename... Alternatives>
struct variant_alternatives<std::variant<Alternatives...>>
{
  ///
  /// \see variant_alternatives::present
  ///
  static constexpr auto present = true;

  ///
  /// \see variant_alternatives::holds_versioned
  ///
  static constexpr auto holds_versioned = (contains_versioned<Alternatives>::value || ...);
};

///
/// \see variant_alternatives
///
template<typename... Alternatives>
struct variant_alternatives<rfl::Variant<Alternatives...>>
{
  ///
  /// \see variant_alternatives::present
  ///
  static constexpr auto present = true;

  ///
  /// \see variant_alternatives::holds_versioned
  ///
  static constexpr auto holds_versioned = (contains_versioned<Alternatives>::value || ...);
};

///
/// \see variant_alternatives
///
template<rfl::internal::StringLiteral Discriminator, typename... Alternatives>
struct variant_alternatives<rfl::TaggedUnion<Discriminator, Alternatives...>>
{
  ///
  /// \see variant_alternatives::present
  ///
  static constexpr auto present = true;

  ///
  /// \see variant_alternatives::holds_versioned
  ///
  static constexpr auto holds_versioned = (contains_versioned<Alternatives>::value || ...);
};

///
/// \see contains_versioned
///
template<typename T>
struct contains_versioned
{
  ///
  /// true if T is a versioned<>, or a variant, a reflected aggregate or a
  /// container holding one at any depth.
  ///
  static constexpr auto value = [] -> bool
  {
    if constexpr(is_versioned_v<T>)
    {
      return true;
    }
    else if constexpr(variant_alternatives<T>::present)
    {
      return variant_alternatives<T>::holds_versioned;
    }
    else if constexpr(std::is_aggregate_v<T>)
    {
      return named_tuple_contains_versioned<rfl::named_tuple_t<T>>::value;
    }
    else
    {
      return false;
    }
  }();
};

///
/// \see contains_versioned
///
template<typename U>
struct contains_versioned<std::optional<U>>
{
  ///
  /// true if the element does.
  ///
  static constexpr auto value = contains_versioned<U>::value;
};

///
/// \see contains_versioned
///
template<typename U, typename Allocator>
struct contains_versioned<std::vector<U, Allocator>>
{
  ///
  /// true if an element does.
  ///
  static constexpr auto value = contains_versioned<U>::value;
};

///
/// \see contains_versioned
///
template<typename Key, typename Value, typename Compare, typename Allocator>
struct contains_versioned<std::map<Key, Value, Compare, Allocator>>
{
  ///
  /// true if a key or a value does.
  ///
  static constexpr auto value = contains_versioned<Key>::value || contains_versioned<Value>::value;
};

///
/// \see contains_versioned
///
template<typename Key, typename Value, typename Hash, typename Equal, typename Allocator>
struct contains_versioned<std::unordered_map<Key, Value, Hash, Equal, Allocator>>
{
  ///
  /// true if a key or a value does.
  ///
  static constexpr auto value = contains_versioned<Key>::value || contains_versioned<Value>::value;
};

///
/// Whether T, or anything inside it, is a variant whose alternatives hold a
/// versioned<> member at any depth. The static_assert in read_root() and
/// write_root() refuses such a type, because the version rules cannot be
/// applied to a table that two alternatives could own: which version applied
/// would depend on the order of the alternatives, and a newer version could
/// be silently overwritten.
///
template<typename T>
struct variant_with_versioned;

///
/// Whether any field of a reflected type is a variant whose alternatives
/// hold a versioned<> member.
///
template<typename NamedTuple>
struct named_tuple_variant_with_versioned;

///
/// \see named_tuple_variant_with_versioned
///
template<typename... Fields>
struct named_tuple_variant_with_versioned<rfl::NamedTuple<Fields...>>
{
  ///
  /// true if any field's type is one.
  ///
  static constexpr auto value = (variant_with_versioned<typename Fields::Type>::value || ...);
};

///
/// \see variant_with_versioned
///
template<typename T>
struct variant_with_versioned
{
  ///
  /// true if T is such a variant, or holds one in a field or a container.
  ///
  static constexpr auto value = [] -> bool
  {
    if constexpr(variant_alternatives<T>::present)
    {
      return variant_alternatives<T>::holds_versioned;
    }
    else if constexpr(std::is_aggregate_v<T>)
    {
      return named_tuple_variant_with_versioned<rfl::named_tuple_t<T>>::value;
    }
    else
    {
      return false;
    }
  }();
};

///
/// \see variant_with_versioned
///
template<typename U>
struct variant_with_versioned<std::optional<U>>
{
  ///
  /// true if the element is one.
  ///
  static constexpr auto value = variant_with_versioned<U>::value;
};

///
/// \see variant_with_versioned
///
template<typename U, typename Allocator>
struct variant_with_versioned<std::vector<U, Allocator>>
{
  ///
  /// true if an element is one.
  ///
  static constexpr auto value = variant_with_versioned<U>::value;
};

///
/// \see variant_with_versioned
///
template<typename Key, typename Value, typename Compare, typename Allocator>
struct variant_with_versioned<std::map<Key, Value, Compare, Allocator>>
{
  ///
  /// true if a key or a value is one.
  ///
  static constexpr auto value = variant_with_versioned<Key>::value || variant_with_versioned<Value>::value;
};

///
/// \see variant_with_versioned
///
template<typename Key, typename Value, typename Hash, typename Equal, typename Allocator>
struct variant_with_versioned<std::unordered_map<Key, Value, Hash, Equal, Allocator>>
{
  ///
  /// true if a key or a value is one.
  ///
  static constexpr auto value = variant_with_versioned<Key>::value || variant_with_versioned<Value>::value;
};

///
/// Reads a table as a versioned configuration of T.
/// The version key decides: the table is read as T, migrated to T from the
/// older version the key names, or refused where the key names none, names a
/// newer version, or the policy declines the migration. A missing key reads
/// as T only under a policy with create.
///
/// \tparam T The configuration type to end up at.
/// \tparam ProcessorsType The processors the read runs under.
/// \param reader The reader, for the version key and the context.
/// \param node The node holding the table. Retained by the read, and taken
///             non-const as reflect-cpp takes it.
/// \return The value as T, or why the table is not one.
///
template<configuration_c T, typename ProcessorsType>
[[nodiscard]] auto read_versioned(const toml_reader& reader, toml::node* node) -> rfl::Result<T>;
///
/// Reads a table as the older version its key names and migrates it to To.
/// Walks To's previous_t chain until the version matches; each step keeps
/// the chain checks migration.hpp documents.
///
/// \tparam To The configuration type to end up at.
/// \tparam Candidate The version the walk is currently at.
/// \tparam ProcessorsType The processors the read runs under.
/// \param reader The reader, for the context.
/// \param node The node holding the table. Retained by the read, and taken
///             non-const as reflect-cpp takes it.
/// \param version The version the table declares, which one walk step matches.
/// \return The value as To, or why the table is not one.
///
template<configuration_c To, configuration_c Candidate, typename ProcessorsType>
[[nodiscard]] auto read_older(const toml_reader& reader, toml::node* node, version_t version) -> rfl::Result<To>;

///
/// The message for a table whose version is newer than this build knows.
/// \param type The configuration type, as type_name spells it.
/// \param found The version the table declares.
/// \param newest The newest version this build knows.
/// \return The message, without a prefix.
///
[[nodiscard]] auto newer_message(std::string_view type, version_t found, version_t newest) -> std::string;

///
/// The message for an older table read without upgrade_to_current.
/// \param type The configuration type, as type_name spells it.
/// \param found The version the table declares.
/// \param newest The version asked for.
/// \return The message, without a prefix.
///
[[nodiscard]] auto older_refused_message(std::string_view type, version_t found, version_t newest) -> std::string;

///
/// The message for a version no type of the chain knows.
/// \param type The configuration type, as type_name spells it.
/// \param found The version the table declares.
/// \return The message, without a prefix.
///
[[nodiscard]] auto unknown_version_message(std::string_view type, version_t found) -> std::string;

///
/// The message for a migration whose upgrade() threw.
/// Quotes the exception being handled, so it must be called from a catch
/// block.
///
/// \param type The configuration type, as type_name spells it.
/// \param found The version the table declares.
/// \param newest The version migrated to.
/// \return The message, without a prefix.
///
[[nodiscard]] auto migration_failed_message(std::string_view type, version_t found, version_t newest) -> std::string;

///
///
template<configuration_c T, typename ProcessorsType>
auto read_versioned(const toml_reader& reader, toml::node* const node) -> rfl::Result<T>
{
  // The table stays non-const, as reflect-cpp's parsers take the node
  // non-const all the way down.
  auto* const table = node->as_table();

  if(table == nullptr)
  {
    return rfl::error(std::format("{} is held in a table, and this is not one.", type_name<T>()));
  }

  auto* const version_node = table->get("version");

  if(version_node == nullptr)
  {
    if constexpr(ProcessorsType::default_if_missing_)
    {
      // The table is read as T, whose version member takes its default,
      // which is T::config_version: a file without a version key still
      // reads under create.
      return rfl::parsing::Parser<toml_reader, toml_writer, T, ProcessorsType>::read(reader, node);
    }
    else
    {
      return rfl::error(std::format("{} declares no configuration version.", type_name<T>()));
    }
  }

  const auto version_result = reader.template to_basic_type<version_t>(version_node);

  if(!version_result.has_value())
  {
    // Not an integer, negative, or out of range: a payload error, never a
    // version to compare.
    return rfl::error(version_result.error());
  }

  const auto version = *version_result;

  if(version > T::config_version)
  {
    return rfl::error(
      reader.context().fail(config_error::invalid_version, newer_message(type_name<T>(), version, T::config_version)));
  }

  if(version == T::config_version)
  {
    return rfl::parsing::Parser<toml_reader, toml_writer, T, ProcessorsType>::read(reader, node);
  }

  if(!is_set(reader.context().policy(), load_policy::upgrade_to_current))
  {
    return rfl::error(reader.context().fail(
      config_error::invalid_version, older_refused_message(type_name<T>(), version, T::config_version)));
  }

  if constexpr(has_previous_configuration_c<T>)
  {
    return read_older<T, typename T::previous_t, ProcessorsType>(reader, node, version);
  }
  else
  {
    return rfl::error(
      reader.context().fail(config_error::invalid_version, unknown_version_message(type_name<T>(), version)));
  }
}

///
///
// One call per version of the chain, which is finite and known at compile
// time; the has_previous_configuration_c branch below is where the walk
// stops.
// NOLINTNEXTLINE(misc-no-recursion)
template<configuration_c To, configuration_c Candidate, typename ProcessorsType>
auto read_older(const toml_reader& reader, toml::node* const node, const version_t version) -> rfl::Result<To>
{
  static_assert(
    upgrade_matches_previous<Candidate>(),
    "arude::config: this version names a previous_t whose upgrade() returns another version. "
    "previous_t and upgrade() must describe the same step.");

  if(version == Candidate::config_version)
  {
    const auto found = rfl::parsing::Parser<toml_reader, toml_writer, Candidate, ProcessorsType>::read(reader, node);

    if(!found.has_value())
    {
      // The table is malformed, not of a wrong version: the code stays
      // invalid_payload.
      return rfl::error(found.error());
    }

    try
    {
      return migrate<To>(*found);
    }
    catch(...)
    {
      return rfl::error(reader.context().fail(
        config_error::invalid_version, migration_failed_message(type_name<To>(), version, To::config_version)));
    }
  }

  if constexpr(has_previous_configuration_c<Candidate>)
  {
    return read_older<To, typename Candidate::previous_t, ProcessorsType>(reader, node, version);
  }
  else
  {
    return rfl::error(
      reader.context().fail(config_error::invalid_version, unknown_version_message(type_name<To>(), version)));
  }
}

} // namespace arude::config::detail

namespace rfl::parsing
{

///
/// Reads and writes arude::config::versioned<T> through libarude's reader
/// and writer. Selected over the primary template, which cannot reflect the
/// type's private member.
///
/// \tparam T The configuration type held.
/// \tparam ProcessorsType The processors the read or write runs under.
///
template<arude::config::configuration_c T, typename ProcessorsType>
struct Parser<
  arude::config::detail::toml_reader,
  arude::config::detail::toml_writer,
  arude::config::versioned<T>,
  ProcessorsType>
{
public: // Typedefs
  // NOLINTNEXTLINE(readability-identifier-naming): reflect-cpp's spelling.
  using InputVarType = arude::config::detail::toml_reader::InputVarType;

public: // Methods
  ///
  /// Reads a table as a versioned configuration, by detail::read_versioned.
  /// \param reader The reader, carrying the context.
  /// \param var The node holding the table.
  /// \return The wrapper, or why the table is not a configuration of T.
  ///
  [[nodiscard]] static auto read(const arude::config::detail::toml_reader& reader, const InputVarType& var) noexcept
    -> Result<arude::config::versioned<T>>;

  ///
  /// Writes the value, stamped with T's version.
  /// \tparam P The parent type, as rfl::parsing::Parent spells it.
  /// \param writer The writer.
  /// \param val The wrapper.
  /// \param parent The parent holding the member.
  ///
  template<typename P>
  static auto write(
    const arude::config::detail::toml_writer& writer, const arude::config::versioned<T>& val, const P& parent) noexcept
    -> void;

  ///
  /// Forwards T's schema.
  /// \param definitions Schema definitions collected so far.
  /// \return The schema.
  ///
  static auto to_schema(std::map<std::string, schema::Type>* definitions) -> schema::Type;
};

///
///
template<arude::config::configuration_c T, typename ProcessorsType>
auto Parser<
  arude::config::detail::toml_reader,
  arude::config::detail::toml_writer,
  arude::config::versioned<T>,
  ProcessorsType>::read(const arude::config::detail::toml_reader& reader, const InputVarType& var) noexcept
  -> Result<arude::config::versioned<T>>
{
  try
  {
    const auto value = arude::config::detail::read_versioned<T, ProcessorsType>(reader, var);

    if(!value.has_value())
    {
      return error(value.error());
    }

    return arude::config::versioned<T>{std::move(*value)};
  }
  catch(...)
  {
    // Nothing above is expected to throw, but read() is noexcept and a
    // throwing value or allocator must not terminate the process.
    return error(arude::exception_message());
  }
}

///
///
template<arude::config::configuration_c T, typename ProcessorsType>
template<typename P>
auto Parser<
  arude::config::detail::toml_reader,
  arude::config::detail::toml_writer,
  arude::config::versioned<T>,
  ProcessorsType>::
  write(
    const arude::config::detail::toml_writer& writer, const arude::config::versioned<T>& val, const P& parent) noexcept
  -> void
{
  // The version written is the one the type declares, whatever the value's
  // version member happens to hold, so text cannot come out claiming to be
  // a version it is not.
  auto stamped = val.value();
  stamped.version = T::config_version;

  Parser<arude::config::detail::toml_reader, arude::config::detail::toml_writer, T, ProcessorsType>::write(
    writer, stamped, parent);
}

///
///
template<arude::config::configuration_c T, typename ProcessorsType>
auto Parser<
  arude::config::detail::toml_reader,
  arude::config::detail::toml_writer,
  arude::config::versioned<T>,
  ProcessorsType>::to_schema(std::map<std::string, schema::Type>* definitions) -> schema::Type
{
  return Parser<arude::config::detail::toml_reader, arude::config::detail::toml_writer, T, ProcessorsType>::to_schema(
    definitions);
}

} // namespace rfl::parsing
