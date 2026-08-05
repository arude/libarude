///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
#pragma once

namespace arude
{

///
/// Return the value it was given.
/// Placeholder that exercises the build, test, and formatting pipeline end to
/// end. It has no other purpose and will be removed once real code lands.
///
/// \param value Value to return.
/// \return The same value, unchanged.
///
[[nodiscard]] auto hello_world(int value) -> int;

} // namespace arude
