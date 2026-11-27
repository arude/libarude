///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///

#include "arude/config/versioned.hpp"

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

namespace arude::config::detail
{

///
///
auto newer_message(const std::string_view type, const version_t found, const version_t newest) -> std::string
{
  return std::format("{} is version {}, newer than version {}, the newest this build knows.", type, found, newest);
}

///
///
auto older_refused_message(const std::string_view type, const version_t found, const version_t newest) -> std::string
{
  return std::format("{} is version {}, and version {} was asked for.", type, found, newest);
}

///
///
auto unknown_version_message(const std::string_view type, const version_t found) -> std::string
{
  return std::format("version {} is not a version of {} that this build knows.", found, type);
}

///
///
auto migration_failed_message(const std::string_view type, const version_t found, const version_t newest) -> std::string
{
  return std::format(
    "{} is version {} and cannot be migrated to version {}: {}", type, found, newest, nested_message());
}

} // namespace arude::config::detail
