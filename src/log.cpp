///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// The spdlog back end behind arude/log.hpp and arude/log_config.hpp: sink
/// construction, the per-channel asynchronous loggers, the buffer that holds
/// records made before setup, and the callback sink.
///
/// The global state is one object reached through a function-local static, and
/// it is the only place spdlog is touched. None of it appears in a header.
///
#include "arude/log.hpp"
#include "arude/exception.hpp"
#include "arude/log_config.hpp"

#include <spdlog/async_logger.h>
#include <spdlog/details/periodic_worker.h>
#include <spdlog/details/thread_pool.h>
#include <spdlog/logger.h>
#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/stdout_sinks.h>

// Conditional for the same reason the configuration support's were: the header
// does not exist, or needs a platform header that does not, on every platform.
#if !(defined _WIN32)
  #include <spdlog/sinks/syslog_sink.h>
#endif // #if !(defined _WIN32)

#if (defined _WIN32)
  #include <spdlog/sinks/msvc_sink.h>
  #include <spdlog/sinks/win_eventlog_sink.h>
#endif // #if (defined _WIN32)

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <source_location>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace arude::detail
{

namespace
{

///
/// The largest number of records buffered before setup, or after shutdown.
/// Beyond it the oldest is dropped, so a program that never sets logging up
/// cannot grow the buffer without bound.
///
constexpr auto max_buffered_records = std::size_t{1000};

///
/// The channel the setup warnings are written to.
///
constexpr auto setup_channel = std::string_view{"arude"};

///
/// Maps arude's level onto spdlog's.
///
/// \param level The level to convert.
/// \return spdlog's level for it.
///
[[nodiscard]] constexpr auto to_spdlog_level(const log_level level) -> spdlog::level::level_enum
{
  switch(level)
  {
  case log_level::debug: return spdlog::level::debug;
  case log_level::info: return spdlog::level::info;
  case log_level::warn: return spdlog::level::warn;
  case log_level::error: return spdlog::level::err;
  case log_level::critical: return spdlog::level::critical;
  case log_level::off: return spdlog::level::off;
  default: return spdlog::level::info;
  }
}

///
/// Maps spdlog's level onto arude's. trace has no arude equivalent and is
/// reported as debug.
///
/// \param level The level to convert.
/// \return arude's level for it.
///
[[nodiscard]] constexpr auto from_spdlog_level(const spdlog::level::level_enum level) -> log_level
{
  switch(level)
  {
  case spdlog::level::trace:
  case spdlog::level::debug: return log_level::debug;
  case spdlog::level::info: return log_level::info;
  case spdlog::level::warn: return log_level::warn;
  case spdlog::level::err: return log_level::error;
  case spdlog::level::critical: return log_level::critical;
  case spdlog::level::off: return log_level::off;
  default: return log_level::info;
  }
}

///
/// Hands one record to a logger. The caller holds loggers_mutex_, so the
/// logger and the thread pool it posts to cannot be dropped underneath it.
///
/// \param logger The logger to enqueue on. Must not be null.
/// \param level Severity of the record.
/// \param message Text of the record. Not retained.
///
auto enqueue(const std::shared_ptr<spdlog::logger>& logger, const log_level level, const std::string_view message)
  -> void
{
  logger->log(to_spdlog_level(level), spdlog::string_view_t{message.data(), message.size()});
}

///
/// The storage behind arude::detail::log_callback().
/// The shared_ptr is copied out under the mutex, so a setter never has to wait
/// for a callback that is already running, and the old function stays alive
/// until the sink is done with it.
///
struct callback_storage
{
  ///
  /// Guards callback.
  ///
  std::mutex mutex;

  ///
  /// The current function. Never null, but may hold an empty function.
  ///
  std::shared_ptr<log_callback_t> callback = std::make_shared<log_callback_t>();
};

///
/// Returns the one callback storage.
/// \return The storage.
///
auto callback_storage_instance() -> callback_storage&
{
  static auto storage = callback_storage{};
  return storage;
}

///
/// Returns the function the callback sink should call.
/// \return The current function, which may be empty.
///
[[nodiscard]] auto current_callback() -> std::shared_ptr<log_callback_t>
{
  auto& storage = callback_storage_instance();
  const auto lock = std::scoped_lock{storage.mutex};
  return storage.callback;
}

///
/// The sink behind log_config::callback_sink.
///
/// Runs on spdlog's worker thread, under base_sink's mutex, which is also why
/// the documentation says a callback must not block. A callback that throws is
/// caught here, because nothing may escape into the worker thread; that record
/// is dropped and the next one still runs.
///
/// Formats with its own formatter so log_record::formatted is what the other
/// sinks would have written. Its pattern is set with an empty line ending, so
/// the formatted view never carries one.
///
class log_callback_sink final
  : public spdlog::sinks::base_sink<std::mutex> // NOLINT(portability-template-virtual-member-function): spdlog's
                                                // base_sink is the template the sink is defined on.
{
protected: // Overrides
  ///
  /// \see spdlog::sinks::base_sink::sink_it_
  ///
  auto sink_it_(const spdlog::details::log_msg& msg) -> void override;

  ///
  /// \see spdlog::sinks::base_sink::flush_
  ///
  auto flush_() -> void override;

  ///
  /// Builds the formatter with no line ending, so formatted is the pattern's
  /// output alone.
  ///
  /// \param pattern Pattern to apply.
  ///
  auto set_pattern_(const std::string& pattern) -> void override;
};

///
///
auto log_callback_sink::sink_it_(const spdlog::details::log_msg& msg) -> void
{
  const auto callback = current_callback();

  if(!callback || !*callback)
  {
    return;
  }

  auto formatted = spdlog::memory_buf_t{};
  formatter_->format(msg, formatted);

  while(!formatted.empty() && (formatted.back() == '\n' || formatted.back() == '\r'))
  {
    formatted.pop_back();
  }

  const auto record = log_record{
    .time = msg.time,
    .level = from_spdlog_level(msg.level),
    .channel = std::string_view{msg.logger_name.data(), msg.logger_name.size()},
    .message = std::string_view{    msg.payload.data(),     msg.payload.size()},
    .formatted = std::string_view{      formatted.data(),       formatted.size()},
    .thread_id = msg.thread_id
  };

  try
  {
    (*callback)(record);
  }
  catch(...) // NOLINT(bugprone-empty-catch): the record is dropped, as documented.
  {
  }
}

///
///
auto log_callback_sink::flush_() -> void
{
}

///
///
auto log_callback_sink::set_pattern_(const std::string& pattern) -> void
{
  set_formatter_(std::make_unique<spdlog::pattern_formatter>(pattern, spdlog::pattern_time_type::local, std::string{}));
}

///
/// The sinks built from one configuration, and the platform-specific ones that
/// had to be skipped.
///
struct built_sinks
{
  ///
  /// The sinks, in the order the configuration names them.
  ///
  std::vector<std::shared_ptr<spdlog::sinks::sink>> sinks;

  ///
  /// Names of the configured sinks the platform cannot create.
  ///
  std::vector<std::string_view> skipped;
};

///
/// Applies a sink's level and pattern.
///
/// \param sink Sink to configure. Must not be null.
/// \param level Lowest severity the sink writes.
/// \param pattern The sink's own pattern, or empty for the default.
/// \param default_pattern The pattern used where pattern is empty.
///
auto configure_sink(
  const std::shared_ptr<spdlog::sinks::sink>& sink,
  const log_level level,
  const std::optional<std::string>& pattern,
  const std::string& default_pattern) -> void
{
  sink->set_level(to_spdlog_level(level));
  sink->set_pattern(pattern.has_value() ? *pattern : default_pattern);
}

///
/// Expands an spdlog pattern into a file name, once, through the same formatter
/// the sinks use.
///
/// \param pattern The file name pattern, as log_config::file_sink spells it.
/// \return The expanded file name.
///
[[nodiscard]] auto expand_file_name(const std::string& pattern) -> std::string
{
  auto formatter = spdlog::pattern_formatter{pattern, spdlog::pattern_time_type::local, std::string{}};
  const auto dummy = spdlog::details::log_msg{spdlog::string_view_t{}, spdlog::level::info, spdlog::string_view_t{}};
  auto buffer = spdlog::memory_buf_t{};
  formatter.format(dummy, buffer);

  return std::string{buffer.data(), buffer.size()};
}

///
/// Converts a path to the type spdlog's file sinks take.
/// That is std::wstring only where spdlog was built with SPDLOG_WCHAR_FILENAMES,
/// which is off by default even on Windows; a plain std::string otherwise. The
/// wide form is the one that keeps a non-ASCII path intact.
///
/// \param path The path to convert.
/// \return The path, in spdlog's filename type.
///
[[nodiscard]] auto spdlog_filename(const std::filesystem::path& path) -> spdlog::filename_t
{
#if (defined _WIN32) && (defined SPDLOG_WCHAR_FILENAMES)
  return path.wstring();
#else
  return path.string();
#endif // #if (defined _WIN32) && (defined SPDLOG_WCHAR_FILENAMES)
}

///
/// Builds every configured sink, before anything is swapped in.
///
/// \param config The configuration. Not retained.
/// \param location The call site a failure reports.
/// \return The sinks, and the platform-specific ones that were skipped.
/// \throws arude::exception If a directory cannot be created, a file cannot be
///         opened, or a Windows event log sink has an empty source.
///
[[nodiscard]] auto build_sinks(const log_config& config, const std::source_location location) -> built_sinks
{
  auto built = built_sinks{};

  if(config.console.has_value())
  {
    const auto& console = *config.console;
    auto sink = std::shared_ptr<spdlog::sinks::sink>{};

    if(console.stream == log_config::console_stream::std_err)
    {
      if(console.use_color)
      {
        sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
      }
      else
      {
        sink = std::make_shared<spdlog::sinks::stderr_sink_mt>();
      }
    }
    else
    {
      if(console.use_color)
      {
        sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
      }
      else
      {
        sink = std::make_shared<spdlog::sinks::stdout_sink_mt>();
      }
    }

    configure_sink(sink, console.level, console.pattern, config.pattern);
    built.sinks.push_back(std::move(sink));
  }

  for(const auto& file : config.files)
  {
    auto error = std::error_code{};

    if(!file.directory.empty())
    {
      std::filesystem::create_directories(file.directory, error);
    }

    if(error)
    {
      throw exception<void>{
        std::format(
          "arude: log setup: the log directory '{}' could not be created: {}.",
          file.directory.string(),
          error.message()),
        location};
    }

    const auto file_path = file.directory / expand_file_name(file.file_name_pattern);

    auto sink = std::shared_ptr<spdlog::sinks::sink>{};

    try
    {
      sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        spdlog_filename(file_path), file.max_file_size, file.max_file_count, file.rotate_on_open);
    }
    catch(const std::exception& ex)
    {
      throw exception<void>{
        std::format("arude: log setup: the log file '{}' could not be opened: {}.", file_path.string(), ex.what()),
        location};
    }

    configure_sink(sink, file.level, file.pattern, config.pattern);
    built.sinks.push_back(std::move(sink));
  }

  if(config.callback.has_value())
  {
    const auto sink = std::make_shared<log_callback_sink>();
    configure_sink(sink, config.callback->level, config.callback->pattern, config.pattern);
    built.sinks.push_back(sink);
  }

#if !(defined _WIN32)

  if(config.syslog.has_value())
  {
    const auto& syslog = *config.syslog;
    const auto sink = std::make_shared<spdlog::sinks::syslog_sink_mt>(
      syslog.ident, syslog.option, syslog.facility, syslog.enable_formatting);
    configure_sink(sink, syslog.level, syslog.pattern, config.pattern);
    built.sinks.push_back(sink);
  }

#else

  if(config.syslog.has_value())
  {
    built.skipped.emplace_back("syslog");
  }

#endif // #if !(defined _WIN32)

#if (defined _WIN32)

  if(config.msvc.has_value())
  {
    const auto sink = std::make_shared<spdlog::sinks::msvc_sink_mt>(config.msvc->check_debugger_present);
    configure_sink(sink, config.msvc->level, config.msvc->pattern, config.pattern);
    built.sinks.push_back(sink);
  }

  if(config.event_log.has_value())
  {
    if(config.event_log->source.empty())
    {
      throw exception<void>{"arude: log setup: the Windows event log sink has an empty source.", location};
    }

    const auto sink =
      std::make_shared<spdlog::sinks::win_eventlog_sink_mt>(config.event_log->source, config.event_log->event_id);
    configure_sink(sink, config.event_log->level, config.event_log->pattern, config.pattern);
    built.sinks.push_back(sink);
  }

#else

  if(config.msvc.has_value())
  {
    built.skipped.emplace_back("msvc");
  }

  if(config.event_log.has_value())
  {
    built.skipped.emplace_back("event_log");
  }

#endif // #if (defined _WIN32)

  return built;
}

///
/// Builds the warning that names the sinks a platform could not create.
///
/// \param skipped The names, from build_sinks().
/// \return The message.
///
[[nodiscard]] auto skipped_message(const std::vector<std::string_view>& skipped) -> std::string
{
  auto message = std::string{"Configured log sinks skipped, because this platform has no such sink:"};

  for(const auto name : skipped)
  {
    message += ' ';
    message += name;
    message += ';';
  }

  return message;
}

///
/// One record held until a session exists.
///
struct buffered_record
{
  ///
  /// Severity of the record.
  ///
  log_level level = log_level::info;

  ///
  /// Channel the record came from.
  ///
  std::string channel;

  ///
  /// The message, already formatted.
  ///
  std::string message;
};

///
/// The global logging state: the sinks and loggers of the running session, the
/// buffer for records made while there is none, and the level the macro's
/// early-out reads.
///
/// One object, reached through log_state_instance(), so its lifetime is the
/// program's and its construction is thread-safe.
///
/// Lock order: setup_mutex_ before loggers_mutex_ before buffer_mutex_. The
/// periodic flusher takes only loggers_mutex_, so stopping it under
/// setup_mutex_ cannot deadlock.
///
class log_state final
{
public: // Methods
  ///
  /// Tears the live session down, builds the configuration's sinks, and
  /// installs them. If building fails, rebuilds and restores the previous
  /// configuration and rethrows the original failure; if that restoration fails
  /// too, leaves no session and rethrows the original failure.
  ///
  /// \param config The configuration. Not retained.
  /// \param location The call site a failure reports.
  /// \return The generation the returned session owns.
  /// \throws arude::exception As build_sinks() does, after the previous
  ///         configuration has been restored or has itself failed to restore.
  ///
  auto setup(const log_config& config, std::source_location location) -> std::uint64_t;

  ///
  /// Shuts down the session that owns generation, if that is still the live
  /// one. Does nothing for a moved-from session or one a later setup replaced.
  /// Only arude's own back end is shut down; spdlog's global registry is not
  /// touched.
  ///
  /// \param generation Generation the session owns.
  ///
  auto release(std::uint64_t generation) noexcept -> void;

  ///
  /// Hands one record to its channel's logger, or buffers it when no session
  /// is live. The no-session check and the append happen under the same shared
  /// lock, so a setup that installs between them cannot strand the record.
  ///
  /// \param level Severity of the record.
  /// \param channel Subsystem the record came from. Not retained.
  /// \param message Text of the record. Moved in.
  ///
  auto write(log_level level, std::string_view channel, std::string message) -> void;

public: // Accessors
  ///
  /// Returns the level the macro's early-out reads. Relaxed, because it is a
  /// threshold and not a synchronisation point.
  ///
  /// \return The current global level.
  ///
  [[nodiscard]] auto global_level() const noexcept -> log_level;

  ///
  /// Overrides the level the macro's early-out reads.
  ///
  /// \param level The level to report.
  ///
  auto global_level(log_level level) noexcept -> void;

private: // Helpers
  ///
  /// Creates a logger for one channel over the current sinks and thread pool.
  ///
  /// \param channel Channel to name the logger after.
  /// \return The logger.
  ///
  [[nodiscard]] auto make_logger(const std::string& channel) -> std::shared_ptr<spdlog::logger>;

  ///
  /// Holds one record until a session exists, dropping the oldest beyond the
  /// bound. Caller holds loggers_mutex_, which is what keeps a setup from
  /// draining the buffer between the no-session check and this append.
  ///
  /// \param level Severity of the record.
  /// \param channel Subsystem the record came from.
  /// \param message Text of the record. Moved in.
  ///
  auto append_buffered(log_level level, std::string_view channel, std::string message) -> void;

  ///
  /// Takes everything buffered, in order, and leaves the buffer empty. Caller
  /// holds loggers_mutex_ exclusively, so nothing can be appended between the
  /// install of a session and this drain.
  ///
  /// \return The buffered records.
  ///
  [[nodiscard]] auto take_buffered() -> std::vector<buffered_record>;

  ///
  /// Installs one built configuration into the state. Caller holds
  /// loggers_mutex_ exclusively, and no session is live: setup() tore the old
  /// one down first, so its files are closed and its syslog sink has already
  /// called ::closelog().
  ///
  /// \param config The configuration being installed. Not retained.
  /// \param sinks The sinks to install, moved in.
  /// \param pool The thread pool to install, moved in.
  ///
  auto install_locked(
    const log_config& config,
    std::vector<std::shared_ptr<spdlog::sinks::sink>> sinks,
    std::shared_ptr<spdlog::details::thread_pool> pool) -> void;

  ///
  /// Installs a built configuration, starts its flusher, and replays the
  /// buffer. Called with the setup lock held and no session live.
  ///
  /// \param config The configuration being activated. Not retained.
  /// \param built The sinks and skipped-sink names, moved in.
  /// \param pool The thread pool to activate, moved in.
  /// \return The generation the new session owns.
  ///
  auto activate(const log_config& config, built_sinks built, std::shared_ptr<spdlog::details::thread_pool> pool)
    -> std::uint64_t;

  ///
  /// Flushes every live logger. Called by the periodic worker.
  ///
  auto flush_loggers() -> void;

  ///
  /// Flushes and drops the live session's resources. Caller holds
  /// loggers_mutex_ exclusively, and has already stopped flusher_.
  ///
  auto teardown_locked() -> void;

private: // Variables
  ///
  /// Serialises setup() and release(), and guards flusher_ and
  /// previous_config_.
  ///
  std::mutex setup_mutex_;

  ///
  /// The configuration of the live session, if any. Kept so a setup that fails
  /// to build its new sinks can rebuild this one and leave it running.
  /// Guarded by setup_mutex_.
  ///
  std::optional<log_config> previous_config_;

  ///
  /// Guards the session below: active_generation_, sinks_, loggers_,
  /// thread_pool_, and the level tables.
  ///
  mutable std::shared_mutex loggers_mutex_;

  ///
  /// Generation of the live session, or 0 when there is none.
  ///
  std::uint64_t active_generation_ = 0;

  ///
  /// Never decreases, so a generation names one setup for the program's life.
  ///
  std::uint64_t next_generation_ = 0;

  ///
  /// Sinks of the live session, shared by every logger.
  ///
  std::vector<std::shared_ptr<spdlog::sinks::sink>> sinks_;

  ///
  /// Loggers of the live session, by channel.
  ///
  std::map<std::string, std::shared_ptr<spdlog::logger>, std::less<>> loggers_;

  ///
  /// Thread pool the live session's loggers post to.
  ///
  std::shared_ptr<spdlog::details::thread_pool> thread_pool_;

  ///
  /// Periodic flush of the live session. Destroyed before thread_pool_, so its
  /// thread is joined first.
  ///
  std::unique_ptr<spdlog::details::periodic_worker> flusher_;

  ///
  /// Default pattern of the live session.
  ///
  std::string pattern_;

  ///
  /// Default level of the live session.
  ///
  log_level default_level_ = log_level::info;

  ///
  /// Channel overrides of the live session.
  ///
  std::map<std::string, log_level, std::less<>> channel_levels_;

  ///
  /// Level at which the live session's loggers flush at once.
  ///
  log_level flush_level_ = log_level::warn;

  ///
  /// Guards buffer_.
  ///
  std::mutex buffer_mutex_;

  ///
  /// Records made while no session is live.
  ///
  std::vector<buffered_record> buffer_;

  ///
  /// The level the macro's early-out reads. Debug before setup, and after
  /// shutdown, so the buffer catches everything.
  ///
  std::atomic<log_level> global_level_{log_level::debug};
};

///
///
auto log_state::setup(const log_config& config, const std::source_location location) -> std::uint64_t
{
  const auto setup_lock = std::scoped_lock{setup_mutex_};

  // The old session goes first. A new file sink then opens after the old one
  // has been closed, so rotate_on_open can rename a file the old session held;
  // on Windows the rename would fail while that file is still open. It also
  // means the old syslog sink's ::closelog() runs before the new one's
  // ::openlog(), so the new ident and facility are the ones in force.
  flusher_.reset();

  auto previous_generation = std::uint64_t{};

  {
    const auto lock = std::unique_lock{loggers_mutex_};
    previous_generation = active_generation_;
    teardown_locked();
  }

  try
  {
    auto built = build_sinks(config, location);
    auto pool = std::make_shared<spdlog::details::thread_pool>(std::max<std::size_t>(config.queue_size, 1), 1);
    const auto generation = activate(config, std::move(built), std::move(pool));
    previous_config_ = config;

    return generation;
  }
  catch(...)
  {
    // Building the new configuration failed. Put the previous one back, so a
    // session a caller already holds keeps working, and report the failure that
    // was asked for rather than the one the restoration might have met.
    const auto original = std::current_exception();

    if(previous_config_.has_value())
    {
      try
      {
        auto built = build_sinks(*previous_config_, location);
        auto pool =
          std::make_shared<spdlog::details::thread_pool>(std::max<std::size_t>(previous_config_->queue_size, 1), 1);
        [[maybe_unused]] const auto generation = activate(*previous_config_, std::move(built), std::move(pool));

        // The restored session is the one the caller already holds a handle
        // for, so it keeps owning it: its destructor still shuts it down. Only
        // a session a successful start() superseded goes inert.
        const auto lock = std::unique_lock{loggers_mutex_};
        active_generation_ = previous_generation;
      }
      catch(...)
      {
        // The previous configuration could not be rebuilt either. End with no
        // session, so records buffer and the original failure can be reported.
        try
        {
          flusher_.reset();

          const auto lock = std::unique_lock{loggers_mutex_};
          teardown_locked();
        }
        catch(...) // NOLINT(bugprone-empty-catch): ending with no session is best effort.
        {
        }

        previous_config_.reset();
      }
    }

    std::rethrow_exception(original);
  }
}

///
///
auto log_state::release(const std::uint64_t generation) noexcept -> void
{
  if(generation == 0)
  {
    return;
  }

  try
  {
    const auto setup_lock = std::scoped_lock{setup_mutex_};

    {
      const auto lock = std::shared_lock{loggers_mutex_};

      if(generation != active_generation_)
      {
        return; // A later setup replaced this session.
      }
    }

    // Stopped before the lock is taken: its thread takes the same lock.
    flusher_.reset();

    {
      const auto lock = std::unique_lock{loggers_mutex_};

      if(generation != active_generation_)
      {
        return;
      }

      teardown_locked();
    }

    previous_config_.reset();
  }
  catch(...) // NOLINT(bugprone-empty-catch): a destructor must never throw.
  {
  }
}

///
///
auto log_state::write(const log_level level, const std::string_view channel, std::string message) -> void
{
  {
    const auto lock = std::shared_lock{loggers_mutex_};

    if(active_generation_ == 0)
    {
      // Appended under the same lock that saw no session, so a setup cannot
      // install and drain between the check and the append.
      append_buffered(level, channel, std::move(message));
      return;
    }

    if(const auto it = loggers_.find(channel); it != loggers_.end())
    {
      // Enqueued while the shared lock is held, so teardown cannot drop this
      // logger or its thread pool before the record is posted. The worker never
      // takes loggers_mutex_, so a full queue blocking here cannot deadlock.
      enqueue(it->second, level, message);
      return;
    }
  }

  // The channel has no logger yet. Creating one needs the exclusive lock, and
  // the enqueue happens under it for the same reason.
  const auto lock = std::unique_lock{loggers_mutex_};

  if(active_generation_ == 0)
  {
    append_buffered(level, channel, std::move(message));
    return;
  }

  if(const auto it = loggers_.find(channel); it != loggers_.end())
  {
    enqueue(it->second, level, message);
    return;
  }

  const auto logger = make_logger(std::string{channel});
  loggers_.emplace(std::string{channel}, logger);
  enqueue(logger, level, message);
}

///
///
auto log_state::install_locked(
  const log_config& config,
  std::vector<std::shared_ptr<spdlog::sinks::sink>> sinks,
  std::shared_ptr<spdlog::details::thread_pool> pool) -> void
{
  sinks_ = std::move(sinks);
  thread_pool_ = std::move(pool);
  pattern_ = config.pattern;
  default_level_ = config.level;
  flush_level_ = config.flush_level;

  channel_levels_.clear();

  for(const auto& channel_level : config.channel_levels)
  {
    channel_levels_.insert_or_assign(channel_level.channel, channel_level.level);
  }

  // The minimum, so the macro's early-out never drops a record some channel
  // would accept.
  auto minimum = config.level;

  for(const auto& channel_level : config.channel_levels)
  {
    minimum = std::min(minimum, channel_level.level);
  }

  global_level_.store(minimum, std::memory_order_relaxed);

  ++next_generation_;
  active_generation_ = next_generation_;
}

///
///
auto log_state::activate(
  const log_config& config, built_sinks built, std::shared_ptr<spdlog::details::thread_pool> pool) -> std::uint64_t
{
  auto generation = std::uint64_t{};
  auto pending = std::vector<buffered_record>{};

  // Started before the session is installed: if creating its worker fails, no
  // session has been installed and the caller's rollback ends with none.
  flusher_ =
    std::make_unique<spdlog::details::periodic_worker>([this]() -> void { flush_loggers(); }, config.flush_interval);

  {
    const auto lock = std::unique_lock{loggers_mutex_};
    install_locked(config, std::move(built.sinks), std::move(pool));
    generation = active_generation_;

    // Drained while the session is installed *and the lock is still held*, so a
    // writer that saw no session either appended before this point (and its
    // record is taken here) or appends after the unlock, when it sees the new
    // session. Nothing can be appended between the two and be stranded.
    pending = take_buffered();
  }

  for(auto& record : pending)
  {
    write(record.level, record.channel, std::move(record.message));
  }

  if(!built.skipped.empty())
  {
    write(log_level::warn, setup_channel, skipped_message(built.skipped));
  }

  return generation;
}

///
///
auto log_state::global_level() const noexcept -> log_level
{
  return global_level_.load(std::memory_order_relaxed);
}

///
///
auto log_state::global_level(const log_level level) noexcept -> void
{
  global_level_.store(level, std::memory_order_relaxed);
}

///
///
auto log_state::make_logger(const std::string& channel) -> std::shared_ptr<spdlog::logger>
{
  auto logger = std::make_shared<spdlog::async_logger>(
    channel, sinks_.begin(), sinks_.end(), thread_pool_, spdlog::async_overflow_policy::block);

  const auto level = [this, &channel] -> log_level
  {
    if(const auto it = channel_levels_.find(channel); it != channel_levels_.end())
    {
      return it->second;
    }

    return default_level_;
  }();

  logger->set_level(to_spdlog_level(level));
  logger->flush_on(to_spdlog_level(flush_level_));

  return logger;
}

///
///
auto log_state::append_buffered(const log_level level, const std::string_view channel, std::string message) -> void
{
  const auto lock = std::scoped_lock{buffer_mutex_};

  if(buffer_.size() >= max_buffered_records)
  {
    buffer_.erase(buffer_.begin());
  }

  buffer_.push_back(buffered_record{.level = level, .channel = std::string{channel}, .message = std::move(message)});
}

///
///
auto log_state::take_buffered() -> std::vector<buffered_record>
{
  const auto lock = std::scoped_lock{buffer_mutex_};
  auto records = std::vector<buffered_record>{};
  records.swap(buffer_);

  return records;
}

///
///
auto log_state::flush_loggers() -> void
{
  auto loggers = std::vector<std::shared_ptr<spdlog::logger>>{};

  {
    const auto lock = std::shared_lock{loggers_mutex_};
    loggers.reserve(loggers_.size());

    for(const auto& entry : loggers_)
    {
      loggers.push_back(entry.second);
    }
  }

  for(const auto& logger : loggers)
  {
    logger->flush();
  }
}

///
///
auto log_state::teardown_locked() -> void
{
  for(auto& entry : loggers_)
  {
    entry.second->flush();
  }

  loggers_.clear();
  sinks_.clear();
  thread_pool_.reset();
  active_generation_ = 0;
  global_level_.store(log_level::debug, std::memory_order_relaxed);
}

///
/// Returns the one logging state.
/// \return The state.
///
auto log_state_instance() -> log_state&
{
  static auto state = log_state{};
  return state;
}

} // namespace

} // namespace arude::detail

