///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///

#include "arude/config/toml_reader.hpp"

#include <toml++/toml.hpp>

#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace arude::config::detail
{

///
///
read_context::read_context(const load_policy policy)
  : policy_{policy}
{
}

///
///
auto read_context::policy() const -> load_policy
{
  return policy_;
}

///
///
auto read_context::fail(const config_error code, std::string message) -> std::string
{
  failures_.push_back(failure{.code = code, .message = message});

  return message;
}

///
///
auto read_context::code_for(const std::string_view final_error) const -> config_error
{
  for(const auto& recorded : failures_)
  {
    if(final_error.find(recorded.message) != std::string_view::npos)
    {
      return recorded.code;
    }
  }

  return config_error::invalid_payload;
}

///
///
auto read_context::note_newer(std::string message) -> void
{
  if(!newer_.has_value())
  {
    newer_ = std::move(message);
  }
}

///
///
auto read_context::newer() const -> const std::optional<std::string>&
{
  return newer_;
}

///
///
toml_reader::toml_reader(read_context& context)
  : context_{&context}
{
}

///
///
auto toml_reader::context() const -> read_context&
{
  return *context_;
}

///
///
auto integer_of(const toml::node& node) -> std::expected<std::int64_t, std::string>
{
  const auto* const integer = node.as_integer();

  if(integer == nullptr)
  {
    return std::unexpected{std::string{"the value is not an integer."}};
  }

  return integer->get();
}

} // namespace arude::config::detail
