///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// Endpoints: the formats a configuration can be written in, and the URIs that
/// name one.
///
/// An endpoint is a format and nothing else — text in, configuration out, and
/// back again. Where the text is kept is the transport's business, and
/// config_manager's; splitting the two is what lets the same TOML endpoint
/// serve a file today and something else later without being touched.
///
/// Both halves are named in the URI scheme, as format+transport:
///
///     toml+file:///etc/example/app.toml
///
/// which is a well-formed scheme under RFC 3986 — '+' is allowed after the
/// first character. The endpoint contributes the part before the '+', so an
/// endpoint's name has to be a scheme token in its own right; the concept
/// below refuses one that is not, at compile time.
///
#pragma once

#include "arude/config/config_file.hpp"
#include "arude/config/migration.hpp"

#include <boost/url.hpp>

#include <algorithm>
#include <concepts>
#include <filesystem>
#include <string>
#include <string_view>

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
  /// \see config_test_v1::config_version
  ///
  static constexpr auto config_version = version_t{1};

  ///
  /// \see config_test_v1::version
  ///
  version_t version = config_version;
};

///
/// Reports whether a character may appear in a URI scheme after the first.
/// RFC 3986 allows a digit, a hyphen, a plus and a dot there as well as a
/// letter; an endpoint name is held to the stricter set that excludes the plus,
/// because the plus is what separates the endpoint from the transport.
///
/// \param character Character to test.
/// \return true if the character may appear in an endpoint name.
///
[[nodiscard]] constexpr auto is_scheme_character(char character) -> bool;

///
/// Reports whether text is usable as the endpoint half of a URI scheme.
/// A scheme starts with a letter, so a name that starts with a digit is not
/// one however the rest of it reads.
///
/// \param name Name to test.
/// \return true if the name is a scheme token.
///
[[nodiscard]] constexpr auto is_scheme_token(std::string_view name) -> bool;

///
///
constexpr auto is_scheme_character(const char character) -> bool
{
  return (character >= 'a' && character <= 'z') ||
         (character >= '0' && character <= '9') ||
         character == '-' ||
         character == '.';
}

///
///
constexpr auto is_scheme_token(const std::string_view name) -> bool
{
  if(name.empty() || name.front() < 'a' || name.front() > 'z')
  {
    return false;
  }

  return std::ranges::all_of(name, is_scheme_character);
}

} // namespace arude::config::detail

namespace arude::config
{

///
/// The transport half of a configuration URI scheme.
/// Files are the only transport there is so far, so this is the only value the
/// half after the '+' takes.
///
inline constexpr auto uri_transport_file = std::string_view{"file"};

///
/// A configuration format that text can be read from and written to.
/// Stated as a concept rather than a base class: an endpoint has no state and
/// nothing to inherit, and the requirements below are the whole contract. They
/// are checked against detail::endpoint_probe, so an endpoint has to be
/// generic over configurations rather than support one of them.
///
/// \tparam E Type to test.
///
template<typename E>
concept endpoint_c = requires(std::string_view text, const detail::endpoint_probe& val) {
  { E::name } -> std::convertible_to<std::string_view>;
  requires detail::is_scheme_token(E::name);
  { E::template read<detail::endpoint_probe>(text) } -> std::same_as<detail::endpoint_probe>;
  { E::template read_migrated<detail::endpoint_probe>(text) } -> std::same_as<detail::endpoint_probe>;
  { E::write(val) } -> std::same_as<std::string>;
  { E::probe_version(text) } -> std::same_as<version_t>;
};

///
/// The TOML endpoint, which is what a configuration is written in by default.
/// A thin front to arude/config/config_file.hpp: everything here is one call,
/// and the format's own rules live there.
///
struct endpoint_toml
{
  ///
  /// The endpoint half of the URI scheme, so "toml+file" for a file.
  ///
  static constexpr auto name = std::string_view{"toml"};

  ///
  /// Parses text as exactly T's version.
  ///
  /// \tparam T Configuration type.
  /// \param text TOML text. Not retained.
  /// \return The configuration.
  /// \throws arude::exception If the text does not parse, or declares another version.
  ///
  template<configuration_c T>
  [[nodiscard]] static auto read(std::string_view text) -> T;

  ///
  /// Parses text as any version of T's chain, migrating it forward to T.
  ///
  /// \tparam T Configuration type.
  /// \param text TOML text. Not retained.
  /// \return The configuration, migrated to T.
  /// \throws arude::exception If the text does not parse, or declares an unknown version.
  ///
  template<configuration_c T>
  [[nodiscard]] static auto read_migrated(std::string_view text) -> T;

  ///
  /// Renders a configuration as text.
  ///
  /// \tparam T Configuration type.
  /// \param val Configuration to render. Not retained.
  /// \return The TOML text.
  ///
  template<configuration_c T>
  [[nodiscard]] static auto write(const T& val) -> std::string;

  ///
  /// Returns the version text declares, without parsing the rest of it.
  ///
  /// \param text TOML text. Not retained.
  /// \return The version the text declares.
  /// \throws arude::exception If the text does not parse, or carries no version.
  ///
  [[nodiscard]] static auto probe_version(std::string_view text) -> version_t;
};

///
/// Returns the URI scheme naming an endpoint over a file, so "toml+file".
///
/// \tparam E Endpoint type.
/// \return The scheme.
///
template<endpoint_c E>
[[nodiscard]] constexpr auto uri_scheme() -> std::string;

///
/// Builds the URI naming a configuration file.
/// The path is made absolute and normalised first, so two spellings of one
/// file produce one URI — which matters, because config_manager caches on the
/// URI and would otherwise hold the same file twice.
///
/// \tparam E Endpoint type, which decides the format and the scheme.
/// \param path File the configuration lives in, absolute or relative to the working directory.
/// \return The URI.
/// \throws std::filesystem::filesystem_error If a relative path cannot be resolved.
///
template<endpoint_c E = endpoint_toml>
[[nodiscard]] auto create_uri(const std::filesystem::path& path) -> boost::urls::url;

///
///
template<configuration_c T>
auto endpoint_toml::read(const std::string_view text) -> T
{
  return from_toml<T>(text);
}

///
///
template<configuration_c T>
auto endpoint_toml::read_migrated(const std::string_view text) -> T
{
  return from_toml_migrated<T>(text);
}

///
///
template<configuration_c T>
auto endpoint_toml::write(const T& val) -> std::string
{
  return to_toml(val);
}

///
///
inline auto endpoint_toml::probe_version(const std::string_view text) -> version_t
{
  return toml_version(text);
}

///
///
template<endpoint_c E>
constexpr auto uri_scheme() -> std::string
{
  return std::string{E::name} + '+' + std::string{uri_transport_file};
}

///
///
template<endpoint_c E>
auto create_uri(const std::filesystem::path& path) -> boost::urls::url
{
  auto uri = boost::urls::url{};

  uri.set_scheme(uri_scheme<E>());

  // An empty authority, so the result reads file:///path rather than file:/path
  // and matches what every other tool writes. config_manager ignores the
  // authority either way and takes the path alone.
  uri.set_encoded_authority("");
  uri.set_path(std::filesystem::absolute(path).lexically_normal().generic_string());

  return uri;
}

} // namespace arude::config
