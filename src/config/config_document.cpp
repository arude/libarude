///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///

#include "arude/config/config_document.hpp"
#include "arude/config/document_base.hpp"
#include "arude/config/load_policy.hpp"

#include <format>
#include <memory>
#include <source_location>
#include <string>
#include <string_view>
#include <utility>

namespace arude::config::detail
{

///
///
auto section_label(const std::string_view name) -> std::string
{
  return name.empty() ? std::string{"the root section"} : std::format("section '{}'", name);
}

} // namespace arude::config::detail

namespace arude::config
{

///
///
config_document_handle::config_document_handle(std::shared_ptr<detail::document_base> document)
  : document_{std::move(document)}
{
}

///
///
auto config_document_handle::location() const -> std::string
{
  return document_->location();
}

///
///
auto config_document_handle::writable() const -> bool
{
  return document_->writable();
}

///
///
auto config_document_handle::load(const load_policy policy, const std::source_location loc) -> void
{
  document_->load(policy, loc);
}

///
///
auto config_document_handle::store(const std::source_location loc) -> void
{
  document_->store(loc);
}

///
///
auto config_document_handle::discard_source() -> void
{
  document_->discard_source();
}

} // namespace arude::config
