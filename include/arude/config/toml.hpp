///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// The TOML format of a configuration file, and the reading and writing of
/// one whole root.
///
/// A configuration is read and written whole through detail::read_root() and
/// detail::write_root(), which drive reflect-cpp's parser the way
/// rfl/toml/read.hpp and write.hpp do: they build libarude's own reader and
/// writer and call the Parser with them, so the specialisations in
/// versioned.hpp and toml_parsers.hpp are the ones chosen. The root itself
/// is read through the versioned specialisation, so the root and its
/// versioned members share one version rule.
///
#pragma once

#include "arude/config/common.hpp"
#include "arude/config/load_policy.hpp"
#include "arude/config/migration.hpp"
#include "arude/config/toml_parsers.hpp"
#include "arude/config/versioned.hpp"
#include "arude/type_name.hpp"

#include <rfl.hpp>
#include <rfl/parsing/Parent.hpp>
#include <rfl/parsing/Parser_base.hpp>
#include <rfl/Result.hpp>
#include <toml++/toml.hpp>

#include <concepts>
#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace arude::config
{

///
/// The TOML format and its options. Passive, so an application sets what it
/// cares about and leaves the rest.
///
struct toml_format
{
  ///
  /// Whether a read fails on a key the type does not declare, at every
  /// level: the root, a versioned member, and an older version a member is
  /// read as. Maps onto reflect-cpp's rfl::NoExtraFields.
  ///
  bool reject_unknown_fields = false;
};

} // namespace arude::config

namespace arude::config::detail
{

///
/// What an error message calls text that did not come from a transport.
///
inline constexpr auto text_source = std::string_view{"<text>"};

///
/// Parses TOML text into a table.
///
/// \param text TOML text. Not retained.
/// \param source What the text came from, used in error messages only.
/// \param loc The call site a failure reports.
/// \return The parsed table.
/// \throws exception_t With invalid_payload if the text is not a TOML document.
///
[[nodiscard]] auto parse_toml(std::string_view text, std::string_view source, std::source_location loc) -> toml::table;

///
/// Renders a table as TOML text, as rfl::toml::write renders one.
///
/// \param document The table. Not retained.
/// \return The text.
///
[[nodiscard]] auto render_toml(const toml::table& document) -> std::string;

///
/// Parses a whole document as the root, read through the versioned
/// specialisation with the processors given.
///
/// \tparam T The root's configuration type.
/// \tparam Ps The processors the read runs under.
/// \param document The table. Retained by the read, not after it.
/// \param context The context the read records into.
/// \return The root, or why the document is not one.
///
template<configuration_c T, typename... Ps>
[[nodiscard]] auto parse_root(toml::table& document, read_context& context) -> rfl::Result<versioned<T>>;

///
/// Reads a whole document as a configuration of T, under a load policy and
/// a format. The document is the caller's own: reading it does not touch
/// any transport, and the caller decides what the source is called.
///
/// \tparam T The root's configuration type.
/// \param document The table. Retained by the read, not after it.
/// \param policy What load() should do about a missing or older root.
/// \param format The format's options, which pick the processors.
/// \param source What the document came from, used in error messages only.
/// \param loc The call site a failure reports.
/// \return The root.
/// \throws exception_t With invalid_payload if the document does not read as
///         T, or invalid_version if it declares a version this build cannot
///         load under the policy.
///
template<configuration_c T>
[[nodiscard]] auto read_root(
  toml::table& document,
  load_policy policy,
  const toml_format& format,
  std::string_view source,
  std::source_location loc) -> T;

///
/// Writes a configuration as a table, stamped with T's version.
/// A value that cannot be written as TOML is an exception, not partial
/// output: the table is discarded and no caller ever writes it out.
///
/// \tparam T The configuration type.
/// \param value The value to write. Not retained.
/// \param loc The call site a failure reports.
/// \return The table.
/// \throws exception_t With invalid_payload if the value cannot be written
///         as TOML.
///
template<configuration_c T>
[[nodiscard]] auto write_root(const T& value, std::source_location loc) -> toml::table;

///
/// Throws the exception a failed read reports, with the code the context
/// recorded for the error that survived the read.
///
/// \param source What the document came from, used in the message only.
/// \param type The configuration type, as type_name spells it.
/// \param error The error reflect-cpp reports for the whole read, whose text
///              carries the field path and the recorded message.
/// \param context The context of the read, whose recorded failures name the code.
/// \param loc The call site a failure reports.
/// \throws exception_t With the recorded code, or invalid_payload if none fits.
///
[[noreturn]] auto raise_read_failure(
  std::string_view source,
  std::string_view type,
  const rfl::Error& error,
  const read_context& context,
  std::source_location loc) -> void;

///
/// Throws the exception a failed write reports.
///
/// \param type The configuration type, as type_name spells it.
/// \param message The reason the value cannot be written, without a prefix.
/// \param loc The call site a failure reports.
/// \throws exception_t With invalid_payload.
///
[[noreturn]] auto raise_write_failure(std::string_view type, std::string_view message, std::source_location loc)
  -> void;
///
///
template<configuration_c T, typename... Ps>
auto parse_root(toml::table& document, read_context& context) -> rfl::Result<versioned<T>>
{
  auto reader = toml_reader{context};
  auto* const root = static_cast<toml::node*>(&document);

  return rfl::parsing::Parser<toml_reader, toml_writer, versioned<T>, rfl::Processors<Ps...>>::read(reader, root);
}

///
///
template<configuration_c T>
auto read_root(
  toml::table& document,
  const load_policy policy,
  const toml_format& format,
  const std::string_view source,
  const std::source_location loc) -> T
{
  static_assert(
    !variant_with_versioned<T>::value,
    "arude::config: versioned<> cannot sit inside a std::variant, rfl::Variant or rfl::TaggedUnion "
    "alternative. One table cannot be read as two versions at once: which version applied would depend on "
    "which alternative accepted the table, and a newer version could be silently overwritten. Move the "
    "versioned member out of the variant, or keep every alternative free of versioned<> members at any depth.");

  // The processor combinations are compile-time, so the ones the policy and
  // the format can ask for are instantiated here and picked at run time.
  const auto create = is_set(policy, load_policy::create);
  const auto strict = format.reject_unknown_fields;

  auto context = read_context{policy};

  const auto parsed = [&]() -> rfl::Result<versioned<T>>
  {
    if(create && strict)
    {
      return parse_root<T, rfl::DefaultIfMissing, rfl::NoExtraFields>(document, context);
    }

    if(create)
    {
      return parse_root<T, rfl::DefaultIfMissing>(document, context);
    }

    if(strict)
    {
      return parse_root<T, rfl::NoExtraFields>(document, context);
    }

    return parse_root<T>(document, context);
  }();

  if(!parsed.has_value())
  {
    raise_read_failure(source, type_name<T>(), parsed.error(), context, loc);
  }

  return std::move(parsed.value().value());
}

///
///
template<configuration_c T>
auto write_root(const T& value, const std::source_location loc) -> toml::table
{
  static_assert(
    !variant_with_versioned<T>::value,
    "arude::config: versioned<> cannot sit inside a std::variant, rfl::Variant or rfl::TaggedUnion "
    "alternative. One table cannot be written as two versions at once: which version applied would depend on "
    "which alternative accepted the table, and a newer version could be silently overwritten. Move the "
    "versioned member out of the variant, or keep every alternative free of versioned<> members at any depth.");

  auto document = toml::table{};
  auto context = write_context{};
  auto writer = toml_writer{document, context};

  rfl::parsing::Parser<toml_reader, toml_writer, versioned<T>, rfl::Processors<>>::write(
    writer, versioned<T>{value}, rfl::parsing::Parent<toml_writer>::Root{});

  if(context.failure().has_value())
  {
    raise_write_failure(type_name<T>(), *context.failure(), loc);
  }

  return document;
}

} // namespace arude::config::detail

