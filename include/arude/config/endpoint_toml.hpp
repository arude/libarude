///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// The TOML endpoint, which is what a configuration is written in by default.
///
/// The free functions at the end convert between a configuration and TOML text
/// with no transport in the way, for a configuration that arrived as a string
/// rather than from somewhere. The endpoint itself reads and writes a whole
/// document through a transport.
///
#pragma once

#include "arude/config/common.hpp"
#include "arude/config/endpoint.hpp"
#include "arude/config/load_policy.hpp"
#include "arude/config/migration.hpp"
#include "arude/config/toml_tree_reader.hpp"
#include "arude/config/toml_tree_writer.hpp"
#include "arude/config/transport.hpp"
#include "arude/config/tree_reader.hpp"

#include <toml++/toml.hpp>

#include <optional>
#include <source_location>
#include <string>
#include <string_view>

namespace arude::config::detail
{

///
/// What an error message calls text that did not come from a transport.
///
inline constexpr auto text_source = std::string_view{"<text>"};

///
/// Parses TOML text into a table.
/// Shared by endpoint_toml::parse and by the text-level free functions, so the
/// two cannot disagree about what counts as a TOML document.
///
/// \param text TOML text. Not retained.
/// \param source What the text came from, used in error messages only.
/// \param loc The call site a failure reports.
/// \return The parsed table.
/// \throws exception_t With invalid_payload if the text is not a TOML document.
///
[[nodiscard]] auto read_toml_document(std::string_view text, std::string_view source, std::source_location loc)
  -> toml::table;

///
/// Renders a table as TOML text, as rfl::toml::write renders one.
///
/// \param document The table. Not retained.
/// \return The text.
///
[[nodiscard]] auto write_toml_document(const toml::table& document) -> std::string;

} // namespace arude::config::detail

namespace arude::config
{

///
/// How the TOML endpoint should behave. Passive, so an application sets what
/// it cares about and leaves the rest.
///
struct endpoint_toml_options
{
  ///
  /// Whether a section read fails on a field its type does not declare.
  /// Maps onto reflect-cpp's rfl::NoExtraFields processor, and applies to
  /// every section read through a document over this endpoint, and through
  /// read_section(). A named section is strict over its whole table. The root
  /// section is strict over the top level, less the names of the registered
  /// sections and every table or array of tables its type does not read: those
  /// are somebody else's as far as the root can tell, and tables nobody
  /// registered survive. So a misspelt value at the top level is refused, and
  /// a misspelt table field of the root's type is passed over.
  ///
  bool reject_unknown_fields = false;
};

///
/// The TOML endpoint, which is what a configuration is written in by default.
/// A stateful instance whose operations are const members, so two documents
/// can share one endpoint value without racing.
///
/// Its tree is toml++'s own table. What no section owns is never rebuilt, and
/// keeps the TOML type the file gave it across a store: an offset or a local
/// date-time, a local date, a local time, an integer as against a float, an
/// array of tables, an inline table. That covers tables nobody registered,
/// top-level keys beside the root, and keys at the top of a section's table
/// that the section's type does not declare. A field the type declares is
/// written whole, so a key inside such a field that the type does not declare
/// is gone after a store.
///
/// What a store does not keep:
///
///   - Comments. toml++ does not keep them, so a store writes a file without
///     the comments it was read with.
///   - Layout. Keys come out in toml++'s order, sorted and values before
///     tables; a dotted key comes back as a table; numbers and strings are
///     written in toml++'s style. Types and values are kept, and an integer
///     no section owns keeps its base. A section's own fields are written
///     afresh, so an integer field the type declares comes back in decimal.
///
/// How a section's own fields are written is reflect-cpp's business:
///
///   - TOML has no null, and an unset std::optional is left out of the file
///     rather than spelt as anything; a field that reads back absent is
///     disengaged again.
///   - reflect-cpp maps no type onto a TOML date or time. An rfl::Timestamp
///     is written as a string in its pattern and read back from one, and a
///     native TOML date under its key is refused as invalid_payload. A
///     std::chrono::duration is written as a table of a count and a unit.
///     A native date or time survives only as data no section owns.
///
class endpoint_toml final
{
public: // Typedefs / Constants
  ///
  /// The endpoint's name, as a plain label for diagnostics.
  ///
  static constexpr auto name = std::string_view{"toml"};

  ///
  /// The endpoint's tree: toml++'s table, as parsed.
  ///
  using document_t = toml::table;

public: // Structors
  ///
  /// Constructs an endpoint with the default options.
  ///
  endpoint_toml() = default;

