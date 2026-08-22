///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
#pragma once

///
/// Umbrella header pulling in the whole of libarude.
/// Convenient for consumers that want everything; prefer including the
/// individual headers where compile time matters.
///

#include "arude/enum.hpp"
#include "arude/exception.hpp"
#include "arude/exception_report.hpp"
#include "arude/hello_world.hpp"
#include "arude/noncopyable.hpp"
#include "arude/pimpl_owner.hpp"
#include "arude/signal_owner.hpp"
#include "arude/type_name.hpp"

///
/// Root namespace for everything libarude provides.
/// Documented here so Doxygen produces a namespace index; without it the API
/// is only reachable through the per-file pages.
///
namespace arude
{

}
