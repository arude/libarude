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
#include <flat_set>
#include <functional>
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
/// Thrown by the transports and the endpoints as well as by the documents and
/// the manager, so a failure keeps the same name whether it came from a
/// socket, a file or the cache, and nothing in between has to translate it.
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
  /// A document is already registered at that location.
  /// Told apart from invalid_location because there is nothing wrong with the
  /// location itself: it is the second registration that is the mistake, and a
  /// caller wiring documents up at start-up can say so rather than reporting
  /// that a path it just built is malformed.
  ///
  already_registered_location,

  ///
  /// A section cannot be added to a document, as its name is taken there.
  /// Either a section of that name is attached already, which for an empty
  /// name means a second root, or the name and the root section's own keys
  /// collide at the top level of the document. Told apart from
  /// already_registered_location because the document and its location are
  /// fine: what is taken is a name within that one document, so the call to
  /// fix is the add_section() or add_root(), not the register_document().
  ///
  already_registered_section,

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
  /// There is nothing at the location, a section is not in the source or has
  /// nothing cached, or a type is registered nowhere.
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
/// A set of keys of one table of a document.
/// The comparison is transparent, so a key arriving as a std::string_view is
/// looked up without being copied into a string first.
///
using key_set_t = std::flat_set<std::string, std::less<>>;

} // namespace arude::config::detail
