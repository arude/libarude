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
/// chain the key names, then migrated to T. The key is libarude's, not the
/// type's: it is written from T::config_version beside the reflected members,
/// and taken out of the table for the length of the read, so the type
/// declares no member for it. The specialisation is selected
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
#include "arude/noncopyable.hpp"
#include "arude/type_name.hpp"

#include <rfl/always_false.hpp>
#include <rfl/Box.hpp>
#include <rfl/Field.hpp>
#include <rfl/Generic.hpp>
#include <rfl/internal/has_reflector.hpp>
#include <rfl/internal/ptr_named_tuple_t.hpp>
#include <rfl/internal/remove_ptrs_nt.hpp>
#include <rfl/parsing/Parent.hpp>
#include <rfl/parsing/Parser_base.hpp>
#include <rfl/parsing/schema/Type.hpp>
#include <rfl/Ref.hpp>
#include <rfl/Result.hpp>
#include <rfl/TaggedUnion.hpp>
#include <rfl/Tuple.hpp>
#include <rfl/Variant.hpp>
#include <toml++/toml.hpp>

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <format>
#include <forward_list>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace arude::config
{

///
/// A member that carries its own version.
/// A transparent wrapper: it converts from T, hands T out, and compares as
/// its value. On read the member's table is parsed as the version its key
/// names and migrated to T; on write the table is given a version key holding
/// T's version.
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
/// Lists types held, for a held_types specialisation to derive from.
/// \tparam Held The types held.
///
template<typename... Held>
struct holding
{
  ///
  /// \see held_types::present
  ///
  static constexpr auto present = true;

  ///
  /// \see held_types::types_t
  ///
  using types_t = std::tuple<Held...>;
};

///
/// The inner type a reflect-cpp wrapper names, if it names one: the
/// ReflectionType of rfl::Validator and rfl::Skip, the Type of rfl::Field,
/// rfl::Flatten, rfl::Rename, rfl::DefaultVal and the like, or the value_type
/// of a container-like wrapper such as rfl::Result. ReflectionType is tried
/// first, as it is the type reflect-cpp's Parser_default reads where no
/// dedicated parser applies; Type is what the wrappers' dedicated parsers
/// read, and value_type the name a container-like wrapper uses.
///
/// \tparam T Type to look into.
/// \tparam Priority Which of the three names is being tried.
/// \tparam Enable Always void; present so a name T lacks is a substitution
///                failure rather than an error.
///
template<typename T, int Priority = 0, typename Enable = void>
struct wrapped_inner
{
  ///
  /// Whether T names an inner type at all.
  ///
  static constexpr auto present = false;

  ///
  /// The inner type, meaningless unless present.
  ///
  using type_t = void;
};

///
/// \see wrapped_inner
///
template<typename T>
struct wrapped_inner<T, 0, std::void_t<typename T::ReflectionType>>
{
  ///
  /// \see wrapped_inner::present
  ///
  static constexpr auto present = true;

  ///
  /// \see wrapped_inner::type_t
  ///
  using type_t = typename T::ReflectionType;
};

///
/// \see wrapped_inner
///
template<typename T>
struct wrapped_inner<T, 1, std::void_t<typename T::Type>>
{
  ///
  /// \see wrapped_inner::present
  ///
  static constexpr auto present = true;

  ///
  /// \see wrapped_inner::type_t
  ///
  using type_t = typename T::Type;
};

///
/// \see wrapped_inner
///
template<typename T>
struct wrapped_inner<T, 2, std::void_t<typename T::value_type>>
{
  ///
  /// \see wrapped_inner::present
  ///
  static constexpr auto present = true;

  ///
  /// \see wrapped_inner::type_t
  ///
  using type_t = typename T::value_type;
};

///
/// The first of the three inner types T names, in the order above.
/// \tparam T Type to look into.
///
template<typename T>
struct wrapped_inner_of
  : std::conditional_t<
      wrapped_inner<T, 0>::present,
      wrapped_inner<T, 0>,
      std::conditional_t<wrapped_inner<T, 1>::present, wrapped_inner<T, 1>, wrapped_inner<T, 2>>>
{};

///
/// What a wrapper holds, or nothing where it names no inner type.
/// A partial specialisation rather than an if constexpr branch, so type_t is
/// never named for a type that has none.
///
/// \tparam T Type to look into.
/// \tparam Present Whether T names an inner type.
///
template<typename T, bool Present = wrapped_inner_of<T>::present>
struct wrapped_held
{
  ///
  /// \see held_types::present
  ///
  static constexpr auto present = false;

  ///
  /// \see held_types::types_t
  ///
  using types_t = std::tuple<>;
};

///
/// \see wrapped_held
///
template<typename T>
struct wrapped_held<T, true>
{
  ///
  /// \see held_types::present
  ///
  static constexpr auto present = true;

  ///
  /// \see held_types::types_t
  ///
  using types_t = std::tuple<typename wrapped_inner_of<T>::type_t>;
};

///
/// What a class with its own rfl::Reflector specialisation holds: reflect-cpp
/// reads and writes such a type as Reflector<T>::ReflType, whatever members
/// the class itself has.
/// \tparam T Type to look into.
/// \tparam Present Whether rfl::Reflector<T> is specialised for T.
///
template<typename T, bool Present = rfl::internal::has_read_reflector<T> || rfl::internal::has_write_reflector<T>>
struct reflector_held : wrapped_held<T, !std::is_aggregate_v<T> && wrapped_inner_of<T>::present>
{};

///
/// \see reflector_held
///
template<typename T>
struct reflector_held<T, true> : holding<typename rfl::Reflector<T>::ReflType>
{};

///
/// The types a wrapper or container holds, for the searches below to look
/// into. Every wrapper reflect-cpp reads as its inner type is covered by
/// wrapped_held, whatever it calls that inner type: rfl::Field, rfl::Flatten,
/// rfl::Rename, rfl::DefaultVal, rfl::Description, rfl::Attribute, rfl::Skip
/// and its two variants, rfl::Validator and rfl::Result among them, and a
/// class with an rfl::Reflector is covered by reflector_held. The rule is
/// deliberately broad: a wrapper this search cannot see through is a compile
/// error the user can work around, where one it does not see through would
/// let newer data be overwritten. A struct is searched through reflect-cpp's
/// field reflection instead, and anything else holds nothing. The containers
/// below are listed so that what they hold is their element or their value,
/// not the container's own value_type.
///
/// \tparam T Type to look into.
///
template<typename T>
struct held_types : reflector_held<T>
{};

///
/// \see held_types
/// rfl::Generic is dynamic: its ReflectionType is a std::optional of a
/// std::variant that holds rfl::Generic again, so following it would not
/// terminate. It holds nothing this search can name.
///
template<>
struct held_types<rfl::Generic> : wrapped_held<rfl::Generic, false>
{};

///
/// \see held_types
///
template<configuration_c U>
struct held_types<versioned<U>> : holding<U>
{};
///
/// \see held_types
/// An untagged variant is searched through its alternatives: it is the
/// alternatives that may hold a versioned<> member or a tagged union.
///
template<typename... Alternatives>
struct held_types<std::variant<Alternatives...>> : holding<Alternatives...>
{};

///
/// \see held_types
///
template<typename... Alternatives>
struct held_types<rfl::Variant<Alternatives...>> : holding<Alternatives...>
{};

///
/// \see held_types
/// A tagged union is searched through its alternatives too, so an untagged
/// variant inside one is found; the alternatives themselves may hold
/// versioned<> members.
///
template<rfl::internal::StringLiteral Discriminator, typename... Alternatives>
struct held_types<rfl::TaggedUnion<Discriminator, Alternatives...>> : holding<Alternatives...>
{};

///
/// \see held_types
/// rfl::Result is read as an untagged variant over its value and an error
/// alternative; its value is what may hold a versioned<> member or a tagged
/// union. Named explicitly rather than through wrapped_inner's value_type,
/// so the intent is stated here rather than derived.
///
template<typename U>
struct held_types<rfl::Result<U>> : holding<U>
{};

///
/// \see held_types
///
template<typename U>
struct held_types<std::optional<U>> : holding<U>
{};

///
/// \see held_types
///
template<typename U, typename... Rest>
struct held_types<std::unique_ptr<U, Rest...>> : holding<U>
{};

///
/// \see held_types
///
template<typename U>
struct held_types<std::shared_ptr<U>> : holding<U>
{};

///
/// \see held_types
///
template<typename U, rfl::Copyability C>
struct held_types<rfl::Box<U, C>> : holding<U>
{};

///
/// \see held_types
///
template<typename U>
struct held_types<rfl::Ref<U>> : holding<U>
{};

///
/// \see held_types
/// Listed because std::array is an aggregate, which reflect-cpp reads as an
/// array rather than a struct.
///
template<typename U, std::size_t N>
struct held_types<std::array<U, N>> : holding<U>
{};

///
/// \see held_types
/// A C array is read element by element by reflect-cpp, not as a struct, so
/// the searches must see into one, which is what the NOLINT below names.
///
template<typename U, std::size_t N>
// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays): the searches must see in.
struct held_types<U[N]> : holding<U>
{};

///
/// \see held_types
/// A pointer is read as the type it points to, where raw pointers are enabled.
///
template<typename U>
struct held_types<U*> : holding<U>
{};

///
/// \see held_types
///
template<typename U, typename... Rest>
struct held_types<std::vector<U, Rest...>> : holding<U>
{};

///
/// \see held_types
///
template<typename U, typename... Rest>
struct held_types<std::deque<U, Rest...>> : holding<U>
{};

///
/// \see held_types
///
template<typename U, typename... Rest>
struct held_types<std::list<U, Rest...>> : holding<U>
{};

///
/// \see held_types
///
template<typename U, typename... Rest>
struct held_types<std::forward_list<U, Rest...>> : holding<U>
{};

///
/// \see held_types
///
template<typename U, typename... Rest>
struct held_types<std::set<U, Rest...>> : holding<U>
{};

///
/// \see held_types
///
template<typename U, typename... Rest>
struct held_types<std::multiset<U, Rest...>> : holding<U>
{};

///
/// \see held_types
///
template<typename U, typename... Rest>
struct held_types<std::unordered_set<U, Rest...>> : holding<U>
{};

///
/// \see held_types
///
template<typename U, typename... Rest>
struct held_types<std::unordered_multiset<U, Rest...>> : holding<U>
{};

///
/// \see held_types
///
template<typename Key, typename Value, typename... Rest>
struct held_types<std::map<Key, Value, Rest...>> : holding<Key, Value>
{};

///
/// \see held_types
///
template<typename Key, typename Value, typename... Rest>
struct held_types<std::multimap<Key, Value, Rest...>> : holding<Key, Value>
{};

///
/// \see held_types
///
template<typename Key, typename Value, typename... Rest>
struct held_types<std::unordered_map<Key, Value, Rest...>> : holding<Key, Value>
{};

///
/// \see held_types
///
template<typename Key, typename Value, typename... Rest>
struct held_types<std::unordered_multimap<Key, Value, Rest...>> : holding<Key, Value>
{};

///
/// \see held_types
///
template<typename First, typename Second>
struct held_types<std::pair<First, Second>> : holding<First, Second>
{};

///
/// \see held_types
///
template<typename... Elements>
struct held_types<std::tuple<Elements...>> : holding<Elements...>
{};

///
/// \see held_types
///
template<typename... Elements>
struct held_types<rfl::Tuple<Elements...>> : holding<Elements...>
{};

///
/// \see held_types
/// A named tuple holds its fields, which reflect-cpp reads as a table like
/// any struct's.
///
template<typename... Fields>
struct held_types<rfl::NamedTuple<Fields...>> : holding<typename Fields::Type...>
{};

///
/// The recursion bound of the two searches below: the most nesting levels a
/// configuration type may have between its root and its deepest member.
/// Every step of a search spends one level, and a reflected aggregate, a
/// container, a wrapper and a pointer each take one, so 64 reaches any real
/// configuration several times over. The searches are lazy — a found result
/// stops further instantiation through std::disjunction — so headroom that
/// is not used costs nothing. A recursive type shows everything it holds
/// within its first cycle, so the bound loses nothing real; only a
/// non-recursive nesting deeper than 64 levels could be missed, and the
/// depth bound that accepts it is what keeps the search linear.
///
inline constexpr auto default_depth = std::size_t{64};

/// Whether T is an rfl::TaggedUnion, the one variant whose alternatives may
/// hold versioned<> members.
template<typename T>
inline constexpr auto is_tagged_v = false;

/// \see is_tagged_v
template<rfl::internal::StringLiteral Discriminator, typename... Alternatives>
inline constexpr auto is_tagged_v<rfl::TaggedUnion<Discriminator, Alternatives...>> = true;

/// Whether T is a std::variant, an rfl::Variant or an rfl::Result: the three
/// untagged variants a versioned configuration must not contain.
template<typename T>
inline constexpr auto is_untagged_variant_v = false;

/// \see is_untagged_variant_v
template<typename... Alternatives>
inline constexpr auto is_untagged_variant_v<std::variant<Alternatives...>> = true;

/// \see is_untagged_variant_v
template<typename... Alternatives>
inline constexpr auto is_untagged_variant_v<rfl::Variant<Alternatives...>> = true;

/// \see is_untagged_variant_v
template<typename U>
inline constexpr auto is_untagged_variant_v<rfl::Result<U>> = true;

/// The leaf predicate of contains_versioned_or_tagged: a versioned<> member,
/// which carries its own version, or an rfl::TaggedUnion, which records a
/// tag no alternative answers to.
/// \tparam T Type to test.
template<typename T>
struct versioned_or_tagged_leaf : std::bool_constant<is_versioned_v<T> || is_tagged_v<T>>
{};

/// The leaf predicate of contains_untagged_variant.
/// \tparam T Type to test.
template<typename T>
struct untagged_variant_leaf : std::bool_constant<is_untagged_variant_v<T>>
{};

///
/// The fields of a struct reflect-cpp reflects, as an rfl::NamedTuple of
/// their declared names and types. What rfl::named_tuple_t gives without
/// processors, built without its detour through rfl::view_t: that takes the
/// decltype of a function template whose return type is deduced, which MSVC
/// refuses as a template argument.
/// \tparam T The struct.
///
template<typename T>
using reflected_fields_t = typename rfl::internal::remove_ptrs_nt<rfl::internal::ptr_named_tuple_t<T>>::NamedTupleType;

template<template<typename> class Leaf, typename Held, std::size_t Depth>
struct search_each;

template<
  template<typename> class Leaf,
  typename T,
  std::size_t Depth,
  bool IsReflected = !held_types<T>::present && std::is_class_v<T> && std::is_aggregate_v<T>>
struct search_reflected;

template<template<typename> class Leaf, typename NamedTuple, std::size_t Depth>
struct search_named_tuple;

/// A depth-bounded search over what a type holds, for a leaf predicate. It is
/// the shared engine of the two traits below, and it is what keeps both cheap:
/// there is no visited pack, so a type is searched once per depth reached
/// rather than once per path to it, and a found result stops the walk through
/// std::disjunction.
///
/// The primary applies the leaf to T, then recurses into what held_types
/// lists and into a reflected aggregate's fields. Depth counts down at every
/// step into a container, a wrapper, a pointer or a field, and Depth 0 stops
/// the descent at the leaf alone, so a recursive type terminates and a
/// non-recursive nesting beyond default_depth is cut off.
///
/// \tparam Leaf The predicate that decides a type, as a bool_constant.
/// \tparam T The type to test.
/// \tparam Depth The number of nesting levels still open below T.
template<template<typename> class Leaf, typename T, std::size_t Depth>
struct search
  : std::disjunction<
      Leaf<T>,
      search_each<Leaf, typename held_types<T>::types_t, Depth - 1>,
      search_reflected<Leaf, T, Depth - 1>>
{};

/// \see search
template<template<typename> class Leaf, typename T>
struct search<Leaf, T, 0> : std::bool_constant<Leaf<T>::value>
{};

/// Folds search over a std::tuple of held types.
/// \tparam Leaf The predicate.
/// \tparam Held The tuple of types.
/// \tparam Depth The number of nesting levels still open below each type.
/// \see search_each
template<template<typename> class Leaf, typename... Held, std::size_t Depth>
struct search_each<Leaf, std::tuple<Held...>, Depth> : std::disjunction<search<Leaf, Held, Depth>...>
{};

/// Reflects a struct into its fields and searches each, where reflect-cpp
/// reads them field by field. A partial specialisation rather than an if
/// constexpr branch, so reflected_fields_t is never named for a type that is
/// not a struct reflect-cpp reflects.
///
/// \tparam Leaf The predicate.
/// \tparam T The type to reflect.
/// \tparam Depth The number of nesting levels still open below a field.
/// \tparam IsReflected Whether T is a struct with no held type for the search.
template<template<typename> class Leaf, typename T, std::size_t Depth, bool IsReflected>
struct search_reflected : std::false_type
{};

/// \see search_reflected
template<template<typename> class Leaf, typename T, std::size_t Depth>
struct search_reflected<Leaf, T, Depth, true> : search_named_tuple<Leaf, reflected_fields_t<T>, Depth>
{};

/// Folds search over the fields of a reflected type.
/// \tparam Leaf The predicate.
/// \tparam NamedTuple The rfl::NamedTuple reflect-cpp reflects the type as.
/// \tparam Depth The number of nesting levels still open below a field.
/// \see search_named_tuple
template<template<typename> class Leaf, typename... Fields, std::size_t Depth>
struct search_named_tuple<Leaf, rfl::NamedTuple<Fields...>, Depth>
  : std::disjunction<search<Leaf, typename Fields::Type, Depth>...>
{};

/// Whether a configuration type contains a versioned<> member or an
/// rfl::TaggedUnion anywhere inside it, at any depth. The counterpart of
/// contains_untagged_variant — see that trait for the rule the two together
/// enforce, and for the one hole in what they can see.
///
/// \tparam T Type to test.
/// \tparam Depth Recursion bound, described under search.
template<typename T, std::size_t Depth = default_depth>
struct contains_versioned_or_tagged : search<versioned_or_tagged_leaf, T, Depth>
{};

/// Whether a configuration type contains a std::variant, an rfl::Variant or
/// an rfl::Result anywhere inside it, at any depth. An rfl::Result is one of
/// the three because reflect-cpp reads it the way it reads an untagged
/// variant: it tries the value, then its error alternative.
///
/// Together with contains_versioned_or_tagged this is the support's one
/// compile-time rule. A type read and written as a configuration may hold
/// whatever it likes. A type that contains a versioned<> member or an
/// rfl::TaggedUnion anywhere must not also contain an untagged variant
/// anywhere: read_root() and write_root() refuse that with a static_assert.
/// The why: reflect-cpp reads an untagged variant as the first alternative
/// that accepts the table, so a newer version reached through a later
/// alternative could be misread or silently overwritten. rfl::TaggedUnion is
/// the way to have a choice in a versioned configuration, because its tag
/// picks the one alternative read. A configuration with no versioned<> and
/// no rfl::TaggedUnion has no version to protect and may use variants
/// freely. The root itself does not count; only versioned<> members and
/// rfl::TaggedUnion alternatives do.
///
/// The search sees through every wrapper and container reflect-cpp reads a
/// configuration through, and into every reflected aggregate's fields, so a
/// variant behind a std::optional, a std::vector, a smart pointer or a field
/// is found. A class the search cannot see into — no reflection, and no
/// wrapper, container or rfl::Reflector — is taken for one that holds
/// nothing: such a type is read through a custom rfl::parsing::Parser, and
/// detecting one is out of scope, so a variant hidden inside one is not
/// refused.
///
/// \tparam T Type to test.
/// \tparam Depth Recursion bound, described under search.
template<typename T, std::size_t Depth = default_depth>
struct contains_untagged_variant : search<untagged_variant_leaf, T, Depth>
{};

///
/// The key a versioned table carries its version under.
///
inline constexpr auto version_key = std::string_view{"version"};

///
/// Takes a table's version key out for the length of a read, and puts it back
/// when destroyed. A configuration type declares no member for the key, so
/// rfl::NoExtraFields would otherwise refuse it as unknown; and the table is
/// the caller's, so it comes back as it was given, for the probe or a second
/// read of the same document. Should putting the key back fail for want of
/// memory, the key stays out rather than the exception escaping.
///
// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions): the copy members come deleted from arude::noncopyable.
class hidden_version_key final : public noncopyable
{
public: // Structors
  ///
  /// Takes the version key out of a table, if it holds one.
  /// \param table The table. Retained, and must outlive this object.
  ///
  explicit hidden_version_key(toml::table& table);

  ///
  /// Puts the version key back.
  ///
  ~hidden_version_key();

private: // Variables
  ///
  /// The table the key was taken out of. Never null.
  ///
  toml::table* table_;

  ///
  /// The key taken out, as the one entry of a table of its own; empty where
  /// the table held none.
  ///
  toml::table saved_;
};

///
/// Returns the table a Parser has just written a value into, found from the
/// parent the value was written under: the root, the key of a table, or the
/// last element of an array.
///
/// \tparam P The parent type, as rfl::parsing::Parent<toml_writer> spells it.
/// \param writer The writer, for the root.
/// \param parent The parent the value was written under.
/// \return The table, or nullptr where the value was not written as one.
///
template<typename P>
[[nodiscard]] auto written_table(const toml_writer& writer, const P& parent) -> toml::table*;

///
/// Reads a table as a versioned configuration of T.
/// The version key decides: the table is read as T, migrated to T from the
/// older version the key names, or refused where the key names none, names a
/// newer version, or the policy declines the migration. A missing key is an
/// invalid_payload under every policy.
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
    if(reader.context().probing())
    {
      // The table is still read as T, for whatever it holds: a member inside
      // it may declare a newer version, and the probe has to find that even
      // though this table does not read.
      std::ignore = rfl::parsing::Parser<toml_reader, toml_writer, T, ProcessorsType>::read(reader, node);
    }

    return rfl::error("the table declares no configuration version.");
  }

  const auto version_result = reader.template to_basic_type<version_t>(version_node);

  if(!version_result.has_value())
  {
    // Not an integer, negative, or out of range: a payload error, never a
    // version to compare.
    return rfl::error(version_result.error());
  }

  const auto version = *version_result;

  // T has no member for the key, so it is out of the table for every read
  // below, older versions included, and back in once they are done.
  const auto hidden = hidden_version_key{*table};

  if(version > T::config_version)
  {
    auto message = newer_message(type_name<T>(), version, T::config_version);
    reader.context().note_newer(message);

    return rfl::error(reader.context().fail(config_error::invalid_version, std::move(message)));
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

///
///
template<typename P>
auto written_table(const toml_writer& writer, const P& parent) -> toml::table*
{
  using parent_t = std::remove_cvref_t<P>;
  using parents_t = rfl::parsing::Parent<toml_writer>;

  if constexpr(std::same_as<parent_t, typename parents_t::Root>)
  {
    return &writer.root();
  }
  else if constexpr(std::same_as<parent_t, typename parents_t::Object>)
  {
    auto* const node = parent.obj_->val_->get(parent.name_);

    return node == nullptr ? nullptr : node->as_table();
  }
  else if constexpr(std::same_as<parent_t, typename parents_t::Array>)
  {
    auto& elements = *parent.arr_->val_;

    return elements.empty() ? nullptr : elements.back().as_table();
  }
  else
  {
    static_assert(rfl::always_false_v<P>, "arude::config: a versioned<> member is written under an unknown parent.");

    return nullptr;
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
  ///
  /// The node type reflect-cpp hands the reader.
  ///
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
  /// Writes the value, and T's version as the table's version key beside its
  /// members. A table that already holds the key, from a member renamed to
  /// it, and a value not written as a table are failures recorded through the
  /// writer's context, and so is whatever the writing throws.
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
  try
  {
    Parser<arude::config::detail::toml_reader, arude::config::detail::toml_writer, T, ProcessorsType>::write(
      writer, val.value(), parent);

    // The key goes in after the members rather than before: toml::table keeps
    // its keys sorted, so the text rendered is the same either way, and a
    // member that took the key first is caught here.
    auto* const table = arude::config::detail::written_table(writer, parent);

    if(table == nullptr)
    {
      writer.context().fail(
        std::format(
          "{} is not written as a table, so it cannot carry a configuration version.", arude::type_name<T>()));

      return;
    }

    const auto version = static_cast<std::int64_t>(arude::config::version_of<T>());

    if(!table->insert(arude::config::detail::version_key, version).second)
    {
      writer.context().fail(
        std::format(
          "{} writes a member as '{}', the key its configuration version is written under.",
          arude::type_name<T>(),
          arude::config::detail::version_key));
    }
  }
  catch(...)
  {
    // write() is noexcept, and reflect-cpp's own writers below it are not: a
    // std::variant that is valueless by exception throws from std::visit.
    // The failure is recorded rather than terminating the process, so the
    // write is refused as any value TOML cannot hold is.
    writer.context().fail(arude::exception_message());
  }
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
