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

#include <cstddef>
#include <filesystem>
#include <optional>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
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
/// The replacement is a new file. Elsewhere than on Windows it takes the old
/// file's permissions, but not its owner; on Windows it inherits the
/// directory's access control, as any new file there does. A hard link to the
/// old file keeps the old contents. On Windows the rename has POSIX
/// semantics, so a reader that shares deletion, as read_file does, does not
/// stop it, while a handle that does not share deletion fails the write at
/// once. Only where POSIX semantics are not available (Windows before 1607,
/// FAT, some network shares) is a rename refused by an open handle tried
/// again, for about a tenth of a second.
///
/// \param path File to write.
/// \param bytes Bytes to write. Not retained.
/// \param loc The call site a failure reports.
/// \throws exception_t With io_error if the file cannot be opened, written or
///                     replaced. The file is unchanged then, and no
///                     temporary file is left behind.
///
auto write_file(const std::filesystem::path& path, std::span<const std::byte> bytes, std::source_location loc) -> void;

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
  /// \return The location, which is what errors quote.
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

} // namespace arude::config

static_assert(arude::config::transport_c<arude::config::transport_file>);
