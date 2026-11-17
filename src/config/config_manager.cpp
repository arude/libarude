///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///

#include "arude/config/config_manager.hpp"
#include "arude/config/document_base.hpp"
#include "arude/config/load_policy.hpp"

#include <cstddef>
#include <memory>
#include <shared_mutex>
#include <source_location>
#include <string>
#include <string_view>
#include <vector>

namespace arude::config
{

///
///
auto config_manager::contains(const std::string_view location) const -> bool
{
  const auto lock = std::shared_lock{mutex_};

  return documents_.contains(std::string{location});
}

///
///
auto config_manager::size() const -> std::size_t
{
  const auto lock = std::shared_lock{mutex_};

  return documents_.size();
}

///
///
auto config_manager::load_all(const load_policy policy, const std::source_location loc) -> void
{
  std::vector<std::shared_ptr<detail::document_base>> documents;

  {
    const auto lock = std::shared_lock{mutex_};

    for(const auto& [location, document] : documents_)
    {
      documents.push_back(document);
    }
  }

  for(const auto& document : documents)
  {
    document->load(policy, loc);
  }
}

} // namespace arude::config
