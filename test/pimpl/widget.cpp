///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// The one translation unit that completes the implementation types declared in
/// widget.hpp. Everything that needs to know what an impl_t is lives here.
///

#include "test/pimpl/widget.hpp"

#include "arude/exception.hpp"
#include "arude/noncopyable.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>

namespace pimpl_test
{

namespace
{

///
/// Live and total counts of every implementation object in this file.
/// One struct handed out by an accessor rather than two variables at namespace
/// scope, which docs/cpp-conventions.md asks to avoid.
///
struct impl_counters final
{
  int live = 0;
  int total = 0;
  int custom_deletes = 0;
};

///
/// The counters.
/// \return The single instance, maintained by the counted base below.
///
[[nodiscard]] auto counters() -> impl_counters&;

///
/// Bookkeeping base for every implementation type in this file.
/// Empty and non-virtual, so it costs an implementation nothing but the
/// counting itself, and an over-aligned derived type keeps its alignment.
///
// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions): the copy members come deleted from arude::noncopyable.
class counted : public arude::noncopyable
{
protected: // Structors
  counted();
  ~counted();
};

///
///
auto counters() -> impl_counters&
{
  static auto instance = impl_counters{};

  return instance;
}

///
///
counted::counted()
{
  ++counters().live;
  ++counters().total;
}

///
///
counted::~counted()
{
  --counters().live;
}

} // namespace

///
/// widget's implementation.
/// The two tag() overloads differ only in constness and return different
/// values, which is how a test tells which one a const owner reached.
///
class widget::impl_t final : public counted
{
public:
  auto trigger() -> void;

  [[nodiscard]] auto counter() const -> int;

  [[nodiscard]] auto tag() -> int;

  [[nodiscard]] auto tag() const -> int;

private: // Variables
  int counter_ = 0;
  int tag_ = 1;
};

///
/// gadget's implementation, unrelated to widget's.
///
class gadget::impl_t final : public counted
{
public:
  explicit impl_t(int seed);

  [[nodiscard]] auto value() const -> int;

