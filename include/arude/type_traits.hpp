///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// Small type traits and alias templates that the standard library does not
/// carry.
///
#pragma once

namespace arude
{

///
/// Names T, making the name formally dependent on TDependency.
/// The alias yields T untouched and never looks at TDependency. Its only job is
/// to put a template parameter into the spelling of a type, which is what makes
/// the whole name dependent: the compiler then defers looking it up, and
/// checking what is done with it, to the point of instantiation. Nothing has to
/// be done with TDependency for that — a template parameter appearing as a
/// template argument is enough.
///
/// The use is a class template that has to name, or reach through, a type that
/// is still incomplete where the template is parsed and complete by the time it
/// is instantiated:
///
/// \code
/// class widget; // Defined further down, after the template below.
///
/// template<typename T>
/// class holder final
/// {
/// private:
///   std::shared_ptr<dependent_t<T, widget>> widget_; // Checked on instantiation.
/// };
/// \endcode
///
/// \tparam TDependency Type the name is made to depend on. Unused, and need not be complete.
/// \tparam T Type named. Yielded unchanged, and need not be complete.
///
template<typename TDependency, typename T>
using dependent_t = T;

} // namespace arude
