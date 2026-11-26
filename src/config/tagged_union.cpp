///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///

#include "arude/config/tagged_union.hpp"

#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace arude::config::detail
{

///
///
auto tagged_union_not_table_message() -> std::string
{
  return std::string{"a tagged union is held in a table, and this is not one."};
}

///
///
auto missing_tag_message(const std::string_view discriminator, const bool present) -> std::string
{
  if(present)
  {
    return std::format("the tagged union's field '{}', which names its alternative, is not a string.", discriminator);
  }

  return std::format("the tagged union has no field '{}' to name its alternative.", discriminator);
}

///
///
auto unknown_tag_message(
  const std::string_view discriminator, const std::string_view tag, const std::vector<std::string>& known)
  -> std::string
{
  auto listed = std::string{};

  for(const auto& name : known)
  {
    listed += std::format("{}'{}'", listed.empty() ? "" : ", ", name);
  }

  return std::format(
    "the tagged union's {} '{}' names no alternative this build knows, as one a newer build added would; "
    "this build knows {}.",
    discriminator,
    tag,
    listed);
}

///
///
auto alternative_failed_message(
  const std::string_view discriminator, const std::string_view tag, const std::string_view reason) -> std::string
{
  return std::format("the tagged union's alternative with {} '{}' does not read: {}", discriminator, tag, reason);
}

} // namespace arude::config::detail
