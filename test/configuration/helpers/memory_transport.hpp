///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// An in-memory transport the configuration tests share.
///
/// Several slots live in one store, so two files can be observed at once and
/// a write is immediately visible to the next read of the same slot. Test
/// hooks make a read or a write fail, hold a read until the test releases
/// it, and run a step inside a read or a write, which is how the concurrency
/// scenarios observe an operation at a chosen moment.
///
/// The injected failures throw a std::runtime_error, not an exception_t:
/// config_file must report them as io_error itself, which is what the
/// rethrow_as() path does.
///
#pragma once

#include "arude/config/common.hpp"
#include "arude/config/transport.hpp"
#include "arude/non_owning_t.hpp"

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace config_test
{

///
/// The gate a transport's read waits on.
/// Shared with the test, and owned through a shared_ptr so the transport
/// stays movable — a mutex member would delete the move constructor the
/// transport contract requires.
///
struct gate final
{
  ///
  /// Protects released.
  ///
  std::mutex mutex;

  ///
  /// Signalled when the test lets the read finish.
  ///
  std::condition_variable ready;

  ///
  /// Whether the read may finish.
  ///
  bool released = false;
};

///
/// A transport over one slot of an in-memory store.
///
class memory_transport final
{
public: // Typedefs / Constants
  ///
  /// The store every transport shares: one entry per slot.
  ///
  using store_t = std::map<std::string, std::vector<std::byte>>;

  ///
  /// The transport's name, as a plain label for diagnostics.
  ///
  static constexpr auto name = std::string_view{"memory"};

public: // Structors
  ///
  /// Constructs a transport over one slot.
  /// \param slot Which slot this transport reads and writes.
  /// \param store Where the bytes live. Held by reference and must outlive this.
  /// \param writable Whether the transport reports itself writable.
  ///
  memory_transport(std::string slot, store_t& store, bool writable = true);

public: // Accessors
  ///
  /// Returns the location, as the transport spells it.
  /// \return The location.
  ///
  [[nodiscard]] auto location() const -> std::string;

  ///
  /// Reports whether the transport can be written.
  /// \return The writable flag the transport was constructed with.
  ///
  [[nodiscard]] auto writable() const -> bool;

  ///
  /// Reads the slot, blocking on the gate where one is held.
  /// \return The bytes, or none where the slot holds nothing.
  /// \throws std::runtime_error Where the next read was made to fail.
  ///
  [[nodiscard]] auto read() const -> std::optional<std::vector<std::byte>>;

public: // Methods
  ///
  /// Writes the slot, after running the step registered for the next write.
  /// \param bytes Bytes to store.
  /// \throws std::runtime_error Where the next write was made to fail.
  ///
  auto write(std::span<const std::byte> bytes) const -> void;

  ///
  /// Makes the next read throw instead of returning.
  ///
  auto fail_reads() -> void;

  ///
  /// Makes the next write throw instead of storing.
  ///
  auto fail_writes() -> void;

  ///
  /// Makes the next read throw the exception given, unchanged. Unlike
  /// fail_reads(), which throws a std::runtime_error for the rethrow_as()
  /// path, this pins that an exception_t passes through config_file with
  /// its code and call site intact.
  /// \param failure The exception to throw, once.
  ///
  auto throw_on_read(arude::config::exception_t failure) -> void;

  ///
  /// Makes the next write throw the exception given, unchanged.
  /// \param failure The exception to throw, once.
  ///
  auto throw_on_write(arude::config::exception_t failure) -> void;

  ///
  /// Holds every read until the gate is released.
  /// \param gate The gate to wait on.
  ///
  auto hold_reads_on(std::shared_ptr<gate> gate) -> void;

  ///
  /// Registers a step to run inside the next read that finds a source,
  /// after the bytes that read returns were taken. It runs once.
  /// \param step The step. Retained until the next read runs it.
  ///
  auto run_on_read(std::function<void()> step) -> void;

  ///
  /// Registers a step to run inside the next write, before the bytes it
  /// was handed are stored. It runs once.
  /// \param step The step. Retained until the next write runs it.
  ///
  auto run_on_write(std::function<void()> step) -> void;

private: // Variables
  ///
  /// Which slot this transport reads and writes.
  ///
  std::string slot_;

  ///
  /// Where the bytes live. Never null.
  ///
  arude::non_owning_t<store_t> store_;

  ///
  /// Whether the transport reports itself writable.
  ///
  bool writable_ = true;

  ///
  /// Whether the next read throws.
  ///
  mutable bool fail_read_ = false;

  ///
  /// Whether the next write throws.
  ///
  mutable bool fail_write_ = false;

  ///
  /// The exception_t the next read throws, or none.
  ///
  mutable std::optional<arude::config::exception_t> read_exception_;

  ///
  /// The exception_t the next write throws, or none.
  ///
  mutable std::optional<arude::config::exception_t> write_exception_;

  ///
  /// The gate read() waits on, or none.
  ///
  std::shared_ptr<gate> gate_;

  ///
  /// The step the next read runs, once, after taking its bytes.
  ///
  mutable std::function<void()> on_read_;

  ///
  /// The step the next write runs, once, before storing its bytes.
  ///
  mutable std::function<void()> on_write_;
};

} // namespace config_test