namespace arude::config
{

///
/// Renders a configuration as TOML text.
/// The text-level counterpart to config_file::store(), for a configuration
/// that has to become a string rather than reach a transport. The version
/// written is the one the type declares, whatever the value's version member
/// happens to hold, so text cannot come out claiming to be a version it is not.
///
/// \tparam T Configuration type.
/// \param val Configuration to render. Not retained.
/// \param loc Where the call was made, which is what a failure reports. Left to its default.
/// \return The TOML text.
/// \throws exception_t With invalid_payload if the value cannot be written
///         as TOML.
///
template<configuration_c T>
[[nodiscard]] auto to_toml(const T& val, std::source_location loc = std::source_location::current()) -> std::string;

///
/// Parses TOML text as a configuration of exactly T's version.
/// Exact versions throughout, root and members alike.
///
/// \tparam T Configuration type.
/// \param text TOML text. Not retained.
/// \param loc Where the call was made, which is what a failure reports. Left to its default.
/// \return The parsed configuration.
/// \throws exception_t With invalid_payload if the text does not parse, or
///         invalid_version if it declares a version other than T's.
///
template<configuration_c T>
[[nodiscard]] auto from_toml(std::string_view text, std::source_location loc = std::source_location::current()) -> T;

///
/// Parses TOML text as any version To knows, and migrates it to To.
/// Text newer than this build is refused rather than guessed at.
///
/// \tparam To Configuration version to end up at, usually the current one.
/// \param text TOML text. Not retained.
/// \param loc Where the call was made, which is what a failure reports. Left to its default.
/// \return The configuration, migrated to To.
/// \throws exception_t With invalid_payload if the text does not parse, or
///         invalid_version if it declares a version this build has no type for.
///
template<configuration_c To>
[[nodiscard]] auto from_toml_migrated(std::string_view text, std::source_location loc = std::source_location::current())
  -> To;

///
/// Returns the configuration version TOML text declares.
/// Reads the version key alone, so it answers for text of any version,
/// including one this build has no type for.
///
/// \param text TOML text. Not retained.
/// \param loc Where the call was made, which is what a failure reports. Left to its default.
/// \return The version the text declares.
/// \throws exception_t With invalid_payload if the text does not parse, or
///         carries no readable version.
///
[[nodiscard]] auto toml_version(std::string_view text, std::source_location loc = std::source_location::current())
  -> version_t;

///
///
template<configuration_c T>
auto to_toml(const T& val, const std::source_location loc) -> std::string
{
  return detail::render_toml(detail::write_root(val, loc));
}

///
///
template<configuration_c T>
auto from_toml(const std::string_view text, const std::source_location loc) -> T
{
  // load_policy::none, so a version other than T's is refused rather than
  // migrated: from_toml_migrated is what accepts an older one.
  auto document = detail::parse_toml(text, detail::text_source, loc);

  return detail::read_root<T>(document, load_policy::none, toml_format{}, detail::text_source, loc);
}

///
///
template<configuration_c To>
auto from_toml_migrated(const std::string_view text, const std::source_location loc) -> To
{
  auto document = detail::parse_toml(text, detail::text_source, loc);

  return detail::read_root<To>(document, load_policy::upgrade_to_current, toml_format{}, detail::text_source, loc);
}

} // namespace arude::config