  auto bump() -> void;

private: // Variables
  int value_ = 0;
};

///
/// relay_widget's implementation, which carries nothing: only its address is
/// interesting, and only to show the relay base was handed the right one.
///
class relay_widget::impl_t final : public counted
{};

///
/// aligned_widget's implementation.
/// The alignment is wider than any pointer, so a plain operator new could not
/// have produced a suitable address by luck.
///
class alignas(64) aligned_widget::impl_t final : public counted
{};

///
/// throwing_widget's implementation, which never finishes being built.
///
class throwing_widget::impl_t final : public counted
{
public:
  impl_t();
};

///
/// late_throwing_widget's implementation, which does.
///
class late_throwing_widget::impl_t final : public counted
{};

///
/// custom_deleter_widget's implementation, freed by custom_deleter.
///
class custom_deleter_widget::impl_t final : public counted
{};

///
///
auto impl_live_count() -> int
{
  return counters().live;
}

///
///
auto impl_total_count() -> int
{
  return counters().total;
}

///
///
auto custom_delete_count() -> int
{
  return counters().custom_deletes;
}

///
///
auto relay::relayed() const -> const void*
{
  return impl_;
}

///
///
auto widget::impl_t::trigger() -> void
{
  ++counter_;
}

///
///
auto widget::impl_t::counter() const -> int
{
  return counter_;
}

///
///
// NOLINTNEXTLINE(readability-make-member-function-const): which overload was reached is the whole point.
auto widget::impl_t::tag() -> int
{
  return tag_;
}

///
///
auto widget::impl_t::tag() const -> int
{
  return tag_ + 1;
}

///
///
widget::widget()
  : pimpl_owner{std::make_unique<impl_t>()}
{
}

///
///
widget::~widget() = default;

///
///
widget::widget(widget&& other) noexcept = default;

///
///
auto widget::operator=(widget&& other) noexcept -> widget& = default;

///
///
auto widget::do_something() -> void
{
  static_assert(std::is_same_v<decltype(impl()), impl_t&>, "A mutable owner must reach a mutable implementation.");

  impl().trigger();
}

///
///
auto widget::count() const -> int
{
  // The const overload of impl() is what carries constness across to the
  // implementation. A hand-written std::unique_ptr<impl_t> member would hand
  // out a mutable implementation here, since the const would apply to the
  // pointer rather than to what it points at.
  static_assert(std::is_same_v<decltype(impl()), const impl_t&>, "const must propagate to the implementation.");

  return impl().counter();
}

///
///
auto widget::tag() -> int
{
  return impl().tag();
}

///
///
auto widget::tag() const -> int
{
  return impl().tag();
}

///
///
auto widget::holds_impl() const -> bool
{
  return has_impl();
}

///
///
auto widget::impl_address() const -> const void*
{
  return &impl();
}

///
///
gadget::impl_t::impl_t(const int seed)
  : value_{seed}
{
}

///
///
auto gadget::impl_t::value() const -> int
{
  return value_;
}

///
///
auto gadget::impl_t::bump() -> void
{
  ++value_;
}

///
///
gadget::gadget(const int seed)
  : pimpl_owner{std::make_unique<impl_t>(seed)}
{
}

///
///
gadget::~gadget() = default;

///
///
auto gadget::value() const -> int
{
  return impl().value();
}

///
///
auto gadget::bump() -> void
{
  impl().bump();
}

///
///
relay_widget::relay_widget()
  : pimpl_owner{std::make_unique<impl_t>()}
  , relay{impl()} // Legal because pimpl_owner is listed first and so is already built.
{
}

///
///
relay_widget::~relay_widget() = default;

///
///
auto relay_widget::impl_address() const -> const void*
{
  return &impl();
}

///
///
aligned_widget::aligned_widget()
  : pimpl_owner{std::make_unique<impl_t>()}
{
}

///
///
aligned_widget::~aligned_widget() = default;

///
///
auto aligned_widget::impl_aligned() const -> bool
{
  // std::bit_cast rather than reinterpret_cast: what is wanted is the value of
  // the pointer as a number, not a reinterpretation of the object it points at,
  // and the former needs no account of the aliasing rules.
  const auto address = std::bit_cast<std::uintptr_t>(static_cast<const void*>(&impl()));

  return address % alignof(impl_t) == 0;
}

///
///
auto aligned_widget::impl_alignment() -> std::size_t
{
  return alignof(impl_t);
}

///
///
throwing_widget::impl_t::impl_t()
{
  // The counted base has already run, and unwinding this ctor destroys it
  // again, so a leak here would show up as a live count that never came back
  // down.
  throw arude::exception{"pimpl_test::throwing_widget: the implementation failed to build."};
}

///
///
throwing_widget::throwing_widget()
  : pimpl_owner{std::make_unique<impl_t>()}
{
}

///
///
throwing_widget::~throwing_widget() = default;

///
///
late_throwing_widget::late_throwing_widget()
  : pimpl_owner{std::make_unique<impl_t>()}
{
  // The base is fully constructed by now, so unwinding runs its dtor and the
  // implementation goes with it. Nothing here has to clean up by hand.
  throw arude::exception{"pimpl_test::late_throwing_widget: failed after the implementation was built."};
}

///
///
late_throwing_widget::~late_throwing_widget() = default;

///
///
auto custom_deleter::operator()(custom_deleter_widget::impl_t* const impl) const noexcept -> void
{
  // The count is what proves pimpl_owner came here rather than taking the
  // ordinary delete, which would free the storage without ever counting.
  ++counters().custom_deletes;
  delete impl;
}

///
///
custom_deleter_widget::custom_deleter_widget()
  // make_unique builds a unique_ptr carrying std::default_delete, which the
  // base must reject: freeing through the wrong deleter is undefined. Spelling
  // custom_deleter out is the price of the custom one.
  : pimpl_owner{std::unique_ptr<impl_t, custom_deleter>{new impl_t}}
{
}

///
///
custom_deleter_widget::~custom_deleter_widget() = default;

///
///
custom_deleter_widget::custom_deleter_widget(custom_deleter_widget&& other) noexcept = default;

///
///
auto custom_deleter_widget::operator=(custom_deleter_widget&& other) noexcept -> custom_deleter_widget& = default;

///
///
auto custom_deleter_widget::holds_impl() const -> bool
{
  return has_impl();
}

} // namespace pimpl_test
