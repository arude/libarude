///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// Reading and writing configuration files as TOML.
///
/// Nothing here is constexpr, and nothing here can be: a file is read at run
/// time. The parts that can be evaluated at compile time — the version
/// arithmetic, the migration steps, the base64 codec — live in the headers
/// this one includes, so a migration is only pushed to run time by the file
/// access itself.
///
#pragma once

#include "arude/config/migration.hpp"
#include "arude/exception.hpp"
#include "arude/type_name.hpp"

#include <rfl/toml.hpp>

#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>

namespace arude::config::detail
{

///
/// What an error message calls text that did not come from a file.
///
inline constexpr auto text_source = std::string_view{"<text>"};

///
/// Reads a whole file into a string.
/// Opened in binary mode, so a file written on one platform and read on
/// another yields the same text rather than one with its line endings
/// rewritten; TOML does not care either way, but a base64 payload would.
///
/// \param path File to read.
/// \return The contents of the file.
/// \throws arude::exception If the file cannot be opened or cannot be read.
///
[[nodiscard]] auto read_file(const std::filesystem::path& path) -> std::string;

///
/// Writes a string to a file, replacing whatever was there.
///
/// \param path File to write.
/// \param text Text to write. Not retained.
/// \throws arude::exception If the file cannot be opened or cannot be written.
///
auto write_file(const std::filesystem::path& path, std::string_view text) -> void;

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
/// Parses TOML text into a value.
///
/// \tparam T Type to parse into.
/// \param text TOML text. Not retained.
/// \param source What the text came from, used in the error message only.
/// \return The parsed value.
/// \throws arude::exception If the text is not valid TOML, or does not hold what T needs.
///
template<typename T>
[[nodiscard]] auto parse_toml(const std::string& text, std::string_view source) -> T;

///
/// The version key alone, read out of a configuration file.
/// reflect-cpp ignores keys it was not asked for, so this parses any
/// configuration file whatever else it holds.
///
struct version_probe
{
  ///
  /// The version the file declares.
  ///
  version_t version = 0;
};

///
/// Parses text as whichever version of To's chain matches a version number.
/// Walks backwards from Candidate through previous_t, which reaches every
/// version that existed when To was written and no further: a file newer than
/// this build is a failure, not something to guess at.
///
/// \tparam To Version to end up at.
/// \tparam Candidate Version being considered.
/// \param text TOML text. Not retained.
/// \param version Version the text declares.
/// \param source What the text came from, used in error messages only.
/// \return The configuration, parsed as the matching version and migrated to To.
/// \throws arude::exception If no version in the chain matches, or the text does not parse.
///
template<configuration_c To, configuration_c Candidate>
[[nodiscard]] auto parse_version_chain(const std::string& text, version_t version, std::string_view source) -> To;

///
///
inline auto read_file(const std::filesystem::path& path) -> std::string
{
  auto stream = std::ifstream{path, std::ios::binary};

  if(!stream.is_open())
  {
    throw exception{std::format("arude::config: cannot open '{}' for reading.", path.string())};
  }

  auto text = std::string{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};

  if(stream.bad())
  {
    throw exception{std::format("arude::config: failed while reading '{}'.", path.string())};
  }

  return text;
}

///
///
inline auto write_file(const std::filesystem::path& path, const std::string_view text) -> void
{
  auto stream = std::ofstream{path, std::ios::binary | std::ios::trunc};

  if(!stream.is_open())
  {
    throw exception{std::format("arude::config: cannot open '{}' for writing.", path.string())};
  }

  stream.write(text.data(), static_cast<std::streamsize>(text.size()));
  stream.close();

  if(stream.fail())
  {
    throw exception{std::format("arude::config: failed while writing '{}'.", path.string())};
  }
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

///
///
template<typename T>
auto parse_toml(const std::string& text, const std::string_view source) -> T
{
  auto result = rfl::toml::read<T>(text);

  if(!result.has_value())
  {
    throw exception{
      std::format("arude::config: '{}' does not hold a {}: {}", source, type_name<T>(), error_text(result.error()))};
  }

  return std::move(result).value();
}

///
///
// One call per version of the chain, which is finite and known at compile
// time; the else branch below is where it stops.
// NOLINTNEXTLINE(misc-no-recursion)
template<configuration_c To, configuration_c Candidate>
auto parse_version_chain(const std::string& text, const version_t version, const std::string_view source) -> To
{
  if(version == version_of<Candidate>())
  {
    return migrate<To>(parse_toml<Candidate>(text, source));
  }

  if constexpr(has_previous_configuration_c<Candidate>)
  {
    return parse_version_chain<To, typename Candidate::previous_t>(text, version, source);
  }
  else
  {
    throw exception{std::format(
      "arude::config: '{}' declares configuration version {}, which is not a version of {} that this build knows.",
      source,
      version,
      type_name<To>())};
  }
}

} // namespace arude::config::detail

namespace arude::config
{

///
/// Renders a configuration as TOML text.
/// The version written is the one the type declares, whatever the value's
/// version member happens to hold, so a file cannot come out claiming to be a
/// version it is not.
///
/// \tparam T Configuration type.
/// \param val Configuration to render. Not retained.
/// \return The TOML text.
///
template<configuration_c T>
[[nodiscard]] auto to_toml(const T& val) -> std::string;

///
/// Parses TOML text as a configuration of a known version.
///
/// \tparam T Configuration type.
/// \param text TOML text. Not retained.
/// \return The parsed configuration.
/// \throws arude::exception If the text does not parse, or declares a version other than T's.
///
template<configuration_c T>
[[nodiscard]] auto from_toml(std::string_view text) -> T;

///
/// Parses TOML text as any version To knows, and migrates it to To.
/// The text equivalent of load_migrated(), for a configuration that arrived as
/// something other than a file.
///
/// \tparam To Configuration version to end up at, usually the current one.
/// \param text TOML text. Not retained.
/// \return The configuration, migrated to To.
/// \throws arude::exception If the text does not parse, or declares an unknown version.
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
/// \throws arude::exception If the text does not parse, or carries no version.
///
[[nodiscard]] auto toml_version(std::string_view text) -> version_t;

///
/// Reads a configuration file of a known version.
/// The file's version has to be T's; use load_migrated() where an older file
/// should be accepted and brought forward.
///
/// \tparam T Configuration type.
/// \param path File to read.
/// \return The configuration held in the file.
/// \throws arude::exception If the file cannot be read, does not parse, or declares another version.
///
template<configuration_c T>
[[nodiscard]] auto load(const std::filesystem::path& path) -> T;

///
/// Reads a configuration file of any version To knows, and migrates it to To.
/// This is what an application wants at start-up: it accepts every file its
/// own version ever wrote, and hands back one type. A file written by a newer
/// build is refused rather than guessed at.
///
/// \tparam To Configuration version to end up at, usually the current one.
/// \param path File to read.
/// \return The configuration, migrated to To.
/// \throws arude::exception If the file cannot be read, does not parse, or declares an unknown version.
///
template<configuration_c To>
[[nodiscard]] auto load_migrated(const std::filesystem::path& path) -> To;

///
/// Writes a configuration to a file, replacing whatever was there.
/// Not atomic: a write interrupted part way leaves a truncated file. Write to
/// a temporary and rename it where that matters.
///
/// \tparam T Configuration type.
/// \param path File to write.
/// \param val Configuration to write. Not retained.
/// \throws arude::exception If the file cannot be written.
///
template<configuration_c T>
auto store(const std::filesystem::path& path, const T& val) -> void;

///
///
template<configuration_c T>
auto to_toml(const T& val) -> std::string
{
  auto stamped = val;
  stamped.version = version_of<T>();

  return rfl::toml::write(stamped);
}

///
///
template<configuration_c T>
auto from_toml(const std::string_view text) -> T
{
  const auto val = detail::parse_toml<T>(std::string{text}, detail::text_source);

  if(val.version != version_of<T>())
  {
    throw exception{std::format(
      "arude::config: text declares configuration version {}, but {} is version {}.",
      val.version,
      type_name<T>(),
      version_of<T>())};
  }

  return val;
}

///
///
template<configuration_c To>
auto from_toml_migrated(const std::string_view text) -> To
{
  const auto owned = std::string{text};
  const auto probe = detail::parse_toml<detail::version_probe>(owned, detail::text_source);

  return detail::parse_version_chain<To, To>(owned, probe.version, detail::text_source);
}

///
///
inline auto toml_version(const std::string_view text) -> version_t
{
  return detail::parse_toml<detail::version_probe>(std::string{text}, detail::text_source).version;
}

///
///
template<configuration_c T>
auto load(const std::filesystem::path& path) -> T
{
  const auto source = path.string();
  const auto val = detail::parse_toml<T>(detail::read_file(path), source);

  if(val.version != version_of<T>())
  {
    throw exception{std::format(
      "arude::config: '{}' declares configuration version {}, but {} is version {}.",
      source,
      val.version,
      type_name<T>(),
      version_of<T>())};
  }

  return val;
}

///
///
template<configuration_c To>
auto load_migrated(const std::filesystem::path& path) -> To
{
  const auto source = path.string();
  const auto text = detail::read_file(path);
  const auto probe = detail::parse_toml<detail::version_probe>(text, source);

  return detail::parse_version_chain<To, To>(text, probe.version, source);
}

///
///
template<configuration_c T>
auto store(const std::filesystem::path& path, const T& val) -> void
{
  detail::write_file(path, to_toml(val));
}

} // namespace arude::config
