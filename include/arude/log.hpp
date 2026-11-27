///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// Logging, as a set of macros over arude::log.
///
/// Macros here rather than functions, for the one thing a function cannot do:
/// leave the arguments unevaluated when the record would be discarded anyway.
/// A filtered-out ARUDE_LOGD costs a level comparison and never formats, never
/// allocates, and never calls whatever the caller wrote in the argument list.
/// That is the whole reason the conventions' "macros are a last resort" is
/// spent here.
///
/// Macros do not cross a module boundary. A module consumer gets arude::log,
/// arude::log_level, and arude::log_global_log_level from `import arude`, but
/// has to include this header for the macros themselves.
///
#pragma once

#include "arude/function_signature.hpp"

#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace arude
{

///
/// Severity of a log record, ordered from least to most severe.
/// The order is what makes the threshold in ARUDE_LOG_DETAIL a comparison, so
/// enumerators must stay in ascending severity.
///
/// off is a threshold rather than a severity: it is above critical, so setting
/// it as the global level discards everything. Logging a record at off is
/// meaningless and nothing stops you doing it.
///
enum class log_level : std::uint8_t
{
  debug,
  info,
  warn,
  error,
  critical,
  off
};

///
/// Writes one record to the log.
/// Placeholder: it accepts a record and drops it. The sinks, the channel
/// routing, and the spdlog backing come later.
///
/// \param level Severity of the record.
/// \param channel Subsystem the record came from. Not retained beyond the call.
/// \param message Text of the record, already formatted.
///
auto log(log_level level, std::string_view channel, std::string message) -> void;

///
/// The level below which records are discarded.
/// Placeholder: it is a fixed level with no way to set it yet.
///
/// \return The lowest severity that is currently logged.
///
[[nodiscard]] auto log_global_log_level() -> log_level;

} // namespace arude

///
/// The guts of a std::format-based log record. Not part of the interface.
/// Use ARUDE_LOGD and friends instead; this is what they expand to.
///
/// The local names are prefixed because a macro has no scope to hide them in:
/// plain `level` would be captured by an argument list that mentions a variable
/// of that name, and silently log the macro's own instead of the caller's.
///
/// do/while(false) rather than a bare block, so a use of the macro is one
/// statement and takes a semicolon like any other. A bare block would make
/// `if(x) ARUDE_LOGI(...); else ...` a syntax error.
///
/// \param CHANNEL Subsystem the record came from.
/// \param LEVEL Unqualified arude::log_level enumerator, as in `info`.
/// \param FMT_STR Format string, or the whole message where no arguments follow.
/// \param ... Format arguments, if any.
///
#define ARUDE_LOG_DETAIL(CHANNEL, LEVEL, FMT_STR, ...)                                                                 \
  do                                                                                                                   \
  {                                                                                                                    \
    constexpr auto arude_log_severity = ::arude::log_level::LEVEL;                                                     \
    if(arude_log_severity >= ::arude::log_global_log_level())                                                          \
    {                                                                                                                  \
      auto arude_log_message = std::string{};                                                                          \
      try                                                                                                              \
      {                                                                                                                \
        /* Empty __VA_ARGS__ stringizes to "", so this asks whether any        */                                      \
        /* argument was passed. Both arms have to compile either way: a        */                                      \
        /* discarded if constexpr branch outside a template is still checked,  */                                      \
        /* which is what __VA_OPT__ is doing in the second one.                */                                      \
        if constexpr(sizeof(#__VA_ARGS__) <= 1)                                                                        \
        {                                                                                                              \
          arude_log_message = FMT_STR;                                                                                 \
        }                                                                                                              \
        else                                                                                                           \
        {                                                                                                              \
          __VA_OPT__(arude_log_message = std::format(FMT_STR, __VA_ARGS__));                                           \
        }                                                                                                              \
      }                                                                                                                \
      /* std::format checks the format string at compile time, so this catches */                                      \
      /* only what survives to run time: a dynamic width or precision that     */                                      \
      /* turns out to be invalid, or a formatter of the caller's that throws.  */                                      \
      catch(const std::format_error& arude_log_format_error)                                                           \
      {                                                                                                                \
        arude_log_message = std::format("ARUDE_LOG_DETAIL format error: {}.", arude_log_format_error.what());          \
      }                                                                                                                \
      ::arude::log(arude_log_severity, (CHANNEL), std::move(arude_log_message));                                       \
    }                                                                                                                  \
  }                                                                                                                    \
  while(false)

///
/// Logs a record at an explicitly named level.
/// Use as ARUDE_LOG("some channel", info, "{} {}", "Hello", 42), or
/// ARUDE_LOG("some channel", info, "Hello World").
///
/// \param CHANNEL Subsystem the record came from.
/// \param LEVEL Unqualified arude::log_level enumerator, as in `info`.
/// \param FMT_STR Format string, or the whole message where no arguments follow.
/// \param ... Format arguments, if any.
///
#define ARUDE_LOG(CHANNEL, LEVEL, FMT_STR, ...) ARUDE_LOG_DETAIL(CHANNEL, LEVEL, FMT_STR __VA_OPT__(, ) __VA_ARGS__)

///
/// Logs a record at debug level.
/// \see ARUDE_LOG
///
#define ARUDE_LOGD(CHANNEL, FMT_STR, ...) ARUDE_LOG((CHANNEL), debug, FMT_STR __VA_OPT__(, ) __VA_ARGS__)

///
/// Logs a record at info level.
/// \see ARUDE_LOG
///
#define ARUDE_LOGI(CHANNEL, FMT_STR, ...) ARUDE_LOG((CHANNEL), info, FMT_STR __VA_OPT__(, ) __VA_ARGS__)

///
/// Logs a record at warn level.
/// \see ARUDE_LOG
///
#define ARUDE_LOGW(CHANNEL, FMT_STR, ...) ARUDE_LOG((CHANNEL), warn, FMT_STR __VA_OPT__(, ) __VA_ARGS__)

///
/// Logs a record at error level.
/// \see ARUDE_LOG
///
#define ARUDE_LOGE(CHANNEL, FMT_STR, ...) ARUDE_LOG((CHANNEL), error, FMT_STR __VA_OPT__(, ) __VA_ARGS__)

///
/// Logs a record at critical level.
/// \see ARUDE_LOG
///
#define ARUDE_LOGF(CHANNEL, FMT_STR, ...) ARUDE_LOG((CHANNEL), critical, FMT_STR __VA_OPT__(, ) __VA_ARGS__)

///
/// Logs the enclosing function's signature at debug level.
/// Takes no message: the record is arude::function_signature() of the
/// function the macro is written in, as in "widget::resize(int) const".
///
/// Debug rather than a level of its own, because the ladder above has no trace
/// level to log it at.
///
/// \param CHANNEL Subsystem the record came from.
///
#define ARUDE_LOGT(CHANNEL) ARUDE_LOG((CHANNEL), debug, ::arude::function_signature())
