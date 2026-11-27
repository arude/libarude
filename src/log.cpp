///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///

#include "arude/log.hpp"

#include <string>
#include <string_view>

namespace arude
{

///
///
// The by-value message is the interface talking, not this body: a sink owns
// the text it is handed, and the macro hands it over with a move rather than a
// copy. A body that drops it looks to clang-tidy like a copy nobody wanted, so
// the suppression stands until there is a sink to move it into. No top-level
// const on it either, for the same reason.
// NOLINTBEGIN(performance-unnecessary-value-param)
auto log(
  [[maybe_unused]] const log_level level,
  [[maybe_unused]] const std::string_view channel,
  [[maybe_unused]] std::string message) -> void
{
}
// NOLINTEND(performance-unnecessary-value-param)

///
///
auto log_global_log_level() -> log_level
{
  return log_level::info;
}

} // namespace arude
