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
/// rather than from somewhere. Everything else here goes through a transport.
///
#pragma once

#include "arude/config/common.hpp"
#include "arude/config/endpoint.hpp"
#include "arude/config/load_policy.hpp"
#include "arude/config/migration.hpp"
#include "arude/config/transport.hpp"
#include "arude/exception_report.hpp"

#include <rfl/toml/read.hpp>
#include <rfl/toml/write.hpp>

#include <cstddef>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace arude::config::detail
{

///
/// What an error message calls text that did not come from a transport.
///
inline constexpr auto text_source = std::string_view{"<text>"};

///
/// Parses TOML text into a document.
/// Shared by endpoint_toml::parse and by the text-level free functions, so the
/// two cannot disagree about what counts as a TOML document.
///
/// \param text TOML text. Not retained.
/// \param source What the text came from, used in error messages only.
/// \return The parsed document.
/// \throws exception_t With invalid_payload if the text is not a TOML document.
///
[[nodiscard]] auto read_toml_document(std::string_view text, std::string_view source) -> rfl::Generic::Object;

///
///
inline auto read_toml_document(const std::string_view text, const std::string_view source) -> rfl::Generic::Object
{
  try
  {
    auto result = rfl::toml::read<rfl::Generic>(std::string{text});

    if(!result.has_value())
    {
      throw exception_t{
        std::format("arude::config: '{}' is not valid TOML: {}", source, error_text(result.error())),
        config_error::invalid_payload};
    }

    auto object = result.value().to_object();

    if(!object.has_value())
    {
      throw exception_t{
        std::format("arude::config: '{}' is not a TOML document.", source), config_error::invalid_payload};
    }

    return std::move(object).value();
  }
  catch(const exception_base&)
  {
    // Already translated by the branches above; pass it through untouched.
    throw;
  }
  catch(...)
  {
    // rfl::toml::read calls ::toml::parse directly, and toml++ throws rather
    // than reporting through reflect-cpp's Result when the text is not TOML
    // at all, so the throw never reaches the Result handling above.
    throw exception_t{
      std::format("arude::config: '{}' is not valid TOML: {}", source, exception_report()),
      config_error::invalid_payload};
  }
}

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
  /// Maps onto reflect-cpp's rfl::NoExtraFields processor, applied when a
  /// section is read into its type; the document itself is always read as
  /// rfl::Generic, which accepts everything, so tables nobody registered
  /// survive.
  ///
  bool reject_unknown_fields = false;
};

///
/// The TOML endpoint, which is what a configuration is written in by default.
/// A stateful instance whose operations are const members working on a whole
/// document, so two documents can share one endpoint value without racing.
///
/// Two limitations follow reflect-cpp's Generic, and are pinned by
/// test/configuration/test_generic_toml.cpp:
///
///   - TOML has no null, and reflect-cpp omits an unset std::optional field
///     rather than spelling a null state. A disengaged field is left out of
///     the document; a field that reads back absent is disengaged again.
///   - Generic's variant has no datetime alternative, so a timestamp field
///     degrades to a plain string on the way in and out.
///
class endpoint_toml final
{
public: // Typedefs / Constants
  ///
  /// The endpoint's name, as a plain label for diagnostics.
  ///
  static constexpr auto name = std::string_view{"toml"};

  ///
  /// The neutral document type: an ordered map of keys to rfl::Generic.
  ///
  using document_t = rfl::Generic::Object;

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
  /// Reads a whole document from a transport and parses it.
  ///
  /// \tparam Tr Transport type. A parameter rather than a member, so a
  ///             medium-integrated endpoint can constrain it further and fail
  ///             at the interface when mispaired.
  /// \param carrier Where the bytes come from.
  /// \return The parsed document.
  /// \throws exception_t With invalid_payload if the bytes are not a TOML document.
  ///
  template<transport_c Tr>
  [[nodiscard]] auto parse(const Tr& carrier) const -> document_t;

  ///
  /// Reads one section of a document into its type.
  /// An empty name is the root: the document's own top-level keys, with every
  /// registered subsection already stripped by the caller.
  ///
  /// The version decision lives here: the section's own subtree declares its
  /// version, independently of every other section. The root section's
  /// version is the document's version — there is no second document-level
  /// version, and cross-section migration is what a root section's upgrade
  /// does.
  ///
  /// \tparam T Configuration type.
  /// \param document The document to read from.
  /// \param section The section name, or empty for the root.
  /// \param policy What to do about a differently versioned section.
  /// \return The configuration, migrated where the policy allows it.
  /// \throws exception_t With invalid_payload or invalid_version.
  ///
  template<configuration_c T>
  [[nodiscard]] auto read_section(const document_t& document, std::string_view section, load_policy policy) const -> T;

public: // Methods
  ///
  /// Writes a whole document through a transport.
  ///
  /// \tparam Tr Transport type, as for parse.
  /// \param carrier Where the bytes go.
  /// \param document The document to write.
  ///
  template<transport_c Tr>
  auto emit(const Tr& carrier, const document_t& document) const -> void;

