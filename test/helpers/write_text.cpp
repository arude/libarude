///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///

#include "test/helpers/write_text.hpp"

#include <filesystem>
#include <fstream>
#include <ios>
#include <string_view>

namespace test_helpers
{

///
///
auto write_text(const std::filesystem::path& path, const std::string_view text) -> void
{
  auto stream = std::ofstream{path, std::ios::binary | std::ios::trunc};
  stream.write(text.data(), static_cast<std::streamsize>(text.size()));
}

} // namespace test_helpers
