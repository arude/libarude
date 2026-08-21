///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// Owners for test/test_pimpl_owner.cpp, declared here and defined in
/// widget.cpp. The split is the point: what arude::pimpl_owner promises is that
/// a translation unit which sees only this header can create, use, move and
/// destroy these types while their impl_t is nothing but a name. Defining them
/// alongside the tests would compile just as well and prove none of it.
///
#pragma once

#include "arude/pimpl_owner.hpp"

#include <cstddef>

namespace pimpl_test
{

///
/// Number of implementation objects alive across every owner in this header.
/// \return The count, which the tests read as a delta rather than an absolute.
///
[[nodiscard]] auto impl_live_count() -> int;

///
/// Number of implementation objects constructed since the program started.
/// \return The count, which never decreases.
///
[[nodiscard]] auto impl_total_count() -> int;

///
/// Number of implementations freed through custom_deleter.
/// \return The count, which the tests read as a delta rather than an absolute.
///
[[nodiscard]] auto custom_delete_count() -> int;

///
/// A second base for relay_widget, standing in for the kind of collaborator
/// that has to be handed the implementation as it is built.
/// It keeps the address rather than the object, so it needs nothing of the
/// implementation type but its name.
///
class relay
{
public:
  ///
  /// Ctor recording which implementation it was handed.
  /// \tparam TImpl Implementation type, deduced and never named by a caller.
  /// \param impl The implementation, whose address is kept.
  ///
  template<typename TImpl>
  explicit relay(const TImpl& impl);

  ///
  /// The implementation this was handed.
  /// \return Its address, to be compared against the owner's own.
  ///
  [[nodiscard]] auto relayed() const -> const void*;

private: // Variables
  const void* impl_ = nullptr;
};

///
/// The reference owner, shaped exactly as the documentation on
/// arude::pimpl_owner describes.
/// The dtor and the two move members are declared here and defaulted in
/// widget.cpp, where impl_t is complete. Nothing else would run the
/// implementation's dtor.
///
// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions): the copy members come deleted from arude::noncopyable.
class widget final : private arude::pimpl_owner<widget>
{
public:
  class impl_t;

  ///
  /// Default ctor, which is where the implementation is allocated.
  ///
  widget();

  ///
  /// Dtor.
  ///
  ~widget();

  ///
  /// Move ctor.
  ///
  widget(widget&& other) noexcept;

  ///
  /// Move operator.
  ///
  auto operator=(widget&& other) noexcept -> widget&;

  ///
  /// Advances the implementation's counter.
  ///
  auto do_something() -> void;

  ///
  /// The implementation's counter.
  /// \return Number of do_something() calls.
  ///
  [[nodiscard]] auto count() const -> int;

  ///
  /// Reaches the implementation's non-const overload.
  /// \return 1, which only the non-const overload returns.
  ///
  [[nodiscard]] auto tag() -> int;

  ///
  /// Reaches the implementation's const overload.
  /// \return 2, which only the const overload returns.
  ///
  [[nodiscard]] auto tag() const -> int;

  ///
  /// Whether an implementation is held, which a moved-from owner reports false.
  /// \return What the protected has_impl() answers.
  ///
  [[nodiscard]] auto holds_impl() const -> bool;

  ///
  /// The address of the implementation.
  /// \return Its address, for comparison with what a collaborator was handed.
  /// \pre An implementation is held.
  ///
  [[nodiscard]] auto impl_address() const -> const void*;
};

///
/// A second, unrelated owner, so that two hidden implementations can be shown
/// to keep to themselves in one program.
/// Like every owner below, it declares a dtor and defines it in widget.cpp, and
/// so gives up the move members it never uses.
///
// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions): the copy members come deleted from arude::noncopyable.
class gadget final : private arude::pimpl_owner<gadget>
{
public:
  class impl_t;

  ///
  /// Ctor.
  /// \param seed Starting value of the implementation's counter.
  ///
  explicit gadget(int seed);

  ///
  /// Dtor.
  ///
  ~gadget();

  ///
  /// The implementation's value.
  /// \return The seed plus the number of bump() calls.
  ///
  [[nodiscard]] auto value() const -> int;

