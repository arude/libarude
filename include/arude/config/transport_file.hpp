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
#include "arude/exception.hpp"

#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace arude::config::detail
{

///
/// Reads a whole file into bytes.
/// Opened in binary mode, so a file written on one platform and read on
/// another yields the same bytes rather than ones with their line endings
/// rewritten.
///
/// \param path File to read.
/// \return The contents of the file.
/// \throws exception_t With io_error if the file cannot be opened or cannot be read.
///
[[nodiscard]] auto read_file(const std::filesystem::path& path) -> std::vector<std::byte>;

///
/// Writes bytes to a file, replacing whatever was there.
///
/// \param path File to write.
/// \param bytes Bytes to write. Not retained.
/// \throws exception_t With io_error if the file cannot be opened or cannot be written.
///
auto write_file(const std::filesystem::path& path, std::span<const std::byte> bytes) -> void;

///
///
inline auto read_file(const std::filesystem::path& path) -> std::vector<std::byte>
{
  auto stream = std::ifstream{path, std::ios::binary};

  if(!stream.is_open())
  {
    throw exception_t{
      std::format("arude::config: cannot open '{}' for reading.", path.string()), config_error::io_error};
  }

  auto text = std::string{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};

  if(stream.bad())
  {
    throw exception_t{std::format("arude::config: failed while reading '{}'.", path.string()), config_error::io_error};
  }

  return as_bytes(text);
}

///
///
inline auto write_file(const std::filesystem::path& path, const std::span<const std::byte> bytes) -> void
{
  auto stream = std::ofstream{path, std::ios::binary | std::ios::trunc};

  if(!stream.is_open())
  {
    throw exception_t{
      std::format("arude::config: cannot open '{}' for writing.", path.string()), config_error::io_error};
  }

  const auto text = as_text(bytes);
  stream.write(text.data(), static_cast<std::streamsize>(text.size()));
  stream.close();

  if(stream.fail())
  {
    throw exception_t{std::format("arude::config: failed while writing '{}'.", path.string()), config_error::io_error};
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
  /// Reports whether anything is at the path.
  /// Answering false is what lets load_policy::create know there is a
  /// configuration to write out.
  ///
  /// \return true if reading it now would find something.
  ///
  [[nodiscard]] auto exists() const -> bool;

  ///
  /// Reads the bytes at the path.
  ///
  /// \return The bytes, exactly as stored.
  /// \throws exception_t With io_error if the file cannot be read.
  ///
  [[nodiscard]] auto read() const -> std::vector<std::byte>;

public: // Methods
  ///
  /// Writes bytes to the path, replacing whatever was there.
  ///
  /// \param bytes Bytes to write. Not retained.
  /// \throws exception_t With io_error if the write fails.
  ///
  auto write(std::span<const std::byte> bytes) const -> void;

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
inline auto transport_file::exists() const -> bool
{
  return std::filesystem::exists(path_);
}

///
///
inline auto transport_file::read() const -> std::vector<std::byte>
{
  return detail::read_file(path_);
}

///
///
inline auto transport_file::write(const std::span<const std::byte> bytes) const -> void
{
  if(const auto parent = path_.parent_path(); !parent.empty())
  {
    std::filesystem::create_directories(parent);
  }

  detail::write_file(path_, bytes);
}

} // namespace arude::config

static_assert(arude::config::transport_c<arude::config::transport_file>);
