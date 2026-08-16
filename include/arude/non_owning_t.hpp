///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
#pragma once

#include <type_traits>

namespace arude
{

///
/// Very simple wrapper to indicate that the pointer is not owned
/// and so no cleanup needs to be done.
///
/// \tparam T Pointee type, which must not itself be a pointer.
///
template<typename T>
  requires(!std::is_pointer_v<T>)
using non_owning_t = T*;

} // namespace arude