  ///
  /// Constructs an endpoint with the options given.
  /// \param options How it should behave. Copied.
  ///
  explicit endpoint_toml(endpoint_toml_options options);

public: // Accessors
  ///
  /// Reads a whole document from a transport, in one read, and parses it.
  ///
  /// \tparam Tr Transport type. A parameter rather than a member, so a
  ///             medium-integrated endpoint can constrain it further and fail
  ///             at the interface when mispaired.
  /// \param carrier Where the bytes come from.
  /// \param loc Where the call was made, which is what a failure reports. Left to its default.
  /// \return The parsed table, or std::nullopt where the transport has
  ///         nothing at its address.
  /// \throws exception_t With invalid_payload if the bytes are not a TOML
  ///                     document, or whatever the transport throws.
  ///
  template<transport_c Tr>
  [[nodiscard]] auto parse(const Tr& carrier, std::source_location loc = std::source_location::current()) const
    -> std::optional<document_t>;

  ///
  /// Returns a reader over a table, which carries this endpoint's options to
  /// every section read through it.
  ///
  /// \param document The table. Retained by the reader, which must not outlive it.
  /// \return The reader.
  ///
  [[nodiscard]] auto make_reader(const document_t& document) const -> toml_tree_reader;

  ///
  /// Deleted, as the reader would outlive the table it reads.
  /// \param document The table.
  /// \return Nothing.
  ///
  auto make_reader(document_t&& document) const -> toml_tree_reader = delete;

public: // Methods
  ///
  /// Writes a whole document through a transport.
  ///
  /// \tparam Tr Transport type, as for parse.
  /// \param carrier Where the bytes go.
  /// \param document The table to write.
  /// \param loc Where the call was made, which is what a failure reports. Left to its default.
  /// \throws Whatever the transport throws.
  ///
  template<transport_c Tr>
  auto
  emit(const Tr& carrier, const document_t& document, std::source_location loc = std::source_location::current()) const
    -> void;

  ///
  /// Returns a writer over a table.
  ///
  /// \param document The table. Retained by the writer, which must not outlive it.
  /// \return The writer.
  ///
  [[nodiscard]] auto make_writer(document_t& document) const -> toml_tree_writer;

private: // Variables
  endpoint_toml_options options_;
};

///
/// Renders a configuration as TOML text.
/// The text-level counterpart to write_section(), for a configuration that has
/// to become a string rather than reach a transport. The version written is
/// the one the type declares, whatever the value's version member happens to
/// hold, so text cannot come out claiming to be a version it is not.
///
/// \tparam T Configuration type.
/// \param val Configuration to render. Not retained.
/// \param loc Where the call was made, which is what a failure reports. Left to its default.
/// \return The TOML text.
/// \throws exception_t As write_section() does.
///
template<configuration_c T>
[[nodiscard]] auto to_toml(const T& val, std::source_location loc = std::source_location::current()) -> std::string;

///
/// Parses TOML text as a configuration of exactly T's version.
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
/// \throws exception_t With invalid_payload if the text does not parse, or carries no version.
///
[[nodiscard]] auto toml_version(std::string_view text, std::source_location loc = std::source_location::current())
  -> version_t;

///
///
template<transport_c Tr>
auto endpoint_toml::parse(const Tr& carrier, const std::source_location loc) const -> std::optional<document_t>
{
  const auto bytes = detail::read_from(carrier, loc);

  if(!bytes.has_value())
  {
    return std::nullopt;
  }

  return detail::read_toml_document(as_text(bytes.value()), carrier.location(), loc);
}

///
///
template<transport_c Tr>
auto endpoint_toml::emit(const Tr& carrier, const document_t& document, const std::source_location loc) const -> void
{
  detail::write_to(carrier, as_bytes(detail::write_toml_document(document)), loc);
}

///
///
template<configuration_c T>
auto to_toml(const T& val, const std::source_location loc) -> std::string
{
  auto document = endpoint_toml::document_t{};
  write_section(endpoint_toml{}, document, {}, val, loc);

  return detail::write_toml_document(document);
}

///
///
template<configuration_c T>
auto from_toml(const std::string_view text, const std::source_location loc) -> T
{
  // load_policy::none, so a version other than T's is refused rather than
  // migrated: from_toml_migrated is what accepts an older one.
  return read_section<T>(
    endpoint_toml{}, detail::read_toml_document(text, detail::text_source, loc), {}, load_policy::none, loc);
}

///
///
template<configuration_c To>
auto from_toml_migrated(const std::string_view text, const std::source_location loc) -> To
{
  return read_section<To>(
    endpoint_toml{},
    detail::read_toml_document(text, detail::text_source, loc),
    {},
    load_policy::upgrade_to_current,
    loc);
}

} // namespace arude::config
