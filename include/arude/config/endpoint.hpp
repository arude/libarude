///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// Endpoints: the formats a configuration can be written in, and the rules a
/// document applies to every one of them.
///
/// An endpoint is a stateful, format-only instance — bytes in, a tree out, and
/// back again. Where the bytes are kept is the transport's business; splitting
/// the two is what lets the same TOML endpoint serve a file today and
/// something else later without being touched.
///
/// Each endpoint keeps its own tree, E::document_t: for TOML, toml++'s own
/// table, so that whatever no section owns is never rebuilt and keeps the type
/// the file gave it. A document never looks inside that tree itself. It asks
/// the endpoint for a tree_reader over it on a load and a tree_writer on a
/// store, and those offer primitives only; the rules below are libarude's,
/// written once on top of them, so they hold for every endpoint alike:
///
///   - whether a section is in the tree, section_present();
///   - what the root section sees, which leaves out the registered named
///     sections and, on a strict read, the tables it does not read;
///   - that a store replaces exactly the keys a section owns.
///
/// A section's value is converted by reflect-cpp's own parser for its type,
/// run over the adapters in tree_reader.hpp and tree_writer.hpp. That is
/// reflect-cpp's extension point for formats, and how its TOML support is
/// built itself, so a section converts as reflect-cpp would convert it.
///
#pragma once

#include "arude/config/common.hpp"
#include "arude/config/load_policy.hpp"
#include "arude/config/migration.hpp"
#include "arude/config/transport.hpp"
#include "arude/config/tree_reader.hpp"
#include "arude/config/tree_writer.hpp"
#include "arude/exception.hpp"
#include "arude/exception_report.hpp"
#include "arude/type_name.hpp"

#include <rfl/Generic.hpp>
#include <rfl/named_tuple_t.hpp>
#include <rfl/NoExtraFields.hpp>
#include <rfl/NoOptionals.hpp>
#include <rfl/parsing/Parent.hpp>
#include <rfl/parsing/Parser.hpp>
#include <rfl/Processors.hpp>
#include <rfl/to_generic.hpp>
#include <rfl/Tuple.hpp>

#include <concepts>
#include <cstddef>
#include <exception>
#include <format>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>

namespace arude::config::detail
{

///
/// The smallest type that is still a configuration.
/// Exists so that what a document does with a configuration can be stated,
/// and instantiated, without one in hand: reading the version of a section
/// of any type is reading this.
///
struct endpoint_probe
{
  ///
  /// The version this type is, known without a value in hand.
  ///
  static constexpr auto config_version = version_t{1};

  ///
  /// The version carried in the text, initialised so the two cannot disagree.
  ///
  version_t version = config_version;
};

///
/// Renders a reflect-cpp error as text.
/// The error type has changed shape across reflect-cpp releases, so the
/// message is asked for rather than assumed: a build against a release that
/// spells it differently loses the detail instead of failing to compile.
///
/// \param error The error reported by reflect-cpp.
/// \return The message it carries, or a stand-in where it carries none.
///
[[nodiscard]] auto error_text(const auto& error) -> std::string;

///
/// What reading a section out of a document yields.
/// Value-initialised, it is what a section that is not in the source takes
/// when it is created: the type's defaults, owning nothing.
///
/// \tparam T Configuration type.
///
template<configuration_c T>
struct parsed_section
{
  ///
  /// The configuration, migrated to T where the policy allowed it.
  ///
  T value{};

