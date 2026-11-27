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
#include <format>
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
auto without_prefix(const std::string_view message) -> std::string
{
  auto stripped = std::string{message};

  // Only where a message of ours begins: at the start, or after the ": "
  // with which a message quoting another one, ours or reflect-cpp's,
  // introduces it. Anywhere else the text is a path, a key or a value
  // somebody wrote, and is quoted as it is.
  static constexpr auto separator = std::string_view{": "};

  for(auto at = stripped.find(message_prefix); at != std::string::npos; at = stripped.find(message_prefix, at))
  {
    const auto starts_message =
      at == 0 || (at >= separator.size() &&
                  std::string_view{stripped}.substr(at - separator.size(), separator.size()) == separator);

    if(starts_message)
    {
      stripped.erase(at, message_prefix.size());
    }
    else
    {
      at += message_prefix.size();
    }
  }

  return stripped;
}

///
///
auto nested_message() -> std::string
{
  return without_prefix(exception_message());
}

///
///
auto rethrow_as(const config_error fallback, const std::string_view what, const std::source_location loc) -> void
{
  try
  {
    // Rethrows the exception being handled, whatever it is, so the handlers
    // below see it rather than a copy.
    throw;
  }
  catch(const exception_t&)
  {
    // An exception_t is what the caller reports; it is rethrown unchanged,
    // code and call site included.
    throw;
  }
  catch(...)
  {
    // Anything else, such as a transport's own std::exception, is not an
    // exception_t and would walk past every handler a caller has.
    throw exception_t{std::format("arude::config: {}: {}", what, nested_message()), fallback, loc};
  }
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
