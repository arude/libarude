///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///

#include "test/helpers/as_text.hpp"

#include <cstddef>
#include <span>
#include <string>

namespace test_helpers
{

///
///
auto as_text(const std::span<const std::byte> bytes) -> std::string
{
  // std::byte and char are both narrow character types and may alias
  // anything, so this reinterpret_cast is legal: the bytes are the text,
  // unchanged.
  const auto* const begin = reinterpret_cast<const char*>(bytes.data());

  return {begin, bytes.size()};
}

} // namespace test_helpers
