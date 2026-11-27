///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// Logging, as a set of macros over arude::log, and the setup that points it at
/// its sinks.
///
/// log_session::start() configures the back end and returns a session whose
/// destruction flushes and shuts it down. Records made before setup, and after
/// the session ends, are buffered and replayed, so start-up messages are not
/// lost. The configuration itself is arude/log_config.hpp, which this header
/// does not include: it is forward-declared here so that a consumer that never
/// sets logging up pays for nothing.
///
/// Macros here rather than functions, for the one thing a function cannot do:
/// leave the arguments unevaluated when the record would be discarded anyway.
/// A filtered-out ARUDE_LOGD costs a level comparison and never formats, never
/// allocates, and never calls whatever the caller wrote in the argument list.
/// That is the whole reason the conventions' "macros are a last resort" is
/// spent here.
///
/// Macros do not cross a module boundary. A module consumer gets arude::log,
/// arude::log_level, arude::log_global_log_level, arude::log_session, and the
/// configuration types from `import arude`, but has to include this header for
/// the macros themselves.
///
#pragma once

#include "arude/function_signature.hpp"

#include <cstdint>
#include <format>
#include <source_location>
#include <string>
#include <string_view>
#include <utility>

namespace arude
{

struct log_config;

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
/// Before setup, and after the session has ended, the record is buffered and
/// replayed in order when log_session::start() succeeds. Once a session is live it is
/// handed to the channel's asynchronous logger, which applies the channel's
/// level and then each sink's own level.
///
/// \param level Severity of the record.
/// \param channel Subsystem the record came from. Not retained beyond the call.
/// \param message Text of the record, already formatted.
///
auto log(log_level level, std::string_view channel, std::string message) -> void;

///
/// The level below which records are discarded.
/// Before setup, and after shutdown, it is debug, so the buffering catches
/// everything. After setup it is the minimum of the configured level and every
/// channel override, so the macro's early-out never drops a record some channel
/// would accept. The cheapest possible read: an atomic, relaxed.
///
/// \return The lowest severity that is currently logged.
///
[[nodiscard]] auto log_global_log_level() -> log_level;

///
/// Overrides the level log_global_log_level() reports.
/// Used by the macro's early-out only; the channel's logger and each sink still
/// apply their own level, so raising this does not discard a record that a
/// logger would accept and lowering it does not make one accepted.
///
/// \param level Lowest severity that the macro's early-out lets through.
///
auto log_global_log_level(log_level level) -> void;

///
/// RAII handle on a running logging session.
///
/// One session per log_session::start(): the destructor flushes, shuts the back
/// end down, and returns logging to its pre-setup state, where records are
/// buffered again. Move-only; a moved-from session is inert, so exactly one
/// session shuts a given setup down. If start() is called while a session is
/// live, the new configuration replaces the old one and the old session's
/// destructor does nothing; the generation it carries no longer matches.
///
/// The constructor does no fallible work: start() builds the sinks and returns
/// the session, so a failed setup never leaves a session to destroy.
///
class log_session final
{
public: // Structors
  ///
  /// Constructs an inert session, which shuts nothing down.
  ///
  log_session() = default;

  ///
  /// Flushes the session's sinks and shuts arude's own back end down, unless
  /// this session has been moved from or superseded by a later
  /// log_session::start(). spdlog's global registry is not touched.
  ///
  ~log_session() noexcept;

  ///
  /// A session owns one setup, so it is not copied.
  ///
  log_session(const log_session&) = delete;

  ///
  /// Takes the other session's generation and leaves it inert.
  /// \param other Session to move from.
  ///
  log_session(log_session&& other) noexcept;

  ///
  /// \see log_session(const log_session&)
  ///
  auto operator=(const log_session&) -> log_session& = delete;

  ///
  /// Shuts this session down, then takes the other's generation and leaves it
  /// inert.
  /// \param other Session to move from.
  /// \return This session.
  ///
  auto operator=(log_session&& other) noexcept -> log_session&;

public: // Methods
  ///
  /// Configures the logging back end and returns the session that owns it.
  /// This is the one way to start logging.
  ///
  /// The previous session, if any, is torn down before the new sinks are built,
  /// so a new file sink can open a file the old one had open and a new syslog
  /// sink's ident replaces the old one's. One asynchronous logger per channel is
  /// created lazily on first use and named after the channel, so the %n pattern
  /// flag prints it. All loggers share one sink list and one thread pool.
  /// Records buffered before setup, or after the previous session ended, are
  /// replayed in order once the new sinks are in place.
  ///
  /// If a sink cannot be created, the previous configuration is rebuilt and
  /// restored, so a session an earlier start() returned keeps working, and the
  /// original failure is rethrown. If the previous configuration cannot be
  /// rebuilt either, no session is left running and records are buffered until
  /// the next start(). A failed first start() leaves no session.
  ///
  /// \param config Sinks, levels and queue settings. Not retained.
  /// \param location The call site a failure reports. Left to its default.
  /// \return The running session.
  /// \throws arude::exception If a sink cannot be created: a file cannot be
  ///         opened, a log directory cannot be created, or an event_log sink has
  ///         an empty source. Thrown after restoring the previous configuration,
  ///         or after ending with no session if that restoration failed too.
  ///
  [[nodiscard]] static auto
  start(const log_config& config, std::source_location location = std::source_location::current()) -> log_session;

private: // Structors
  ///
  /// Constructs a session for one generation, which start() is the only caller
  /// of.
  ///
  /// \param generation Generation the session owns.
  ///
  explicit log_session(std::uint64_t generation);

private: // Variables
  ///
  /// Generation of the setup this session owns; 0 is inert.
  ///
  std::uint64_t generation_ = 0;
};

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
