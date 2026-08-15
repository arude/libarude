///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///

#include "test/helpers/temp_directory.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace test_helpers
{

///
///
temp_directory::temp_directory(const std::string_view name)
  : path_{std::filesystem::temp_directory_path() / std::string{name}}
{
  std::filesystem::remove_all(path_);
  std::filesystem::create_directories(path_);
}

///
///
temp_directory::~temp_directory()
{
  // A destructor throws nothing, and a leftover directory under the temporary
  // directory is not worth reporting.
  auto error = std::error_code{};
  std::filesystem::remove_all(path_, error);
}

///
///
auto temp_directory::path() const -> const std::filesystem::path&
{
  return path_;
}

///
///
auto temp_directory::file(const std::string_view name) const -> std::filesystem::path
{
  return path_ / std::string{name};
}

} // namespace test_helpers
