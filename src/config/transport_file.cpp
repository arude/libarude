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

#if (defined _WIN32)
  #if !(defined WIN32_LEAN_AND_MEAN)
    #define WIN32_LEAN_AND_MEAN
  #endif // #if !(defined WIN32_LEAN_AND_MEAN)
  #if !(defined NOMINMAX)
    #define NOMINMAX
  #endif // #if !(defined NOMINMAX)
  #include <windows.h>

  #include <algorithm>
  #include <array>
  #include <chrono>
  #include <cstring>
  #include <memory>
  #include <thread>
#else
  #include <fstream>
  #include <ios>
  #include <iterator>
#endif // #if (defined _WIN32)

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <optional>
#include <random>
#include <source_location>
#include <span>
#include <string>
#include <system_error>
#include <vector>

namespace arude::config::detail
{

namespace
{

#if (defined _WIN32)

///
/// How often the classic (non-POSIX) rename is tried, and how long to wait
/// between tries: about a tenth of a second in all. Only the fallback path
/// needs it: without POSIX semantics a file cannot be renamed over while
/// another handle has it open, and a foreign holder may let go at any moment.
/// The POSIX path never retries.
///
constexpr auto transient_attempts = 50;

///
/// \see transient_attempts
///
constexpr auto transient_pause = std::chrono::milliseconds{2};

///
/// Owns a Win32 file handle, closing it when it goes away, so no raw
/// CloseHandle calls are left scattered around.
///
class win_handle final
{
public:
  ///
  /// Takes ownership of \p handle.
  ///
  explicit win_handle(HANDLE handle);

  ///
  /// Closes the handle.
  ///
  ~win_handle();

  win_handle(const win_handle&) = delete;
  win_handle(win_handle&&) = delete;
  auto operator=(const win_handle&) -> win_handle& = delete;
  auto operator=(win_handle&&) -> win_handle& = delete;

  ///
  /// Returns the handle.
  /// \return The handle.
  ///
  [[nodiscard]] auto get() const -> HANDLE;

