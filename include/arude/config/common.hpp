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
#include <source_location>
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
/// Thrown by the transports, the reading and writing of a configuration, and
/// the config_file that ties the two to a medium, so a failure keeps the same
/// name whether it came from a socket, a file or the cache, and nothing in
/// between has to translate it.
///
/// Formats as its own name, through arude/enum.hpp, so an exception report
/// names the error rather than calling the payload unformattable.
///
enum class config_error : std::uint8_t
{
  ///
  /// The location is malformed: a transport refused the address it was
  /// constructed with.
  ///
  invalid_location,

  ///
  /// The source holds a version that cannot be brought to the one asked for.
  ///
  invalid_version,

  ///
  /// The source does not parse, or does not hold a configuration of the type
  /// asked for.
  ///
  invalid_payload,

  ///
  /// The source could not be read or written.
  ///
  io_error,

  ///
  /// There is nothing at the location, or a file has nothing loaded or set.
  ///
  not_found,

  ///
  /// The transport the location names can be read but not written.
  ///
  read_only
};

///
/// The exception the configuration subsystem throws: an arude::exception whose
/// payload says which error it was.
///
using exception_t = exception<config_error>;

} // namespace arude::config

namespace arude::config::detail
{

// The configuration support is compiled into the library and throws
// arude::exception, whose base class and layout ARUDE_EXCEPTION_RUNTIME_ERROR_BASE
// decides. A consumer compiled with a different setting from the library would
// catch those exceptions as a type they are not, which no compiler or linker
// notices on its own. So each setting declares a symbol of its own,
// src/config/common.cpp defines the one the library was built with, and every
// translation unit including this header refers to the one it was built with:
// a mismatch fails to link, naming the symbol, rather than failing at run time.
//
// MSVC has no attribute that keeps an unread variable, so there the check is
// detect_mismatch instead, which the linker compares across every object file
// it links and reports by name. The symbol is declared regardless and unused.

#if (defined ARUDE_EXCEPTION_RUNTIME_ERROR_BASE)

///
/// Defined by the library only when it was built with ARUDE_EXCEPTION_RUNTIME_ERROR_BASE.
///
extern const int library_built_with_exception_runtime_error_base;

  #if (defined _MSC_VER)
    #pragma detect_mismatch("arude_exception_runtime_error_base", "defined")
  #else

///
/// Refers to the library's definition, so a library built otherwise fails to link.
/// Emitted although nothing reads it, which is what makes the reference.
///
[[maybe_unused, gnu::used, gnu::retain]] inline constexpr const int* exception_base_check =
  &library_built_with_exception_runtime_error_base;

  #endif // #if (defined _MSC_VER)

#else

///
/// Defined by the library only when it was built without ARUDE_EXCEPTION_RUNTIME_ERROR_BASE.
///
extern const int library_built_without_exception_runtime_error_base;

  #if (defined _MSC_VER)
    #pragma detect_mismatch("arude_exception_runtime_error_base", "undefined")
  #else

///
/// Refers to the library's definition, so a library built otherwise fails to link.
/// Emitted although nothing reads it, which is what makes the reference.
///
[[maybe_unused, gnu::used, gnu::retain]] inline constexpr const int* exception_base_check =
  &library_built_without_exception_runtime_error_base;

  #endif // #if (defined _MSC_VER)

#endif // #if (defined ARUDE_EXCEPTION_RUNTIME_ERROR_BASE)

} // namespace arude::config::detail

namespace arude::config
{

///
/// Interprets bytes as text.
/// std::byte and char are both narrow character types and may alias anything,
/// so reading one as the other is legal: the bytes are the text, unchanged.
///
/// \param bytes Bytes to interpret. Not retained.
/// \return The text.
///
[[nodiscard]] auto as_text(std::span<const std::byte> bytes) -> std::string;

///
/// Interprets text as bytes.
/// \see as_text for why reading one as the other is legal.
///
/// \param text Text to interpret. Not retained.
/// \return The bytes.
///
[[nodiscard]] auto as_bytes(std::string_view text) -> std::vector<std::byte>;

} // namespace arude::config

namespace arude::config::detail
{

///
/// The prefix every message of the configuration support starts with.
///
inline constexpr auto message_prefix = std::string_view{"arude::config: "};

///
/// Returns text with message_prefix removed wherever a message begins.
/// The prefix is removed at the start, and after the ": " with which a
/// message quoting another one, ours or reflect-cpp's, introduces it, so a
/// report reads as one sentence with one prefix rather than repeating it at
/// each level. The same text inside a path, a key or a value is left as it
/// was written.
///
/// \param message The text to strip. Not retained.
/// \return The text without message_prefix.
///
[[nodiscard]] auto without_prefix(std::string_view message) -> std::string;

///
/// Returns the message of the exception being handled, for quoting inside
/// another message: without_prefix() of arude::exception_message().
/// A message quoted this way usually comes from the configuration support
/// itself, sometimes by way of reflect-cpp, which puts its own text around it.
///
/// Call it only from inside a catch block, as arude::exception_message().
///
/// \return The message, as arude::exception_message() describes it, without message_prefix.
///
[[nodiscard]] auto nested_message() -> std::string;

///
/// Rethrows the exception being handled as an exception_t, or throws one
/// reporting the failure through a fallback code.
/// An exception_t is rethrown unchanged, code and call site included.
/// Anything else is turned into an exception_t naming the failure that was
/// expected and quoting the message of what was thrown in its place.
///
/// Call it only from inside a catch block.
///
/// \param fallback The code the failure reports, usually io_error.
/// \param what The failure being attempted, such as "'file' could not be read".
/// \param loc The call site a failure reports.
/// \throws exception_t The one being handled, or the one described above.
///
[[noreturn]] auto rethrow_as(config_error fallback, std::string_view what, std::source_location loc) -> void;

} // namespace arude::config::detail
