///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///

#include "test/helpers/temp_file.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace test_helpers
{

///
///
temp_file::temp_file(const std::string_view name)
  : path_{std::filesystem::temp_directory_path() / std::string{name}}
{
}

///
///
temp_file::~temp_file()
{
  // A destructor throws nothing, and a leftover file in the temporary
  // directory is not worth reporting anyway.
  auto error = std::error_code{};
  std::filesystem::remove(path_, error);
}

///
///
auto temp_file::path() const -> const std::filesystem::path&
{
  return path_;
}

} // namespace test_helpers
