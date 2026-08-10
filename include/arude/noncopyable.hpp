///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
#pragma once

namespace arude
{

// NOLINTNEXTLINE(readability-identifier-naming): the suffix is what keeps the name out of a client's ADL set.
namespace non_copyable_ // Protection from unintended ADL.
{

///
/// Not copyable base class.
///
/// Just inheriting from this class makes the derived class non-copyable.
/// The provided ctors/dtors are protected so they can only be used by derived classes.
///
/// Unlike boost::noncopyable, a derived class comes out move-only rather than
/// immovable. boost::noncopyable deletes the copy members and says nothing
/// about the move ones, and a user-declared copy ctor is enough to stop the
/// compiler declaring them, so a class deriving from it is neither copyable nor
/// movable. Defaulting the move members below is what puts them back.
///
/// \note A derived class that declares its own dtor loses the moves all the
/// same, because a user-declared dtor suppresses the implicit move members.
/// Overload resolution then finds the deleted copy members and the class ends
/// up immovable after all. A derived class needing both a dtor and the moves
/// has to default the two move members itself.
///
class noncopyable
{
protected: // Structors
  ///
  /// Default Ctor.
  /// This is provided to be called by a derived default ctor.
  ///
  constexpr noncopyable() = default;

  ///
  /// Default Dtor.
  /// This is provided to be called by a derived default dtor.
  ///
  constexpr ~noncopyable() noexcept = default;

  ///
  /// Copy ctor.
  /// Deleted to prevent copying.
  ///
  // NOLINTNEXTLINE(modernize-use-equals-delete): every member here is protected by design, the deleted ones too.
  constexpr noncopyable(const noncopyable&) = delete;

  ///
  /// Move ctor.
  ///
  constexpr noncopyable(noncopyable&&) noexcept = default;

protected: // Operators
  ///
  /// Copy operator.
  /// Deleted to prevent copying.
  ///
  // NOLINTNEXTLINE(modernize-use-equals-delete): every member here is protected by design, the deleted ones too.
  constexpr auto operator=(const noncopyable&) -> noncopyable& = delete;

  ///
  /// Move operator.
  ///
  constexpr auto operator=(noncopyable&&) noexcept -> noncopyable& = default;
};

} // namespace non_copyable_

// NOLINTNEXTLINE(readability-identifier-naming): a _t suffix would rename the type consumers inherit from.
using noncopyable = non_copyable_::noncopyable;

} // namespace arude