  ///
  /// The keys the section owns in the table it was read from: the ones the
  /// version found there serialises to, before any migration. A key that an
  /// older version wrote and the current one has renamed or dropped is among
  /// them, which is what lets a store take it out again; a key the type never
  /// declared is not, so it is left alone as somebody else's.
  ///
  key_set_t owned_keys;
};

///
/// Returns the keys a configuration serialises to.
///
/// \tparam T Configuration type.
/// \param val Configuration to ask. Not retained.
/// \return The keys, which leave out a std::optional member that is disengaged.
///
template<configuration_c T>
[[nodiscard]] auto keys_of(const T& val) -> key_set_t;

///
/// Reports whether reflect-cpp leaves a member out of everything it writes, as
/// it does an rfl::Skip or rfl::SkipSerialization member, renamed or not. An
/// rfl::SkipDeserialization member is written, so it is not left out.
///
/// \tparam T Type of the member, as rfl::named_tuple_t has it.
/// \return Whether a member of that type is never written.
///
template<typename T>
[[nodiscard]] consteval auto skipped_on_write() -> bool;

///
/// Reports whether reflect-cpp leaves a member out of everything it reads, as
/// it does an rfl::Skip or rfl::SkipDeserialization member, renamed or not.
///
/// \tparam T Type of the member, as rfl::named_tuple_t has it.
/// \return Whether a member of that type is never read.
///
template<typename T>
[[nodiscard]] consteval auto skipped_on_read() -> bool;

///
/// Returns every key a configuration of a type can serialise to, whatever its
/// value: keys_of() for a value in which every std::optional member is engaged.
/// Includes the version key. Leaves out a member that is never written, such
/// as an rfl::Skip or rfl::SkipSerialization one.
///
/// \tparam T Configuration type.
/// \return The keys.
///
template<configuration_c T>
[[nodiscard]] auto declared_keys() -> key_set_t;

///
/// Returns every key reading a configuration of a type looks for, as
/// reflect-cpp names it, rfl::Rename and rfl::Flatten included. Leaves out a
/// member that is never read, such as an rfl::Skip or rfl::SkipDeserialization
/// one.
///
/// \tparam T Configuration type.
/// \return The keys.
///
template<configuration_c T>
[[nodiscard]] auto read_keys() -> key_set_t;

///
/// Reports whether a node holds what belongs to other owners as far as the
/// root section can tell: a table, or an array that is not empty and holds
/// tables alone. An empty array is a value like any scalar, as nothing marks
/// it as somebody else's tables.
///
/// \param tree The tree.
/// \param node A node of it.
/// \return true if the node is a table or an array of tables.
///
[[nodiscard]] auto holds_tables(const tree_reader& tree, tree_node node) -> bool;

///
/// Reports whether a section is in a tree.
/// A named section is there when the root has its key, whatever it holds. The
/// root section has no key of its own, so it is there when some top-level
/// entry outside the registered named sections does not hold_tables(): a root
/// that was ever written has its scalar version key, and a top level made of
/// tables and arrays of tables alone holds only what belongs to other owners.
///
/// \param tree The tree to look in.
/// \param name The section name, or empty for the root.
/// \param foreign The names of the registered named sections, which the root
///                section does not see.
/// \return true if the section is there, whatever shape it is in.
///
[[nodiscard]] auto section_present(const tree_reader& tree, std::string_view name, const key_set_t& foreign) -> bool;

///
/// Returns the top-level keys a strict read of the root section passes over:
/// the registered named sections, and every key that holds_tables() and is
/// not one T reads. So a strict root still refuses a misspelt or undeclared
/// value, and still sits beside tables that are somebody else's; a misspelt
/// table field of its own is passed over rather than refused.
///
/// \tparam T Configuration type of the root section.
/// \param tree The tree.
/// \param foreign The names of the registered named sections.
/// \return The keys.
///
template<configuration_c T>
[[nodiscard]] auto root_hidden(const tree_reader& tree, const key_set_t& foreign) -> key_set_t;

///
/// Locates a section's subtree.
///
/// \param tree The tree.
/// \param name The section name, or empty for the root.
/// \return The subtree: the root itself for the root section.
/// \throws arude::exception If a named section is not there.
///
[[nodiscard]] auto acquire_subtree(const tree_reader& tree, std::string_view name) -> tree_node;

///
/// Reads a configuration out of a node of a tree, through reflect-cpp's parser
/// for T. Where the node is the root, the keys in foreign are not seen, nor on
/// a strict read are those root_hidden() adds.
///
/// Throws a plain arude::exception where the node does not hold a T; the
/// caller is what wraps it with the source and the error code.
///
/// \tparam T Configuration type.
/// \param tree The tree.
/// \param node The node to read.
/// \param foreign The names of the registered named sections.
/// \param strict Whether a field T does not declare is an error, which is
///               rfl::NoExtraFields.
/// \return The configuration.
/// \throws arude::exception If the node does not hold what T needs.
/// \throws Whatever the tree threw, rethrown as it was.
///
template<configuration_c T>
[[nodiscard]] auto read_tree(const tree_reader& tree, tree_node node, const key_set_t& foreign, bool strict) -> T;

///
/// Runs reflect-cpp's parser for T over a node, not seeing the hidden keys of
/// the root. What read_tree() is made of.
///
/// \tparam T Configuration type.
/// \param tree The tree.
/// \param node The node to read.
/// \param hidden The keys of the root not to see.
/// \param strict Whether a field T does not declare is an error.
/// \return The configuration.
/// \throws arude::exception If the node does not hold what T needs.
/// \throws Whatever the tree threw, rethrown as it was.
///
template<configuration_c T>
[[nodiscard]] auto read_tree_hiding(const tree_reader& tree, tree_node node, const key_set_t& hidden, bool strict) -> T;

///
/// The version decision for one section.
/// Probes the version in the section's own subtree, returns the exact match,
/// rejects any other version unless upgrade_to_current is given, and migrates
/// otherwise. Reads as strictly as tree.options() says.
///
/// A named section that is not in the tree is an invalid_payload here, as it
/// declares no version. A caller that tells an absent section from a malformed
/// one asks section_present() first.
///
/// \tparam T Configuration type.
/// \param tree The tree to read from.
/// \param name The section name, or empty for the root.
/// \param foreign The names of the registered named sections.
/// \param policy What to do about a differently versioned section.
/// \return The configuration, migrated where the policy allows it, and the
///         keys it owns in the tree.
/// \throws exception_t With invalid_payload or invalid_version, which is also
///                     what a throw from the tree is reported as.
///
template<configuration_c T>
[[nodiscard]] auto
parse_section(const tree_reader& tree, std::string_view name, const key_set_t& foreign, load_policy policy)
  -> parsed_section<T>;

///
/// Parses a subtree as whichever version of To's chain matches its version
/// number. Walks backwards through previous_t, which reaches every version
/// that existed when To was written and no further: a subtree newer than this
/// build is a failure, not something to guess at.
///
/// \tparam To Version to end up at.
/// \tparam Candidate Version being considered.
/// \param tree The tree.
/// \param subtree The section's subtree.
/// \param foreign The names of the registered named sections.
/// \param version The version the subtree declares.
/// \param strict Whether a field the type does not declare is an error.
/// \return The configuration, parsed as the matching version and migrated to
///         To, and the keys the matching version owns in the subtree.
/// \throws arude::exception If no version in the chain matches.
///
template<configuration_c To, configuration_c Candidate>
[[nodiscard]] auto parse_section_chain(
  const tree_reader& tree, tree_node subtree, const key_set_t& foreign, version_t version, bool strict)
  -> parsed_section<To>;

///
/// Writes a configuration into a tree as a section.
/// The version written is the one the type declares, whatever the value's
/// version member happens to hold. An empty name is the root: the tree's own
/// top-level keys; otherwise the value goes into the table under that name,
/// and anything under the name that is not a table is replaced by one.
///
/// Only what the section owns is replaced. Every field the value writes
/// replaces whatever the table held under its key, every owned key it does not
/// write is removed, and every other key keeps its node as it was, type
/// included. With no owned keys given the value is merged over whatever the
/// table holds, and nothing is removed.
///
/// \tparam T Configuration type.
/// \param tree The tree to write into.
/// \param name The section name, or empty for the root.
/// \param val Configuration to write. Not retained.
/// \param owned_keys The keys the section owns in its table, from parsed_section.
/// \throws Whatever the tree threw, rethrown as it was.
///
template<configuration_c T>
auto write_section_value(tree_writer& tree, std::string_view name, const T& val, const key_set_t& owned_keys = {})
  -> void;

///
///
auto error_text(const auto& error) -> std::string
{
  if constexpr(requires { error.what(); })
  {
    return std::string{error.what()};
  }
  else
  {
    return std::string{"no detail reported"};
  }
}

///
///
template<configuration_c T>
auto keys_of(const T& val) -> key_set_t
{
  // A configuration is an aggregate, so its generic form is always a table.
  const auto fields = rfl::to_generic(val).to_object().value();

  return fields | std::views::keys | std::ranges::to<key_set_t>();
}

///
///
template<typename T>
consteval auto skipped_on_write() -> bool
{
  // The tests rfl::parsing::is_empty() makes before writing a member, made on
  // the members rfl::Skip and rfl::Rename declare rather than on the traits in
  // rfl::internal.
  if constexpr(requires { T::skip_serialization_; })
  {
    return T::skip_serialization_;
  }
  else if constexpr(requires {
                      typename T::Type;
                      typename T::Name;
                    })
  {
    return skipped_on_write<typename T::Type>();
  }
  else
  {
    return false;
  }
}

///
///
template<typename T>
consteval auto skipped_on_read() -> bool
{
  // As skipped_on_write(), for the other half of what rfl::Skip declares.
  if constexpr(requires { T::skip_deserialization_; })
  {
    return T::skip_deserialization_;
  }
  else if constexpr(requires {
                      typename T::Type;
                      typename T::Name;
                    })
  {
    return skipped_on_read<typename T::Type>();
  }
  else
  {
    return false;
  }
}

///
///
template<configuration_c T>
auto declared_keys() -> key_set_t
{
  // The same serialisation write_section_value() runs, so the keys are named
  // as it names them, rfl::Rename and rfl::Flatten included. NoOptionals is
  // what makes it name a std::optional member that the default value leaves
  // disengaged: it writes such a member as a null rather than leaving it out.
  const auto fields = rfl::to_generic<rfl::NoOptionals>(T{}).to_object().value();
  auto keys = fields | std::views::keys | std::ranges::to<key_set_t>();

  // NoOptionals writes a skipped member as a null too, which no write without
  // it does. The named tuple has the members flattened and renamed as the
  // serialisation has them, so its names are the ones to take out again.
  using named_t = rfl::named_tuple_t<T>;

  const auto drop = [&keys]<typename TField> -> void
  {
    if constexpr(skipped_on_write<typename TField::Type>())
    {
      keys.erase(std::string{TField::name()});
    }
  };

  [&drop]<std::size_t... Index>(std::index_sequence<Index...>) -> void
  {
    (drop.template operator()<rfl::tuple_element_t<Index, typename named_t::Fields>>(), ...);
  }(std::make_index_sequence<named_t::size()>{});

  return keys;
}

///
///
template<configuration_c T>
auto read_keys() -> key_set_t
{
  // The named tuple has the members flattened and renamed as reflect-cpp reads
  // them, so its names are the keys a read looks for.
  using named_t = rfl::named_tuple_t<T>;

  auto keys = key_set_t{};

  const auto add = [&keys]<typename TField> -> void
  {
    if constexpr(!skipped_on_read<typename TField::Type>())
    {
      keys.emplace(TField::name());
    }
  };

  [&add]<std::size_t... Index>(std::index_sequence<Index...>) -> void
  {
    (add.template operator()<rfl::tuple_element_t<Index, typename named_t::Fields>>(), ...);
  }(std::make_index_sequence<named_t::size()>{});

  return keys;
}

///
///
inline auto holds_tables(const tree_reader& tree, const tree_node node) -> bool
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
inline auto section_present(const tree_reader& tree, const std::string_view name, const key_set_t& foreign) -> bool
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
template<configuration_c T>
auto root_hidden(const tree_reader& tree, const key_set_t& foreign) -> key_set_t
{
  const auto read = read_keys<T>();
  auto hidden = foreign;

  const auto each = [&tree, &read, &hidden](const std::string_view key, const tree_node value) -> void
  {
    if(!read.contains(key) && holds_tables(tree, value))
    {
      hidden.emplace(key);
    }
  };

  tree.for_each_field(tree.root(), field_visitor_for{each});

  return hidden;
}

///
///
inline auto acquire_subtree(const tree_reader& tree, const std::string_view name) -> tree_node
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
template<configuration_c T>
auto read_tree(const tree_reader& tree, const tree_node node, const key_set_t& foreign, const bool strict) -> T
{
  // Only a strict read of the root needs more hidden than the named sections:
  // a lenient one ignores what it does not read anyway, and below the root
  // nothing is hidden at all.
  if(strict && node == tree.root())
  {
    return read_tree_hiding<T>(tree, node, root_hidden<T>(tree, foreign), strict);
  }

  return read_tree_hiding<T>(tree, node, foreign, strict);
}

///
///
template<configuration_c T>
auto read_tree_hiding(const tree_reader& tree, const tree_node node, const key_set_t& hidden, const bool strict) -> T
{
  using strict_parser_t =
    rfl::parsing::Parser<rfl_tree_reader, rfl_tree_writer, T, rfl::Processors<rfl::NoExtraFields>>;
  using lenient_parser_t = rfl::parsing::Parser<rfl_tree_reader, rfl_tree_writer, T, rfl::Processors<>>;

  auto failure = std::exception_ptr{};
  const auto reader = rfl_tree_reader{tree, hidden, failure};

  auto result = strict ? strict_parser_t::read(reader, node) : lenient_parser_t::read(reader, node);

  // A throw from the tree explains whatever reflect-cpp made of the error it
  // was handed in its place, so it is the one reported.
  if(failure)
  {
    std::rethrow_exception(failure);
  }

  if(!result.has_value())
  {
    throw exception{
      std::format("arude::config: it does not hold a {}: {}", type_name<T>(), error_text(result.error()))};
  }

  return std::move(result).value();
}

///
///
template<configuration_c T>
auto parse_section(
  const tree_reader& tree, const std::string_view name, const key_set_t& foreign, const load_policy policy)
  -> parsed_section<T>
{
  const auto source = name.empty() ? std::string_view{"the document"} : name;

  auto version = version_t{0};
  auto subtree = tree_node{};
  auto strict = false;

  try
  {
    strict = tree.options().reject_unknown_fields;
    subtree = acquire_subtree(tree, name);

    // Never strict: the probe declares the version key alone, and every other
    // key is there for the type it is read as next.
    version = read_tree<endpoint_probe>(tree, subtree, foreign, false).version;
  }
  catch(...)
  {
    throw exception_t{
      std::format("arude::config: {} declares no configuration version: {}", source, exception_report()),
      config_error::invalid_payload};
  }

  if(version == version_of<T>())
  {
    try
    {
      auto value = read_tree<T>(tree, subtree, foreign, strict);
      auto owned_keys = keys_of(value);

      return parsed_section<T>{.value = std::move(value), .owned_keys = std::move(owned_keys)};
    }
    catch(...)
    {
      throw exception_t{
        std::format("arude::config: {} is not a {}: {}", source, type_name<T>(), exception_report()),
        config_error::invalid_payload};
    }
  }

  if(!is_set(policy, load_policy::upgrade_to_current))
  {
    throw exception_t{
      std::format("arude::config: {} is version {}, and version {} was asked for.", source, version, version_of<T>()),
      config_error::invalid_version};
  }

  try
  {
    return parse_section_chain<T, T>(tree, subtree, foreign, version, strict);
  }
  catch(...)
  {
    throw exception_t{
      std::format(
        "arude::config: {} cannot be migrated to version {}: {}", source, version_of<T>(), exception_report()),
      config_error::invalid_version};
  }
}

///
///
// One call per version of the chain, which is finite and known at compile
// time; the else branch below is where it stops.
// NOLINTNEXTLINE(misc-no-recursion)
template<configuration_c To, configuration_c Candidate>
auto parse_section_chain(
  const tree_reader& tree,
  const tree_node subtree,
  const key_set_t& foreign,
  const version_t version,
  const bool strict) -> parsed_section<To>
{
  if(version == version_of<Candidate>())
  {
    const auto found = read_tree<Candidate>(tree, subtree, foreign, strict);

    // The keys are those of the version found, not of To: they are what is in
    // the source, and a key that the migration renames or drops has to be
    // among them for a store to take it out.
    return parsed_section<To>{.value = migrate<To>(found), .owned_keys = keys_of(found)};
  }

  if constexpr(has_previous_configuration_c<Candidate>)
  {
    return parse_section_chain<To, typename Candidate::previous_t>(tree, subtree, foreign, version, strict);
  }
  else
  {
    throw exception{
      std::format("arude::config: version {} is not a version of {} that this build knows.", version, type_name<To>())};
  }
}

///
///
template<configuration_c T>
auto write_section_value(tree_writer& tree, const std::string_view name, const T& val, const key_set_t& owned_keys)
  -> void
{
  using parser_t = rfl::parsing::Parser<rfl_tree_reader, rfl_tree_writer, T, rfl::Processors<>>;

  auto stamped = val;
  stamped.version = version_of<T>();

  const auto root = tree.root();
  const auto target = name.empty() ? root : tree.table(root, name);

  auto written = key_set_t{};
  auto failure = std::exception_ptr{};
  const auto writer = rfl_tree_writer{tree, target, written, failure};

  // Straight into the section's own table: each field the writer adds at its
  // top first erases the key, so the table ends up with the value's fields
  // and whatever else it held, and nothing has to be merged afterwards.
  parser_t::write(writer, stamped, typename rfl::parsing::Parent<rfl_tree_writer>::Root{});

  if(failure)
  {
    std::rethrow_exception(failure);
  }

  for(const auto& key : owned_keys)
  {
    if(!written.contains(key))
    {
      tree.erase(target, key);
    }
  }
}

} // namespace arude::config::detail

