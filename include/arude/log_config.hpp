///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// The configuration of the logging subsystem, and the callback its callback
/// sink feeds.
///
/// Pure data on purpose: nothing here includes spdlog, and nothing here
/// includes the configuration support. Every consumer of arude/log.hpp pays for
/// this header being reachable, so it carries the shape of the configuration
/// and the spdlog-facing back end stays in src/log.cpp. That also lets the
/// whole struct be stored as TOML through arude::config without an adapter.
///
#pragma once

#include "arude/log.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace arude
{

///
/// One record as handed to a log_config::callback_sink.
/// The views are valid only for the duration of the call; copy what you keep.
///
struct log_record
{
  ///
  /// When the record was made, in the system clock's epoch.
  ///
  std::chrono::system_clock::time_point time;

  ///
  /// Severity of the record.
  ///
  log_level level = log_level::info;

  ///
  /// Subsystem the record came from, which is the logger's name.
  ///
  std::string_view channel;

  ///
  /// The message as arude::log() received it, before any pattern was applied.
  ///
  std::string_view message;

  ///
  /// The message after the callback sink's pattern is applied, without the
  /// line ending. This is what a file or console sink would have written.
  ///
  std::string_view formatted;

  ///
  /// spdlog's thread id, as written by the %t pattern flag.
  ///
  std::size_t thread_id = 0;
};

///
/// Everything log_session::start() needs to create the sinks and the
/// asynchronous queue.
///
/// Every sink is optional; a sink that is absent is not created. The file sinks
/// are a vector, so an application can write several files. The Windows-only
/// and POSIX-only sinks are present in the struct on every platform, so one
/// configuration can be written portably; a sink the platform does not support
/// is skipped, and log_session::start() logs a warning naming it.
///
/// Passive data: no invariant, no ownership beyond the members, and no spdlog.
/// The callback itself is not part of the configuration because it cannot be
/// written to TOML; see arude::detail::log_callback().
///
struct log_config
{
  ///
  /// The version this configuration type is, for arude::config.
  ///
  static constexpr auto config_version = std::uint32_t{1};

  ///
  /// Where a console sink writes.
  ///
  enum class console_stream : std::uint8_t
  {
    ///
    /// Standard output.
    ///
    std_out,

    ///
    /// Standard error.
    ///
    std_err
  };

  ///
  /// A sink that writes to stdout or stderr.
  ///
  struct console_sink
  {
    ///
    /// Lowest severity this sink writes. Defaults to debug, so the sinks do
    /// not filter and the logger's channel level does.
    ///
    log_level level = log_level::debug;

    ///
    /// Pattern for this sink alone, overriding log_config::pattern.
    /// Absent means log_config::pattern.
    ///
    std::optional<std::string> pattern;

    ///
    /// Stream this sink writes to. Defaults to stdout.
    ///
    console_stream stream = console_stream::std_out;

    ///
    /// Whether to colour the output. Defaults to true; a terminal that cannot
    /// colour is detected by spdlog and left plain.
    ///
    bool use_color = true;
  };

  ///
  /// A size-rotating file sink.
  ///
  struct file_sink
  {
    ///
    /// Lowest severity this sink writes. Defaults to debug.
    ///
    log_level level = log_level::debug;

    ///
    /// Pattern for this sink alone, overriding log_config::pattern.
    /// Absent means log_config::pattern.
    ///
    std::optional<std::string> pattern;

    ///
    /// Directory the files are written to, created if it does not exist.
    /// Defaults to the working directory.
    ///
    std::filesystem::path directory = ".";

    ///
    /// File name as an spdlog pattern, expanded once when the sink is created.
    /// The default writes a new file per process start, down to the second and
    /// the process id.
    ///
    std::string file_name_pattern = "%Y%m%d_%H%M%S_%P.log";

    ///
    /// Size in bytes at which the file rotates. Defaults to 512 MiB.
    ///
    std::size_t max_file_size = std::size_t{512} * 1024 * 1024;

    ///
    /// Number of rotated files kept beside the current one. Defaults to 100.
    ///
    std::size_t max_file_count = 100;

    ///
    /// Whether to rotate an existing file away when the sink opens it.
    /// Defaults to true.
    ///
    bool rotate_on_open = true;
  };

  ///
  /// A sink that feeds whatever arude::detail::log_callback() last set.
  ///
  struct callback_sink
  {
    ///
    /// Lowest severity this sink hands to the callback. Defaults to debug.
    ///
    log_level level = log_level::debug;

    ///
    /// Pattern for this sink alone, overriding log_config::pattern.
    /// The line ending is always omitted from log_record::formatted.
    ///
    std::optional<std::string> pattern;
  };

  ///
  /// A sink that writes through syslog(3). POSIX only; skipped on Windows.
  ///
  struct syslog_sink
  {
    ///
    /// Lowest severity this sink writes. Defaults to info.
    ///
    log_level level = log_level::info;

    ///
    /// Pattern for this sink alone, overriding log_config::pattern.
    /// Absent means log_config::pattern.
    ///
    std::optional<std::string> pattern;

    ///
    /// Identifier passed to openlog(). Empty means the program name.
    ///
    std::string ident;

    ///
    /// openlog() options, such as LOG_PID. Defaults to 0.
    ///
    int option = 0;

    ///
    /// Syslog facility, as openlog() takes it. The default is LOG_USER,
    /// spelled as its value so the header needs no <syslog.h>.
    ///
    int facility = 1 << 3;

    ///
    /// Whether to apply the pattern before handing the record to syslog.
    /// Defaults to false, because syslog adds its own time and host.
    ///
    bool enable_formatting = false;
  };

  ///
  /// A sink that writes through OutputDebugString. Windows only; skipped
  /// elsewhere.
  ///
  struct msvc_sink
  {
    ///
    /// Lowest severity this sink writes. Defaults to debug.
    ///
    log_level level = log_level::debug;

    ///
    /// Pattern for this sink alone, overriding log_config::pattern.
    /// Absent means log_config::pattern.
    ///
    std::optional<std::string> pattern;

    ///
    /// Whether to skip the call when no debugger is attached. Defaults to true.
    ///
    bool check_debugger_present = true;
  };

  ///
  /// A sink that writes to the Windows event log. Windows only; skipped
  /// elsewhere.
  ///
  struct event_log_sink
  {
    ///
    /// Lowest severity this sink writes. Defaults to warn.
    ///
    log_level level = log_level::warn;

    ///
    /// Pattern for this sink alone, overriding log_config::pattern.
    /// Absent means log_config::pattern.
    ///
    std::optional<std::string> pattern;

    ///
    /// Event source name. Must not be empty; the registry entry behind it is
    /// the application's to create.
    ///
    std::string source;

    ///
    /// Event id written with every record. Defaults to 1000, the id the
    /// mscoree.dll message file documents.
    ///
    std::uint32_t event_id = 1000;
  };

  ///
  /// A per-channel override of log_config::level.
  ///
  struct channel_level
  {
    ///
    /// Channel the override applies to, matched exactly.
    ///
    std::string channel;

    ///
    /// Lowest severity that channel logs. Defaults to debug.
    ///
    log_level level = log_level::debug;
  };

  ///
  /// Default level for every channel, before any channel_level override.
  /// Defaults to info.
  ///
  log_level level = log_level::info;

  ///
  /// Per-channel overrides of level. A channel not listed here uses level.
  ///
  std::vector<channel_level> channel_levels;

  ///
  /// Default pattern for every sink that names no pattern of its own.
  ///
  std::string pattern = "[%Y-%m-%d %H:%M:%S.%e] [%t] [%L] [%n] - %v";

  ///
  /// Capacity of the asynchronous queue, in records. A full queue blocks the
  /// caller until the worker has made room. Defaults to 8192.
  ///
  std::size_t queue_size = 8192;

  ///
  /// How often the periodic flush runs. Defaults to 1000 ms.
  ///
  std::chrono::milliseconds flush_interval{1000};

  ///
  /// Records at this level or above are flushed at once. Defaults to warn.
  ///
  log_level flush_level = log_level::warn;

  ///
  /// The console sink, if one is wanted.
  ///
  std::optional<console_sink> console;

  ///
  /// The file sinks, in the order they are created. Empty by default.
  ///
  std::vector<file_sink> files;

  ///
  /// The callback sink, if one is wanted. It feeds
  /// arude::detail::log_callback(); an empty callback drops records rather
  /// than failing setup.
  ///
  std::optional<callback_sink> callback;

  ///
  /// The syslog sink, if one is wanted. Skipped on Windows.
  ///
  std::optional<syslog_sink> syslog;

  ///
  /// The OutputDebugString sink, if one is wanted. Skipped outside Windows.
  ///
  std::optional<msvc_sink> msvc;

  ///
  /// The Windows event log sink, if one is wanted. Skipped outside Windows.
  ///
  std::optional<event_log_sink> event_log;
};

} // namespace arude

namespace arude::detail
{

///
/// The function a log_config::callback_sink feeds.
/// Receives one record, whose views are valid only for the duration of the
/// call. Copy what has to be kept.
///
/// An empty function drops records.
///
using log_callback_t = std::function<void(const log_record&)>;

///
/// Sets the function the callback sink feeds. An empty function drops records.
/// Replaces any earlier one.
///
/// Thread-safe; may be called before or after log_session::start(), and takes
/// effect for the next record. The callback runs on spdlog's worker thread, so
/// it must not log and should not block. This setter is temporary: it will be
/// replaced by a signal once libarude has one, which is why it is not part of
/// log_config.
///
/// \param callback The function to call, or an empty one to drop records.
///
auto log_callback(log_callback_t callback) -> void;

} // namespace arude::detail