namespace arude
{

///
///
auto log(const log_level level, const std::string_view channel, std::string message) -> void
{
  detail::log_state_instance().write(level, channel, std::move(message));
}

///
///
auto log_global_log_level() -> log_level
{
  return detail::log_state_instance().global_level();
}

///
///
auto log_global_log_level(const log_level level) -> void
{
  detail::log_state_instance().global_level(level);
}

///
///
log_session::log_session(const std::uint64_t generation)
  : generation_{generation}
{
}

///
///
log_session::~log_session() noexcept
{
  detail::log_state_instance().release(generation_);
}

///
///
log_session::log_session(log_session&& other) noexcept
  : generation_{std::exchange(other.generation_, 0)}
{
}

///
///
auto log_session::operator=(log_session&& other) noexcept -> log_session&
{
  if(this != &other)
  {
    detail::log_state_instance().release(generation_);
    generation_ = std::exchange(other.generation_, 0);
  }

  return *this;
}

///
///
auto log_session::start(const log_config& config, const std::source_location location) -> log_session
{
  return log_session{detail::log_state_instance().setup(config, location)};
}

} // namespace arude

namespace arude::detail
{

///
///
auto log_callback(log_callback_t callback) -> void
{
  auto& storage = callback_storage_instance();
  const auto lock = std::scoped_lock{storage.mutex};
  storage.callback = std::make_shared<log_callback_t>(std::move(callback));
}

} // namespace arude::detail
