///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///

#include "test/configuration/helpers/memory_transport.hpp"

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace config_test
{

///
///
memory_transport::memory_transport(std::string slot, store_t& store, const bool writable)
  : slot_{std::move(slot)}
  , store_{&store}
  , writable_{writable}
{
}

///
///
auto memory_transport::location() const -> std::string
{
  return std::string{name} + ":" + slot_;
}

///
///
auto memory_transport::writable() const -> bool
{
  return writable_;
}

///
///
auto memory_transport::read() const -> std::optional<std::vector<std::byte>>
{
  if(gate_)
  {
    auto lock = std::unique_lock{gate_->mutex};
    gate_->ready.wait(lock, [this] -> bool { return gate_->released; });
  }

  if(fail_read_)
  {
    fail_read_ = false;
    throw std::runtime_error{"memory transport: the read was made to fail."};
  }

  if(read_exception_.has_value())
  {
    auto failure = *read_exception_;
    read_exception_.reset();
    // NOLINTNEXTLINE(misc-throw-by-value-catch-by-reference): the exception is concrete, so nothing can slice.
    throw failure;
  }

  if(const auto found = store_->find(slot_); found != store_->end())
  {
    auto bytes = found->second;

    if(on_read_)
    {
      // Taken before the step runs, so the read returns the bytes the
      // source held when it started; a step that replaces them is seen by
      // the next read, not this one. Cleared first, so a read the step
      // starts runs without it.
      auto step = std::exchange(on_read_, nullptr);
      step();
    }

    return bytes;
  }

  return std::nullopt;
}

///
///
auto memory_transport::write(const std::span<const std::byte> bytes) const -> void
{
  if(fail_write_)
  {
    fail_write_ = false;
    throw std::runtime_error{"memory transport: the write was made to fail."};
  }

  if(write_exception_.has_value())
  {
    auto failure = *write_exception_;
    write_exception_.reset();
    // NOLINTNEXTLINE(misc-throw-by-value-catch-by-reference): the exception is concrete, so nothing can slice.
    throw failure;
  }

  if(on_write_)
  {
    // Cleared before it runs, so a write the step starts runs without it.
    auto step = std::exchange(on_write_, nullptr);
    step();
  }

  store_->insert_or_assign(slot_, std::vector<std::byte>{bytes.begin(), bytes.end()});
}

///
///
auto memory_transport::fail_reads() -> void
{
  fail_read_ = true;
}

///
///
auto memory_transport::fail_writes() -> void
{
  fail_write_ = true;
}

///
///
auto memory_transport::throw_on_read(arude::config::exception_t failure) -> void
{
  read_exception_ = std::move(failure);
}

///
///
auto memory_transport::throw_on_write(arude::config::exception_t failure) -> void
{
  write_exception_ = std::move(failure);
}

///
///
auto memory_transport::hold_reads_on(std::shared_ptr<gate> gate) -> void
{
  gate_ = std::move(gate);
}

///
///
auto memory_transport::run_on_read(std::function<void()> step) -> void
{
  on_read_ = std::move(step);
}

///
///
auto memory_transport::run_on_write(std::function<void()> step) -> void
{
  on_write_ = std::move(step);
}

} // namespace config_test