  ///
  /// Advances the implementation's value.
  ///
  auto bump() -> void;
};

///
/// An owner with a second base that is handed the implementation as it is
/// built, which is what the base declaration order below guarantees.
///
// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions): the copy members come deleted from arude::noncopyable.
class relay_widget final
  : private arude::pimpl_owner<relay_widget> // Initialised first.
  , public relay                             // Initialised second, from impl().
{
public:
  class impl_t;

  ///
  /// Default ctor, which hands impl() to the relay base.
  ///
  relay_widget();

  ///
  /// Dtor.
  ///
  ~relay_widget();

  ///
  /// The address of the implementation.
  /// \return Its address, which must be what relayed() reports.
  ///
  [[nodiscard]] auto impl_address() const -> const void*;
};

///
/// An owner whose implementation is over-aligned.
/// The delete happens through a static_cast from void* back to impl_t, so this
/// is what shows the aligned operator delete being reached all the same.
///
// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions): the copy members come deleted from arude::noncopyable.
class aligned_widget final : private arude::pimpl_owner<aligned_widget>
{
public:
  class impl_t;

  ///
  /// Default ctor.
  ///
  aligned_widget();

  ///
  /// Dtor.
  ///
  ~aligned_widget();

  ///
  /// Whether the implementation sits on its required boundary.
  /// \return true if the address is a multiple of impl_alignment().
  ///
  [[nodiscard]] auto impl_aligned() const -> bool;

  ///
  /// The implementation's alignment requirement.
  /// \return alignof(impl_t), which is wider than any pointer.
  ///
  [[nodiscard]] static auto impl_alignment() -> std::size_t;
};

///
/// An owner whose implementation throws while being constructed.
/// The base is never entered, so there is nothing for it to clean up.
///
// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions): the copy members come deleted from arude::noncopyable.
class throwing_widget final : private arude::pimpl_owner<throwing_widget>
{
public:
  class impl_t;

  ///
  /// Default ctor.
  /// \throws arude::exception Always, from the implementation's own ctor.
  ///
  throwing_widget();

  ///
  /// Dtor.
  ///
  ~throwing_widget();
};

///
/// An owner that throws after its implementation is built.
/// The base is fully constructed by then, so its dtor runs and the
/// implementation goes with it.
///
// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions): the copy members come deleted from arude::noncopyable.
class late_throwing_widget final : private arude::pimpl_owner<late_throwing_widget>
{
public:
  class impl_t;

  ///
  /// Default ctor.
  /// \throws arude::exception Always, from the ctor body.
  ///
  late_throwing_widget();

  ///
  /// Dtor.
  ///
  ~late_throwing_widget();
};

///
/// The custom deleter custom_deleter_widget hands to pimpl_owner.
/// Forward-declared here, since the base specifier needs only its name, and
/// defined after the owner, whose impl_t the operator() must name.
///
struct custom_deleter;

///
/// An owner that names a custom deleter, proving the base calls it rather than
/// the ordinary delete, and stays one pointer wide doing it.
///
// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions): the copy members come deleted from arude::noncopyable.
class custom_deleter_widget final : private arude::pimpl_owner<custom_deleter_widget, custom_deleter>
{
public:
  class impl_t;

  ///
  /// Default ctor.
  ///
  custom_deleter_widget();

  ///
  /// Dtor.
  ///
  ~custom_deleter_widget();

  ///
  /// Move ctor.
  ///
  custom_deleter_widget(custom_deleter_widget&& other) noexcept;

  ///
  /// Move operator.
  ///
  auto operator=(custom_deleter_widget&& other) noexcept -> custom_deleter_widget&;

  ///
  /// Whether an implementation is held, which a moved-from owner reports false.
  /// \return What the protected has_impl() answers.
  ///
  [[nodiscard]] auto holds_impl() const -> bool;
};

///
/// The deleter custom_deleter_widget hands to pimpl_owner.
/// Stateless, as TDeleter must be: the base stores nothing of it, which is why
/// it costs the owner no size. The operator() is defined in widget.cpp, where
/// impl_t is complete, like every other member that touches the implementation.
///
struct custom_deleter final
{
  ///
  /// Frees the implementation, counting the call as it goes.
  /// \param impl The implementation to free.
  ///
  auto operator()(custom_deleter_widget::impl_t* impl) const noexcept -> void;
};

///
///
template<typename TImpl>
relay::relay(const TImpl& impl)
  : impl_{&impl}
{
}

} // namespace pimpl_test