  ///
  /// Writes one section of a document from its type.
  /// An empty name is the root: the document's own top-level keys. The
  /// version written is the one the type declares, whatever the value's
  /// version member happens to hold, so a file cannot come out claiming to be
  /// a version it is not.
  ///
  /// \tparam T Configuration type.
  /// \param document The document to write into.
  /// \param section The section name, or empty for the root.
  /// \param val Configuration to write. Not retained.
  ///
  template<configuration_c T>
  auto write_section(document_t& document, std::string_view section, const T& val) const -> void;

private: // Variables
  endpoint_toml_options options_;
};

///
/// Renders a configuration as TOML text.
/// The text-level counterpart to endpoint_toml::write_section, for a
/// configuration that has to become a string rather than reach a transport.
/// The version written is the one the type declares, whatever the value's
/// version member happens to hold, so text cannot come out claiming to be a
/// version it is not.
///
/// \tparam T Configuration type.
/// \param val Configuration to render. Not retained.
/// \return The TOML text.
///
template<configuration_c T>
[[nodiscard]] auto to_toml(const T& val) -> std::string;

///
/// Parses TOML text as a configuration of exactly T's version.
///
/// \tparam T Configuration type.
/// \param text TOML text. Not retained.
/// \return The parsed configuration.
/// \throws exception_t With invalid_payload if the text does not parse, or
///         invalid_version if it declares a version other than T's.
///
template<configuration_c T>
[[nodiscard]] auto from_toml(std::string_view text) -> T;

///
/// Parses TOML text as any version To knows, and migrates it to To.
/// Text newer than this build is refused rather than guessed at.
///
/// \tparam To Configuration version to end up at, usually the current one.
/// \param text TOML text. Not retained.
/// \return The configuration, migrated to To.
/// \throws exception_t With invalid_payload if the text does not parse, or
///         invalid_version if it declares a version this build has no type for.
///
template<configuration_c To>
[[nodiscard]] auto from_toml_migrated(std::string_view text) -> To;

///
/// Returns the configuration version TOML text declares.
/// Reads the version key alone, so it answers for text of any version,
/// including one this build has no type for.
///
/// \param text TOML text. Not retained.
/// \return The version the text declares.
/// \throws exception_t With invalid_payload if the text does not parse, or carries no version.
///
[[nodiscard]] auto toml_version(std::string_view text) -> version_t;

///
///
inline endpoint_toml::endpoint_toml(endpoint_toml_options options)
  : options_{options}
{
}

///
///
template<transport_c Tr>
auto endpoint_toml::parse(const Tr& carrier) const -> document_t
{
  return detail::read_toml_document(as_text(carrier.read()), carrier.location());
}

///
///
template<transport_c Tr>
auto endpoint_toml::emit(const Tr& carrier, const document_t& document) const -> void
{
  carrier.write(as_bytes(rfl::toml::write(document)));
}

///
///
template<configuration_c T>
auto endpoint_toml::read_section(
  const document_t& document, const std::string_view section, const load_policy policy) const -> T
{
  return detail::parse_section<T>(document, section, policy, options_.reject_unknown_fields);
}

///
///
template<configuration_c T>
auto endpoint_toml::write_section(document_t& document, const std::string_view section, const T& val) const -> void
{
  detail::write_section_value<T>(document, section, val);
}

///
///
template<configuration_c T>
auto to_toml(const T& val) -> std::string
{
  auto document = rfl::Generic::Object{};
  detail::write_section_value<T>(document, {}, val);

  return rfl::toml::write(document);
}

///
///
template<configuration_c T>
auto from_toml(const std::string_view text) -> T
{
  // load_policy::none, so a version other than T's is refused rather than
  // migrated: from_toml_migrated is what accepts an older one.
  return detail::parse_section<T>(detail::read_toml_document(text, detail::text_source), {}, load_policy::none, false);
}

///
///
template<configuration_c To>
auto from_toml_migrated(const std::string_view text) -> To
{
  return detail::parse_section<To>(
    detail::read_toml_document(text, detail::text_source), {}, load_policy::upgrade_to_current, false);
}

///
///
inline auto toml_version(const std::string_view text) -> version_t
{
  const auto document = detail::read_toml_document(text, detail::text_source);

  return detail::read_generic<detail::endpoint_probe>(rfl::Generic{document}, false).version;
}

} // namespace arude::config
