///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///

#include "arude/config/common.hpp"
#include "arude/exception_report.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace arude::config::detail
{

// The other half of the check in arude/config/common.hpp: exactly one of the
// two is defined, the one matching the setting this library is built with.
#if (defined ARUDE_EXCEPTION_RUNTIME_ERROR_BASE)
const int library_built_with_exception_runtime_error_base = 0;
#else
const int library_built_without_exception_runtime_error_base = 0;
#endif // #if (defined ARUDE_EXCEPTION_RUNTIME_ERROR_BASE)

///
///
auto nested_message() -> std::string
{
  auto message = std::string{exception_message()};

  // Only where a message of ours begins: at the start, or after the ": " with
  // which a message quoting another one, ours or reflect-cpp's, introduces it.
  // Anywhere else the text is a path, a key or a value somebody wrote, and is
  // quoted as it is.
  static constexpr auto separator = std::string_view{": "};

  for(auto at = message.find(message_prefix); at != std::string::npos; at = message.find(message_prefix, at))
  {
    const auto starts_message =
      at == 0 || (at >= separator.size() &&
                  std::string_view{message}.substr(at - separator.size(), separator.size()) == separator);

    if(starts_message)
    {
      message.erase(at, message_prefix.size());
    }
    else
    {
      at += message_prefix.size();
    }
  }

  return message;
}

} // namespace arude::config::detail

namespace arude::config
{

///
///
auto as_text(const std::span<const std::byte> bytes) -> std::string
{
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  const auto* const begin = reinterpret_cast<const char*>(bytes.data());
  return {begin, bytes.size()};
}

///
///
auto as_bytes(const std::string_view text) -> std::vector<std::byte>
{
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  const auto* const begin = reinterpret_cast<const std::byte*>(text.data());
  return {begin, begin + text.size()};
}

} // namespace arude::config
