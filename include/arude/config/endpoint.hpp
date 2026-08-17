///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// Endpoints: the formats a configuration can be written in.
///
/// An endpoint is a stateful, format-only instance — bytes in, a document out,
/// and back again. Where the bytes are kept is the transport's business;
/// splitting the two is what lets the same TOML endpoint serve a file today
/// and something else later without being touched.
///
/// The neutral document type is rfl::Generic::Object: format-independent
/// within reflect-cpp, so a later JSON or YAML endpoint uses the same one and
/// nothing downstream has to learn a second document type. Sections are typed
/// views into it, read and written through rfl::from_generic / rfl::to_generic.
///
#pragma once

#include "arude/config/load_policy.hpp"
#include "arude/config/migration.hpp"
#include "arude/exception.hpp"
#include "arude/exception_report.hpp"
#include "arude/type_name.hpp"

#include <rfl/from_generic.hpp>
#include <rfl/Generic.hpp>
#include <rfl/NoExtraFields.hpp>
#include <rfl/to_generic.hpp>

#include <cstddef>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace arude::config::detail
{

///
/// The smallest type that is still a configuration.
/// Exists so that the endpoint concept can state what an endpoint does with a
/// configuration, rather than describing it in a comment and finding out at
/// the point of use.
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
/// The smallest type that is still a transport.
/// Exists for the same reason as endpoint_probe: the endpoint concept states
/// its contract against a probe rather than a real transport, so that contract
/// does not drag a filesystem or a network stack into every endpoint header.
///
struct transport_probe
{
  ///
  /// \see transport_c
  ///
  static constexpr auto name = std::string_view{"probe"};

  // The probe exists to state the contract, and the contract is instance
  // operations: making these static would let a type of statics pass and
  // still probe nothing about an instance.
  // NOLINTBEGIN(readability-convert-member-functions-to-static)
  ///
  /// \see transport_c
  ///
  [[nodiscard]] auto location() const -> std::string;

  ///
  /// \see transport_c
  ///
  [[nodiscard]] auto writable() const -> bool;

  ///
  /// \see transport_c
  ///
  [[nodiscard]] auto exists() const -> bool;

  ///
  /// \see transport_c
  ///
  [[nodiscard]] auto read() const -> std::vector<std::byte>;

  ///
  /// \see transport_c
  ///
  auto write(std::span<const std::byte> bytes) const -> void;
  // NOLINTEND(readability-convert-member-functions-to-static)
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
/// Reads a configuration out of a Generic value.
/// Throws a plain arude::exception on failure; the caller is what wraps it
/// with the source and the error code.
///
/// \tparam T Configuration type.
/// \param generic Value to read from.
/// \param reject_unknown_fields Whether a field the type does not declare is an error.
/// \return The configuration.
/// \throws arude::exception If the value does not hold what T needs.
///
template<configuration_c T>
[[nodiscard]] auto read_generic(const rfl::Generic& generic, bool reject_unknown_fields) -> T;

///
/// Writes a configuration into a document as a section.
/// The version written is the one the type declares, whatever the value's
/// version member happens to hold. An empty name is the root: the document's
/// own top-level keys; otherwise the value goes into the nested table under
/// that name.
///
/// \tparam T Configuration type.
/// \param document The document to write into.
/// \param section The section name, or empty for the root.
/// \param val Configuration to write. Not retained.
///
template<configuration_c T>
auto write_section_value(rfl::Generic::Object& document, std::string_view section, const T& val) -> void;

///
/// Extracts the section's subtree from the document.
/// An empty name is the root: the document's own top-level keys, with every
/// registered subsection already stripped by the caller.
///
/// \param document The document to read from.
/// \param section The section name, or empty for the root.
/// \return The subtree.
/// \throws arude::exception If a named section is not there.
///
[[nodiscard]] auto acquire_subtree(const rfl::Generic::Object& document, std::string_view section) -> rfl::Generic;

///
/// The version decision for one section, moved from config_manager::parse.
/// Probes the version in the section's own subtree, returns the exact match,
/// rejects on strict_version or an absent upgrade_to_current, and migrates
/// otherwise.
///
/// \tparam T Configuration type.
/// \param document The document to read from.
/// \param section The section name, or empty for the root.
/// \param policy What to do about a missing or differently versioned section.
/// \param reject_unknown_fields Whether a field the type does not declare is an error.
/// \return The configuration, migrated where the policy allows it.
/// \throws exception_t With invalid_payload or invalid_version.
///
template<configuration_c T>
[[nodiscard]] auto parse_section(
  const rfl::Generic::Object& document, std::string_view section, load_policy policy, bool reject_unknown_fields) -> T;

///
/// Parses a subtree as whichever version of To's chain matches its version
/// number. Walks backwards through previous_t, which reaches every version
/// that existed when To was written and no further: a subtree newer than this
/// build is a failure, not something to guess at.
///
/// \tparam To Version to end up at.
/// \tparam Candidate Version being considered.
/// \param subtree The section's subtree.
/// \param version The version the subtree declares.
/// \param reject_unknown_fields Whether a field the type does not declare is an error.
/// \return The configuration, parsed as the matching version and migrated to To.
/// \throws arude::exception If no version in the chain matches.
///
template<configuration_c To, configuration_c Candidate>
[[nodiscard]] auto parse_section_chain(const rfl::Generic& subtree, version_t version, bool reject_unknown_fields)
  -> To;

///
///
// The probe states the contract, and the contract is instance operations: a
// static member would pass the concept without probing an instance.
// NOLINTBEGIN(readability-convert-member-functions-to-static)
inline auto transport_probe::location() const -> std::string
{
  return "probe:";
}

///
///
inline auto transport_probe::writable() const -> bool
{
  return true;
}

///
///
inline auto transport_probe::exists() const -> bool
{
  return true;
}

///
///
inline auto transport_probe::read() const -> std::vector<std::byte>
{
  return {};
}

///
///
inline auto transport_probe::write(const std::span<const std::byte> bytes) const -> void
{
  std::ignore = bytes;
}

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
// NOLINTEND(readability-convert-member-functions-to-static)

///
///
template<configuration_c T>
auto read_generic(const rfl::Generic& generic, const bool reject_unknown_fields) -> T
{
  auto result =
    reject_unknown_fields ? rfl::from_generic<T, rfl::NoExtraFields>(generic) : rfl::from_generic<T>(generic);

  if(!result.has_value())
  {
    throw exception{
      std::format("arude::config: it does not hold a {}: {}", type_name<T>(), error_text(result.error()))};
  }

  return std::move(result).value();
}

///
///
inline auto acquire_subtree(const rfl::Generic::Object& document, const std::string_view section) -> rfl::Generic
{
  if(section.empty())
  {
    return rfl::Generic{document};
  }

  const auto found = document.get(std::string{section});

  if(!found.has_value())
  {
    throw exception{std::format("arude::config: there is no section named '{}'.", section)};
  }

  return found.value();
}

///
///
template<configuration_c T>
auto parse_section(
  const rfl::Generic::Object& document,
  const std::string_view section,
  const load_policy policy,
  const bool reject_unknown_fields) -> T
{
  const auto source = section.empty() ? std::string_view{"the document"} : section;

  auto version = version_t{0};
  auto subtree = rfl::Generic{};

  try
  {
    subtree = acquire_subtree(document, section);
    version = read_generic<endpoint_probe>(subtree, false).version;
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
      return read_generic<T>(subtree, reject_unknown_fields);
    }
    catch(...)
    {
      throw exception_t{
        std::format("arude::config: {} is not a {}: {}", source, type_name<T>(), exception_report()),
        config_error::invalid_payload};
    }
  }

  if(is_set(policy, load_policy::strict_version) || !is_set(policy, load_policy::upgrade_to_current))
  {
    throw exception_t{
      std::format("arude::config: {} is version {}, and version {} was asked for.", source, version, version_of<T>()),
      config_error::invalid_version};
  }

  try
  {
    return parse_section_chain<T, T>(subtree, version, reject_unknown_fields);
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
auto parse_section_chain(const rfl::Generic& subtree, const version_t version, const bool reject_unknown_fields) -> To
{
  if(version == version_of<Candidate>())
  {
    return migrate<To>(read_generic<Candidate>(subtree, reject_unknown_fields));
  }

  if constexpr(has_previous_configuration_c<Candidate>)
  {
    return parse_section_chain<To, typename Candidate::previous_t>(subtree, version, reject_unknown_fields);
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
auto write_section_value(rfl::Generic::Object& document, const std::string_view section, const T& val) -> void
{
  auto stamped = val;
  stamped.version = version_of<T>();

  const auto generic = rfl::to_generic(stamped);
  const auto fields = generic.to_object().value();

  if(section.empty())
  {
    for(const auto& [key, value] : fields)
    {
      document[key] = value;
    }

    return;
  }

  auto& entry = document[std::string{section}];

  auto table = entry.to_object();

  if(!table.has_value())
  {
    table = rfl::Generic::Object{};
  }

  for(const auto& [key, value] : fields)
  {
    table.value()[key] = value;
  }

  entry = rfl::Generic{std::move(table).value()};
}

} // namespace arude::config::detail

namespace arude::config
{

///
/// A configuration format that whole documents can be read from and written
/// to. Stated as a concept rather than a base class: an endpoint has nothing
/// to inherit, and the requirements below are the whole contract. The
/// operations are const members on an instance, which is what lets two
/// documents share one format value; an endpoint that mutated itself while
/// reading would race.
///
/// \tparam E Type to test.
///
template<typename E>
concept endpoint_c = std::move_constructible<E> && requires(
                                                     const E& endpoint,
                                                     const detail::transport_probe& carrier,
                                                     typename E::document_t& document,
                                                     const typename E::document_t& const_document,
                                                     const detail::endpoint_probe& val,
                                                     load_policy policy) {
  { E::name } -> std::convertible_to<std::string_view>;
  { endpoint.parse(carrier) } -> std::same_as<typename E::document_t>;
  { endpoint.emit(carrier, const_document) } -> std::same_as<void>;
  {
    endpoint.template read_section<detail::endpoint_probe>(const_document, std::string_view{}, policy)
  } -> std::same_as<detail::endpoint_probe>;
  { endpoint.template write_section<detail::endpoint_probe>(document, std::string_view{}, val) } -> std::same_as<void>;
};

} // namespace arude::config
