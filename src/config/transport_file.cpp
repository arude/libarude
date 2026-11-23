///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///

#include "arude/config/transport_file.hpp"
#include "arude/config/common.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <iterator>
#include <optional>
#include <random>
#include <source_location>
#include <span>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace arude::config::detail
{

namespace
{

///
/// How often an open or a rename that another handle got in the way of is
/// tried, and how long to wait between tries: about a tenth of a second in
/// all. On Windows a file cannot be renamed over while it is open without
/// shared deletion, which a std::ifstream is, nor opened while a rename over
/// it is under way, so a load and a store of one file at the same moment
/// collide there. Elsewhere neither happens, and a failure that is not
/// transient fails the same way on every try.
///
constexpr auto transient_attempts = 50;

///
/// \see transient_attempts
///
constexpr auto transient_pause = std::chrono::milliseconds{2};

} // namespace

///
///
auto read_file(const std::filesystem::path& path, const std::source_location loc)
  -> std::optional<std::vector<std::byte>>
{
  auto stream = std::ifstream{path, std::ios::binary};

  // Tried again only while something is at the path, so a missing file is
  // reported at once; see transient_attempts.
  for(auto attempt = 1; !stream.is_open() && attempt < transient_attempts; ++attempt)
  {
    if(auto unexamined = std::error_code{};
       std::filesystem::status(path, unexamined).type() == std::filesystem::file_type::not_found)
    {
      break;
    }

    std::this_thread::sleep_for(transient_pause);
    stream = std::ifstream{path, std::ios::binary};
  }

  if(!stream.is_open())
  {
    // Asked only now, after the open failed, so that the answer explains the
    // failure rather than predicting a read. A file created in between makes
    // this an io_error, which is the safe side: it never hands out defaults
    // for a file that is there.
    auto unexamined = std::error_code{};

    if(std::filesystem::status(path, unexamined).type() == std::filesystem::file_type::not_found)
    {
      return std::nullopt;
    }

    throw exception_t{
      std::format("arude::config: cannot open '{}' for reading.", path.string()), config_error::io_error, loc};
  }

  try
  {
    // A directory opens on some platforms, and what the read makes of it
    // depends on the standard library: libstdc++ throws
    // std::ios_base::failure from the first extraction, while libc++ reads
    // nothing and reports the end of the file. So nothing read is checked
    // against the path, rather than taken for an empty file.
    auto text = std::string{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};

    if(auto unexamined = std::error_code{};
       stream.bad() || (text.empty() && std::filesystem::is_directory(path, unexamined)))
    {
      throw exception_t{
        std::format("arude::config: failed while reading '{}'.", path.string()), config_error::io_error, loc};
    }

    return as_bytes(text);
  }
  catch(const exception_t&)
  {
    throw;
  }
  catch(...)
  {
    throw exception_t{
      std::format("arude::config: failed while reading '{}': {}", path.string(), detail::nested_message()),
      config_error::io_error,
      loc};
  }
}

///
///
auto temporary_sibling(const std::filesystem::path& path) -> std::filesystem::path
{
  auto entropy = std::random_device{};
  const auto tag = (std::uint64_t{entropy()} << 32U) | std::uint64_t{entropy()};

  // Built by appending to a path rather than formatted as a string, so a
  // file name that is not representable in the narrow encoding survives.
  auto name = std::filesystem::path{"."};
  name += path.filename();
  name += std::format(".{:016x}.tmp", tag);

  return path.parent_path() / name;
}

///
///
auto write_file(
  const std::filesystem::path& path, const std::span<const std::byte> bytes, const std::source_location loc) -> void
{
  auto target = path;

  // Renaming over a link would replace the link itself with a regular file.
  // Where the path cannot even be looked at, it is taken as it is, and the
  // open below reports the failure.
  auto unexamined = std::error_code{};

  if(std::filesystem::is_symlink(std::filesystem::symlink_status(path, unexamined)))
  {
    auto error = std::error_code{};
    target = std::filesystem::weakly_canonical(path, error);

    if(error)
    {
      throw exception_t{
        std::format("arude::config: cannot resolve the link '{}': {}", path.string(), error.message()),
        config_error::io_error,
        loc};
    }
  }

  const auto temporary = temporary_sibling(target);

  try
  {
    auto stream = std::ofstream{temporary, std::ios::binary | std::ios::trunc};

    if(!stream.is_open())
    {
      throw exception_t{
        std::format("arude::config: cannot open '{}' for writing.", temporary.string()), config_error::io_error, loc};
    }

    // Before anything is written, so contents the old file kept private are
    // not readable by others in the meantime. Best effort: where the old
    // permissions cannot be read, or there is no old file, the new one keeps
    // the defaults it was created with.
    auto ignored = std::error_code{};
    if(const auto previous = std::filesystem::status(target, ignored); std::filesystem::exists(previous))
    {
      std::filesystem::permissions(temporary, previous.permissions(), ignored);
    }

    const auto text = as_text(bytes);
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    stream.close();

    if(stream.fail())
    {
      throw exception_t{
        std::format("arude::config: failed while writing '{}'.", temporary.string()), config_error::io_error, loc};
    }

    auto error = std::error_code{};
    std::filesystem::rename(temporary, target, error);

    // A reader holding the file open refuses the rename on Windows for as
    // long as it reads; see transient_attempts.
    for(auto attempt = 1; error &&
                          (error == std::errc::permission_denied || error == std::errc::device_or_resource_busy) &&
                          attempt < transient_attempts;
        ++attempt)
    {
      std::this_thread::sleep_for(transient_pause);
      error.clear();
      std::filesystem::rename(temporary, target, error);
    }

    if(error)
    {
      throw exception_t{
        std::format("arude::config: cannot replace '{}': {}", target.string(), error.message()),
        config_error::io_error,
        loc};
    }
  }
  catch(...)
  {
    // Whatever failed, the temporary file is not to be left lying beside
    // the configuration. Nothing to report if it was never created.
    auto ignored = std::error_code{};
    std::filesystem::remove(temporary, ignored);
    throw;
  }
}

} // namespace arude::config::detail

namespace arude::config
{

///
///
// NOLINTNEXTLINE(performance-unnecessary-value-param): by value, so a caller's temporary moves in.
transport_file::transport_file(std::filesystem::path path)
  : path_{std::filesystem::absolute(path).lexically_normal()}
{
}

///
///
auto transport_file::path() const -> std::filesystem::path
{
  return path_;
}

///
///
auto transport_file::location() const -> std::string
{
  return std::format("file:{}", path_.string());
}

///
///
// NOLINTNEXTLINE(readability-convert-member-functions-to-static): the contract is an instance operation.
auto transport_file::writable() const -> bool
{
  return true;
}

///
///
auto transport_file::read(const std::source_location loc) const -> std::optional<std::vector<std::byte>>
{
  return detail::read_file(path_, loc);
}

///
///
auto transport_file::write(const std::span<const std::byte> bytes, const std::source_location loc) const -> void
{
  if(const auto parent = path_.parent_path(); !parent.empty())
  {
    std::filesystem::create_directories(parent);
  }

  detail::write_file(path_, bytes, loc);
}

} // namespace arude::config