namespace arude::config
{

///
/// What a document needs of an endpoint's tree, whatever carries it.
///
/// E::document_t is the endpoint's own tree, which a document default
/// constructs, copies on every store and assigns on every load. make_reader()
/// and make_writer() return the endpoint's tree_reader and tree_writer over
/// one tree, by value: a document builds one per load and one per store, on
/// the stack, and they need be neither copyable nor movable for that. A reader
/// retains the tree it is given, so an endpoint should delete the overload
/// that takes a temporary one.
///
/// The operations are const members on an instance, which is what lets two
/// documents share one format value; an endpoint that mutated itself while
/// reading would race.
///
/// \tparam E Type to test.
///
template<typename E>
concept tree_endpoint_c =
  std::move_constructible<E> &&
  std::semiregular<typename E::document_t> &&
  requires(const E& endpoint, typename E::document_t& document, const typename E::document_t& const_document) {
    { E::name } -> std::convertible_to<std::string_view>; // Label, for diagnostics.
    { endpoint.make_reader(const_document) } -> std::derived_from<tree_reader>;
    { endpoint.make_writer(document) } -> std::derived_from<tree_writer>;
  };

///
/// An endpoint usable with the transport Tr: a tree_endpoint_c that can parse
/// a tree out of what Tr reads and emit one through it.
///
/// parse() returns std::nullopt exactly where the transport's read() does, and
/// throws where the bytes are not a tree of its format, preferably an
/// exception_t with invalid_payload. Stated against the transport it is used
/// with, rather than against a stand-in, so that an endpoint may constrain its
/// parse() and emit() further, to path_transport_c say, and is then refused at
/// the interface where it is paired with a transport that does not fit.
///
/// \tparam E Type to test.
/// \tparam Tr Transport the endpoint is to be used with.
///
template<typename E, typename Tr>
concept endpoint_c = transport_c<Tr> &&
                     tree_endpoint_c<E> &&
                     requires(const E& endpoint, const Tr& carrier, const typename E::document_t& document) {
                       { endpoint.parse(carrier) } -> std::same_as<std::optional<typename E::document_t>>;
                       { endpoint.emit(carrier, document) } -> std::same_as<void>;
                     };

///
/// Reads one section out of an endpoint's tree, as a document would with no
/// other section registered: an empty name is the root, which then sees every
/// top-level key. Reads as strictly as the endpoint's reader says.
///
/// The version decision is the section's own: its subtree declares its
/// version, independently of every other section. The root section's version
/// is the document's version — there is no second document-level version, and
/// cross-section migration is what a root section's upgrade does.
///
/// \tparam T Configuration type.
/// \tparam E Endpoint type.
/// \param format The endpoint.
/// \param document The tree to read from. Not retained.
/// \param name The section name, or empty for the root.
/// \param policy What to do about a differently versioned section.
/// \return The configuration, migrated where the policy allows it.
/// \throws exception_t With invalid_payload, which is also what a named
///                     section that is not in the tree is, or with
///                     invalid_version.
///
template<configuration_c T, tree_endpoint_c E>
[[nodiscard]] auto
read_section(const E& format, const typename E::document_t& document, std::string_view name, load_policy policy) -> T;

///
/// Writes one section into an endpoint's tree. An empty name is the root. The
/// version written is the one the type declares, whatever the value's version
/// member happens to hold, so a tree cannot come out claiming to be a version
/// it is not.
///
/// The keys the value writes replace what the section's table held under
/// them, and every other key in the table is left as it is: nothing is
/// removed, as nothing is known to be owned.
///
/// \tparam T Configuration type.
/// \tparam E Endpoint type.
/// \param format The endpoint.
/// \param document The tree to write into.
/// \param name The section name, or empty for the root.
/// \param val Configuration to write. Not retained.
/// \throws exception_t Whatever the endpoint's writer threw, where that is an
///                     exception_t, and with io_error for anything else, as a
///                     document's store() reports it. The tree may hold part
///                     of the section then.
///
template<configuration_c T, tree_endpoint_c E>
auto write_section(const E& format, typename E::document_t& document, std::string_view name, const T& val) -> void;

///
///
template<configuration_c T, tree_endpoint_c E>
auto read_section(
  const E& format, const typename E::document_t& document, const std::string_view name, const load_policy policy) -> T
{
  const auto tree = format.make_reader(document);

  return detail::parse_section<T>(tree, name, {}, policy).value;
}

///
///
template<configuration_c T, tree_endpoint_c E>
auto write_section(const E& format, typename E::document_t& document, const std::string_view name, const T& val) -> void
{
  try
  {
    auto tree = format.make_writer(document);

    detail::write_section_value<T>(tree, name, val);
  }
  catch(const exception_t&)
  {
    throw;
  }
  catch(...)
  {
    const auto target = name.empty() ? std::string_view{"the document"} : name;

    throw exception_t{
      std::format("arude::config: {} could not be written: {}", target, exception_report()), config_error::io_error};
  }
}

} // namespace arude::config