  ///
  /// Reports whether the handle is a real one.
  /// \return false for INVALID_HANDLE_VALUE, true for a handle.
  ///
  [[nodiscard]] auto valid() const -> bool;

private: // Variables
  ///
  /// The handle, or INVALID_HANDLE_VALUE.
  ///
  HANDLE handle_ = INVALID_HANDLE_VALUE;
};

///
///
win_handle::win_handle(HANDLE handle)
  : handle_{handle}
{
}

///
///
win_handle::~win_handle()
{
  if(valid())
  {
    ::CloseHandle(handle_);
  }
}

///
///
auto win_handle::get() const -> HANDLE
{
  return handle_;
}

///
///
auto win_handle::valid() const -> bool
{
  return handle_ != INVALID_HANDLE_VALUE;
}

///
/// Describes a Win32 error code.
/// \param code The error.
/// \return The message.
///
[[nodiscard]] auto win_message(DWORD code) -> std::string
{
  return std::error_code{static_cast<int>(code), std::system_category()}.message();
}

///
/// FILE_RENAME_INFO as Windows 10 1607 lays it out, with the flags in place of
/// ReplaceIfExists: the two share their first four bytes, so a flags word of
/// 1 reads as ReplaceIfExists = TRUE to the classic rename. Declared here, as
/// git does, because the SDK declares the flags, FileRenameInfoEx and the
/// FILE_RENAME_FLAG_ constants only for a target of 1607 or later, and
/// raising _WIN32_WINNT in one file lowers NTDDI_VERSION to plain Windows 10,
/// which hides them again.
///
struct rename_info
{
  DWORD flags;
  HANDLE root_directory;
  DWORD file_name_length;
  WCHAR file_name[1];
};

///
/// FileRenameInfoEx of FILE_INFO_BY_HANDLE_CLASS.
///
constexpr auto file_rename_info_ex = static_cast<FILE_INFO_BY_HANDLE_CLASS>(22);

///
/// FILE_RENAME_FLAG_REPLACE_IF_EXISTS.
///
constexpr auto rename_replace_if_exists = DWORD{0x1};

///
/// FILE_RENAME_FLAG_POSIX_SEMANTICS.
///
constexpr auto rename_posix_semantics = DWORD{0x2};

///
/// Renames the file behind \p handle over \p target, replacing whatever was
/// there. Tries POSIX semantics first, so a reader holding the target open
/// with FILE_SHARE_DELETE does not block the replace; where they are not
/// available (Windows before 1607, FAT, some network shares), falls back to a
/// classic replace and the bounded retry above.
///
/// \param handle Open handle to the file to rename, held with DELETE access.
/// \param target The path to replace, absolute as transport_file makes it.
/// \return ERROR_SUCCESS, or the Win32 error of the last attempt.
///
[[nodiscard]] auto rename_over(const HANDLE handle, const std::filesystem::path& target) -> DWORD
{
  const auto& name = target.native();
  const auto name_bytes = name.size() * sizeof(wchar_t);
  const auto size = sizeof(rename_info) + name_bytes;

  // Assembled as bytes, header first and the name after it, so nothing is
  // written past the one-element array the struct declares. Zeroed, so the
  // name ends in a null either way.
  auto storage = std::make_unique<std::byte[]>(size);
  std::memcpy(storage.get() + offsetof(rename_info, file_name), name.c_str(), name_bytes);

  const auto attempt_rename = [&](const FILE_INFO_BY_HANDLE_CLASS kind, const DWORD flags) -> DWORD
  {
    const auto header = rename_info{flags, nullptr, static_cast<DWORD>(name_bytes), {}};
    std::memcpy(storage.get(), &header, offsetof(rename_info, file_name));

    return ::SetFileInformationByHandle(handle, kind, storage.get(), static_cast<DWORD>(size)) != 0
             ? DWORD{ERROR_SUCCESS}
             : ::GetLastError();
  };

  const auto posix = attempt_rename(file_rename_info_ex, rename_replace_if_exists | rename_posix_semantics);

  // Anything but the two ways "not here" is reported, older Windows and a
  // file system without POSIX semantics, is the answer: a foreign handle that
  // does not share deletion fails here at once, with no retry.
  if(posix != ERROR_INVALID_PARAMETER && posix != ERROR_NOT_SUPPORTED)
  {
    return posix;
  }

  auto error = DWORD{ERROR_SUCCESS};

  for(auto attempt = 0; attempt < transient_attempts; ++attempt)
  {
    error = attempt_rename(FileRenameInfo, rename_replace_if_exists);

    if(error != ERROR_ACCESS_DENIED && error != ERROR_SHARING_VIOLATION)
    {
      return error;
    }

    std::this_thread::sleep_for(transient_pause);
  }

  return error;
}

///
/// The Windows tail of write_file, after link resolution: writes the bytes to
/// a temporary sibling and renames that same handle over target, so a reader
/// sharing the target's deletion keeps reading the old file whole.
///
auto replace_file(
  const std::filesystem::path& target,
  const std::filesystem::path& temporary,
  const std::span<const std::byte> bytes,
  const std::source_location loc) -> void
{
  // DELETE, because the rename goes through this handle. The temporary's
  // random name is held by nobody else, so no sharing is granted.
  const auto file = win_handle{
    ::CreateFileW(temporary.c_str(), GENERIC_WRITE | DELETE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr)};

  if(!file.valid())
  {
    throw exception_t{
      std::format("arude::config: cannot open '{}' for writing: {}", temporary.string(), win_message(::GetLastError())),
      config_error::io_error,
      loc};
  }

  // No copy of the old file's permissions: std::filesystem::permissions only
  // toggles the read-only bit on Windows, which a replace refuses anyway, and
  // the new file inherits the directory's ACL exactly as MoveFileExW leaves
  // it.
  //
  // Removes the temporary through the handle that made it, so no other holder
  // can get in the way; called on every failure below.
  const auto discard = [&file]() -> void
  {
    const auto disposition = FILE_DISPOSITION_INFO{TRUE};
    ::SetFileInformationByHandle(file.get(), FileDispositionInfo, &disposition, sizeof(disposition));
  };

  for(auto rest = bytes; !rest.empty();)
  {
    // Chunked, so a buffer over 4 GiB does not truncate in the DWORD.
    const auto request = static_cast<DWORD>(std::min<std::size_t>(rest.size(), 1U << 30U));
    auto written = DWORD{0};

    if(::WriteFile(file.get(), rest.data(), request, &written, nullptr) == 0)
    {
      const auto error = ::GetLastError();
      discard();
      throw exception_t{
        std::format("arude::config: failed while writing '{}': {}", temporary.string(), win_message(error)),
        config_error::io_error,
        loc};
    }

    rest = rest.subspan(written);
  }

  if(const auto error = rename_over(file.get(), target); error != ERROR_SUCCESS)
  {
    discard();

    // ERROR_ACCESS_DENIED here means a handle that does not share deletion,
    // or a read-only target; the POSIX path does not retry, so a foreign
    // holder still open reports at once.
    throw exception_t{
      std::format("arude::config: cannot replace '{}': {}", target.string(), win_message(error)),
      config_error::io_error,
      loc};
  }
}

#else

///
/// The non-Windows tail of write_file, after link resolution: writes the bytes
/// to a temporary sibling and renames it over target. A POSIX rename replaces
/// atomically even while another handle reads the target, so it is tried once.
///
auto replace_file(
  const std::filesystem::path& target,
  const std::filesystem::path& temporary,
  const std::span<const std::byte> bytes,
  const std::source_location loc) -> void
{
  auto stream = std::ofstream{temporary, std::ios::binary | std::ios::trunc};

  if(!stream.is_open())
  {
    throw exception_t{
      std::format("arude::config: cannot open '{}' for writing.", temporary.string()), config_error::io_error, loc};
  }

  // Before anything is written, so contents the old file kept private are not
  // readable by others in the meantime. Best effort: where the old permissions
  // cannot be read, or there is no old file, the new one keeps the defaults it
  // was created with.
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

#endif // #if (defined _WIN32)

} // namespace

///
///
auto read_file(const std::filesystem::path& path, const std::source_location loc)
  -> std::optional<std::vector<std::byte>>
{
#if (defined _WIN32)

  // FILE_SHARE_DELETE, which no std::ifstream on Windows grants, lets a store
  // replace the file while this reads it; this then reads the old file whole.
  const auto file = win_handle{::CreateFileW(
    path.c_str(),
    GENERIC_READ,
    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
    nullptr,
    OPEN_EXISTING,
    FILE_ATTRIBUTE_NORMAL,
    nullptr)};

  if(!file.valid())
  {
    const auto error = ::GetLastError();

    if(error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
    {
      return std::nullopt;
    }

    // A directory cannot be opened without FILE_FLAG_BACKUP_SEMANTICS and
    // lands here as ERROR_ACCESS_DENIED; report it for what it is.
    auto unexamined = std::error_code{};

    if(std::filesystem::is_directory(path, unexamined))
    {
      throw exception_t{
        std::format("arude::config: cannot open '{}' for reading: it is a directory.", path.string()),
        config_error::io_error,
        loc};
    }

    throw exception_t{
      std::format("arude::config: cannot open '{}' for reading: {}", path.string(), win_message(error)),
      config_error::io_error,
      loc};
  }

  auto bytes = std::vector<std::byte>{};
  auto chunk = std::array<std::byte, 64 * 1024>{};

  for(;;)
  {
    auto read = DWORD{0};

    if(::ReadFile(file.get(), chunk.data(), static_cast<DWORD>(chunk.size()), &read, nullptr) == 0)
    {
      throw exception_t{
        std::format("arude::config: failed while reading '{}': {}", path.string(), win_message(::GetLastError())),
        config_error::io_error,
        loc};
    }

    if(read == 0)
    {
      return bytes;
    }

    bytes.insert(bytes.end(), chunk.begin(), chunk.begin() + read);
  }

#else

  auto stream = std::ifstream{path, std::ios::binary};

  if(!stream.is_open())
  {
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
    // depends on the standard library: libstdc++ throws std::ios_base::failure
    // from the first extraction, while libc++ reads nothing and reports the
    // end of the file. So nothing read is checked against the path, rather
    // than taken for an empty file.
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

#endif // #if (defined _WIN32)
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
    replace_file(target, temporary, bytes, loc);
  }
  catch(...)
  {
    // Whatever failed, the temporary file is not to be left lying beside the
    // configuration. Nothing to report if it was never created.
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
