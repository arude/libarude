///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///

#include "arude/config/versioned.hpp"

#include <toml++/toml.hpp>

#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace arude::config::detail
{

///
///
hidden_version_key::hidden_version_key(toml::table& table)
  : table_{&table}
{
  if(const auto* const node = table.get(version_key); node != nullptr)
  {
    node->visit([this](const auto& value) -> void { saved_.insert_or_assign(version_key, value); });
    table.erase(version_key);
  }
}

///
///
hidden_version_key::~hidden_version_key()
{
  try
  {
    if(auto* const node = saved_.get(version_key); node != nullptr)
    {
      node->visit([this](auto& value) -> void { table_->insert_or_assign(version_key, std::move(value)); });
    }
  }
  catch(...) // NOLINT(bugprone-empty-catch): the key staying out is the handling.
  {
    // Only an allocation can fail here, and nothing escapes a destructor:
    // the key stays out, which a later read reports as a missing version.
  }
}

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
