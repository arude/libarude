///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
#pragma once

#include <cstddef>
#include <span>
#include <string>

namespace test_helpers
{

///
/// Interprets bytes as text.
/// Transports deal in bytes, so every payload assertion would otherwise carry
/// its own conversion; this is the one shared spelling.
///
/// \param bytes Bytes to interpret. Not retained.
/// \return The text.
///
[[nodiscard]] auto as_text(std::span<const std::byte> bytes) -> std::string;

} // namespace test_helpers
