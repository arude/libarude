///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
#pragma once

#include "arude/config/common.hpp"
#include "arude/config/transport.hpp"
#include "arude/exception_report.hpp"

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
#include <string_view>
#include <system_error>
#include <vector>

namespace arude::config::detail
{

///
/// Reads a whole file into bytes, in one step.
/// Opened in binary mode, so a file written on one platform and read on
/// another yields the same bytes rather than ones with their line endings
/// rewritten.
///
/// The file is opened once and nothing is asked about it first, so it cannot
/// change between a check and the read. The path is looked at only where the
/// open fails, to tell a file that is not there from one that cannot be read,
/// and where the read finds nothing, to tell an empty file from a directory.
///
/// \param path File to read.
/// \param loc The call site a failure reports.
/// \return The contents of the file, or std::nullopt where there is no file
///         at the path: nothing of that name, a directory on the way that is
///         missing or is not a directory, or a symbolic link to nothing.
/// \throws exception_t With io_error if there is something at the path and it
///                     cannot be opened or read, a directory among them.
///
[[nodiscard]] auto read_file(const std::filesystem::path& path, std::source_location loc)
  -> std::optional<std::vector<std::byte>>;

///
/// Returns a path beside a file for a write to go to before it replaces the file.
/// In the same directory, so that the rename which follows stays on one
/// filesystem and is atomic; a random name, so that two writers, in this
/// process or in another, do not pick the same one.
///
/// \param path File the write is for.
/// \return The temporary path. Nothing is created there.
///
[[nodiscard]] auto temporary_sibling(const std::filesystem::path& path) -> std::filesystem::path;

///
/// Writes bytes to a file, replacing whatever was there in one step.
/// The bytes go to a temporary file beside it, which is then renamed over it,
/// so a reader finds either the old contents or the new, never a file that is
/// empty or part-way through being written. A symbolic link is followed: the
/// file it names is replaced, and the link stays.
///
/// The replacement is a new file. It takes the old file's permissions, but
/// not its owner, and a hard link to the old file keeps the old contents. On
/// Windows the rename can fail, and the write with it, while another handle
/// has the file open without sharing its deletion, which a std::ifstream
/// reading it at that moment may well be.
///
/// \param path File to write.
/// \param bytes Bytes to write. Not retained.
/// \param loc The call site a failure reports.
/// \throws exception_t With io_error if the file cannot be opened, written or
///                     replaced. The file is unchanged then, and no
///                     temporary file is left behind.
///
auto write_file(const std::filesystem::path& path, std::span<const std::byte> bytes, std::source_location loc) -> void;

///
///
inline auto read_file(const std::filesystem::path& path, const std::source_location loc)
  -> std::optional<std::vector<std::byte>>
{
  auto stream = std::ifstream{path, std::ios::binary};

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
      std::format("arude::config: failed while reading '{}': {}", path.string(), exception_message()),
      config_error::io_error,
      loc};
  }
}

///
///
inline auto temporary_sibling(const std::filesystem::path& path) -> std::filesystem::path
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
inline auto write_file(
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
/// The filesystem transport.
/// Owns its path, made absolute and lexically normal at construction, so
/// "./app.toml" and "/etc/app.toml" cannot become two registry entries for one
/// file.
///
/// Writing creates the directories leading to the file. An application whose
/// configuration lives somewhere like ~/.config/example/app.toml would
/// otherwise have to create them itself on every first run.
///
/// Writing replaces the file in one step, through a temporary file beside it,
/// so a read that overlaps a write, in this process or another, finds the old
/// contents or the new and never a torn file. See detail::write_file for what
/// a replacement carries over.
///
class transport_file final
{
public: // Structors
  ///
  /// Constructs a transport for the file at \p path.
  ///
  /// \param path File the configuration lives in, absolute or relative to the working directory.
  /// \throws std::filesystem::filesystem_error If a relative path cannot be resolved.
  ///
  explicit transport_file(std::filesystem::path path);

public: // Accessors
  ///
  /// The name, as a plain label for diagnostics.
  ///
  static constexpr auto name = std::string_view{"file"};

  ///
  /// Returns the normalised path this transport reads and writes.
  /// \return The path, whether or not anything is there.
  ///
  [[nodiscard]] auto path() const -> std::filesystem::path;

  ///
  /// Returns the location, "file:" plus the path.
  /// The name prefix is what keeps it distinct from an EEPROM slot whose
  /// address spells the same.
  ///
  /// \return The location, which is what the manager keys on and what errors quote.
  ///
  [[nodiscard]] auto location() const -> std::string;

  ///
  /// Reports whether this transport can be written as well as read.
  /// \return true, always: a file can be written.
  ///
  [[nodiscard]] auto writable() const -> bool;

  ///
  /// Reads the bytes at the path, in one step. See detail::read_file.
  ///
  /// \param loc Where the call was made, which is what a failure reports. Left to its default.
  /// \return The bytes, exactly as stored, or std::nullopt where there is no
  ///         file at the path, which is the source that load_policy::create
  ///         gives defaults for.
  /// \throws exception_t With io_error if there is something at the path and
  ///                     it cannot be read.
  ///
  [[nodiscard]] auto read(std::source_location loc = std::source_location::current()) const
    -> std::optional<std::vector<std::byte>>;

public: // Methods
  ///
  /// Writes bytes to the path, replacing whatever was there in one step.
  ///
  /// \param bytes Bytes to write. Not retained.
  /// \param loc Where the call was made, which is what a failure reports. Left to its default.
  /// \throws exception_t With io_error if the write fails, which leaves the
  ///                     file as it was.
  /// \throws std::filesystem::filesystem_error If the directories leading to
  ///                                          the file cannot be created.
  ///
  auto write(std::span<const std::byte> bytes, std::source_location loc = std::source_location::current()) const
    -> void;

private: // Variables
  std::filesystem::path path_;
};

///
///
// NOLINTNEXTLINE(performance-unnecessary-value-param): by value, so a caller's temporary moves in.
inline transport_file::transport_file(std::filesystem::path path)
  : path_{std::filesystem::absolute(path).lexically_normal()}
{
}

///
///
inline auto transport_file::path() const -> std::filesystem::path
{
  return path_;
}

///
///
inline auto transport_file::location() const -> std::string
{
  return std::format("file:{}", path_.string());
}

///
///
// NOLINTNEXTLINE(readability-convert-member-functions-to-static): the contract is an instance operation.
inline auto transport_file::writable() const -> bool
{
  return true;
}

///
///
inline auto transport_file::read(const std::source_location loc) const -> std::optional<std::vector<std::byte>>
{
  return detail::read_file(path_, loc);
}

///
///
inline auto transport_file::write(const std::span<const std::byte> bytes, const std::source_location loc) const -> void
{
  if(const auto parent = path_.parent_path(); !parent.empty())
  {
    std::filesystem::create_directories(parent);
  }

  detail::write_file(path_, bytes, loc);
}

} // namespace arude::config

static_assert(arude::config::transport_c<arude::config::transport_file>);
