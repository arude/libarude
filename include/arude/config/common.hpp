///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// Small helpers that do not belong to any one subsystem and that several of
/// them need, and the error type every part of the subsystem reports.
///
/// config_error lives here rather than in a header of its own because the
/// transports throw it directly. A transport is the lowest layer there is, so
/// anything it had to include would be included by everything above it; this
/// header it includes already.
///
#pragma once

#include "arude/enum.hpp"
#include "arude/exception.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace arude::config
{

///
/// What went wrong, carried as the payload of every exception thrown here.
/// Distinguishing these is the point: a caller can create a configuration on
/// not_found, report io_error to the user, and refuse to start on
/// invalid_payload, which one message string would not let it do.
///
/// Thrown by the transports as well as by the manager, so a failure keeps the
/// same name whether it came from a socket, a file or the cache, and nothing in
/// between has to translate it.
///
/// Formats as its own name, through arude/enum.hpp, so an exception report
/// names the error rather than calling the payload unformattable.
///
enum class config_error : std::uint8_t
{
  ///
  /// The location is malformed, or names a transport this manager cannot reach.
  ///
  invalid_location,

  ///
  /// A document is already registered at that location.
  /// Told apart from invalid_location because there is nothing wrong with the
  /// location itself: it is the second registration that is the mistake, and a
  /// caller wiring documents up at start-up can say so rather than reporting
  /// that a path it just built is malformed.
  ///
  already_registered_location,

  ///
  /// The source holds a version that cannot be brought to the one asked for.
  ///
  invalid_version,

  ///
  /// The source is not a configuration of that type, or the cache holds another type under the location.
  ///
  invalid_payload,

  ///
  /// The source could not be read or written.
  ///
  io_error,

  ///
  /// There is nothing at the location, or nothing cached for it.
  ///
  not_found,

  ///
  /// The transport the location names can be read but not written.
  ///
  read_only,

  ///
  /// get<T>() or find<T>() was asked for a type registered in more than one place.
  ///
  ambiguous_type
};

///
/// The exception the configuration subsystem throws: an arude::exception whose
/// payload says which error it was.
///
using exception_t = exception<config_error>;

///
/// Interprets bytes as text.
/// std::byte and char are both narrow character types and may alias anything,
/// so the reinterpret_cast below is legal: the bytes are the text, unchanged.
///
/// \param bytes Bytes to interpret. Not retained.
/// \return The text.
///
[[nodiscard]] auto as_text(std::span<const std::byte> bytes) -> std::string;

///
/// Interprets text as bytes.
/// \see as_text for why the reinterpret_cast is legal.
///
/// \param text Text to interpret. Not retained.
/// \return The bytes.
///
[[nodiscard]] auto as_bytes(std::string_view text) -> std::vector<std::byte>;

///
///
inline auto as_text(const std::span<const std::byte> bytes) -> std::string
{
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  const auto* const begin = reinterpret_cast<const char*>(bytes.data());
  return {begin, bytes.size()};
}

///
///
inline auto as_bytes(const std::string_view text) -> std::vector<std::byte>
{
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  const auto* const begin = reinterpret_cast<const std::byte*>(text.data());
  return {begin, begin + text.size()};
}

} // namespace arude::config
