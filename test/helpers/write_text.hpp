///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
#pragma once

#include <filesystem>
#include <string_view>

namespace test_helpers
{

///
/// Writes text to a file, replacing whatever was there.
/// For the cases that need a file no writer of ours produces: one holding the
/// wrong version, or text that is not the format at all. Failure is silent,
/// because a test that then reads the file back reports it better than a throw
/// from the arrangement would.
///
/// \param path File to write.
/// \param text Text to write. Not retained.
///
auto write_text(const std::filesystem::path& path, std::string_view text) -> void;

} // namespace test_helpers
