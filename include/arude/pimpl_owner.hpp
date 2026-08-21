///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
#pragma once

#include "arude/noncopyable.hpp"

#include <memory>
#include <type_traits>

namespace arude
{

// NOLINTNEXTLINE(readability-identifier-naming): the suffix is what keeps the name out of a client's ADL set.
namespace pimpl_owner_ // Protection from unintended ADL.
{

///
/// The deleter a consumer leaves alone, and what TDeleter defaults to: the
/// base destroys the implementation with the ordinary delete. The value could
/// as well be spelled void, but a name carries the intent, and only this tag
/// makes the default_delete requirement on the ctor expressible.
///
struct delete_impl_tag final
{};

///
/// Storage for a class that hides its implementation behind a pimpl.
/// A class deriving from this keeps its implementation in a nested impl_t that
/// only its own translation unit ever sees. What is stored is a bare void*, so
/// an owner object is one pointer wide — the same as a hand-written pimpl.
/// Destroying the implementation is the ordinary delete, at no cost in
/// storage; an owner that frees some other way names a stateless deleter as
/// TDeleter, which is still nothing to store.
///
/// What the deriving class gets, and all it gets:
///
/// | Member | Purpose |
/// | ------ | ------- |
/// | pimpl_owner(std::unique_ptr) | Takes over the implementation. |
/// | impl() | The implementation, const for a const owner and mutable otherwise. |
/// | has_impl() | Whether an implementation is held, which only a moved-from owner is not. |
///
/// The table is here rather than a block on each declaration because the
/// declarations are private to TSelf and doxygen extracts no private member, so
/// a block there would reach nobody. arude/enum.hpp documents its re-exports in
/// its own description for the same kind of reason. The blocks are kept on the
/// declarations all the same, for whoever is reading the header.
///
/// The deriving class supplies its implementation type as a nested impl_t and
/// hands an instance to the ctor. Declaring impl_t in a public section, as
/// below, is what makes it accessible to this base; a class that would rather
/// not publish the name declares friend pimpl_owner; instead, naming the base's
/// injected-class-name, which docs/cpp-conventions.md places above the first
/// access specifier.
///
/// \code{.cpp}
/// // widget.hpp
/// class widget final : private arude::pimpl_owner<widget>
/// {
/// public: // Typedefs
///   class impl_t; // Defined in widget.cpp; incomplete everywhere else.
///   using my_size_t = std::size_t; // Implicitly visible to impl_t, which doens't have to be told about it.
///
///   widget();
///   ~widget(); // These three are declared here and defaulted in widget.cpp;
///   widget(widget&& other) noexcept;                      // see the note below.
///   auto operator=(widget&& other) noexcept -> widget&;
///
///   auto do_something() -> void;
///   [[nodiscard]] auto count() const -> my_size_t;
/// };
///
/// // widget.cpp
/// class widget::impl_t final
/// {
/// public:
///   auto trigger() -> void { ++counter_; }
///   [[nodiscard]] auto counter() const -> my_size_t { return counter_; }
///
/// private:
///   my_size_t counter_ = 0;
/// };
///
/// widget::widget()
///   : pimpl_owner{std::make_unique<impl_t>()}
/// {
/// }
///
/// widget::~widget() = default;
/// widget::widget(widget&& other) noexcept = default;
/// auto widget::operator=(widget&& other) noexcept -> widget& = default;
///
/// auto widget::do_something() -> void
/// {
///   impl().trigger();
/// }
///
/// auto widget::count() const -> my_size_t
/// {
///   return impl().counter();
/// }
/// \endcode
///
/// An owner that must free its implementation some other way — malloc'd
/// storage, an arena, a pool — names a stateless deleter instead:
///
/// \code{.cpp}
/// // widget.hpp
/// struct impl_deleter; // Declared first, since the base needs only its name.
///
/// class widget final : private arude::pimpl_owner<widget, impl_deleter>
/// {
/// public:
///   class impl_t; // Declared here; incomplete everywhere else.
///   ...
/// };
///
/// struct impl_deleter final
/// {
///   auto operator()(widget::impl_t* impl) const noexcept -> void; // Defined in widget.cpp.
/// };
/// ...
/// // widget.cpp
/// widget::widget()
///   : pimpl_owner{std::unique_ptr<impl_t, impl_deleter>{new impl_t}}
/// ...
/// \endcode
///
/// make_unique cannot build that pointer — its deleter is
/// std::default_delete, which is not impl_deleter — and the ctor will not
/// accept it, since freeing new'd storage through the wrong deleter is
/// undefined. The base adds no size either way: a stateless deleter is
/// nothing to store.
///
/// \tparam TSelf The deriving class, which must declare a nested impl_t
///         reachable from here. It is incomplete while this base is
///         instantiated and is only looked into when a member that needs it is
///         called, by which point the deriving class is complete. It is also
///         the only class that can use this one: everything here is private to
///         it, so a class that derives from pimpl_owner<widget> without being
///         widget gets a base it can neither build nor read.
/// \tparam TDeleter The type that destroys the implementation, defaulting to
///         delete_impl_tag, which means the ordinary delete. Any other value
///         must be an empty, stateless, default-constructible type callable
///         with an impl_t*, declared before the deriving class — which needs
///         only its name — and with the operator() defined where impl_t is
///         complete, like the dtor below. The base stores no deleter, so the
///         size of an owner never depends on it.
///
/// \note The deriving class must declare its dtor and both move members and
/// define them — `= default` is enough — in the translation unit that completes
/// impl_t. This is the price of the single pointer. Destroying the
/// implementation means deleting it as an impl_t, so the dtor below has to be
/// instantiated where impl_t is complete, and it is instantiated wherever the
/// deriving class's own dtor is. Left implicit, that would be some consumer's
/// translation unit, where impl_t is a name and nothing more: the destruction
/// would then free the storage without running the implementation's dtor. Compilers
/// warn about it — -Wdelete-incomplete under GCC and clang — but it is a
/// warning and not an error, so the rule is worth following rather than
/// discovering. Declaring the dtor is also what suppresses the implicit move
/// members, which is why all three go together.
///
/// \note Bases are initialised in declaration order, so impl() may be used in
/// the member-initialiser list of any base listed after this one. That is a
/// guarantee of the order, not of this class: listed before, the storage is
/// still empty and reading it is undefined.
///
/// \note const propagates through impl(), so a const member function of the
/// deriving class sees a const impl_t&. A hand-written std::unique_ptr<impl_t>
/// member does not do this — const applies to the pointer rather than to what
/// it points at — which is what std::experimental::propagate_const exists for.
/// It, the impl() accessor itself, and getting the move bookkeeping right once
/// rather than in every owner, are what this class is worth over the pimpl
/// written by hand; the boilerplate is much the same either way.
///
/// \note A moved-from owner is valid, destructible and assignable, and holds no
/// implementation at all. Calling impl() on one is undefined; has_impl() is
/// what answers the question.
///
/// \note Inheriting arude::noncopyable in addition to this class is redundant,
/// and costs the size of the second empty base, which cannot share an address
/// with the first.
///
/// \see arude::noncopyable
///
template<typename TSelf, typename TDeleter = delete_impl_tag>
// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions): the copy members come deleted from arude::noncopyable.
class pimpl_owner : public noncopyable
{
  // Everything below is private, with the deriving class named as a friend,
  // rather than protected. Protected members would let any class at all derive
  // from pimpl_owner<widget> — the parameter says which class this base belongs
  // to, and nothing about deriving checks it — and impl() would then hand that
  // interloper a widget's implementation, cast to whatever its own impl_t
  // happens to be. Naming TSelf is what ties the two together.
  friend TSelf;

private: // Structors
  ///
  /// Ctor taking ownership of the implementation object.
  ///
  /// \tparam TImpl Implementation type. Must be exactly TSelf::impl_t, since
  ///         impl() casts the stored void* back to that type and the round trip
  ///         is only defined for the type actually stored. An implementation
  ///         that has to vary at run time puts its virtual functions inside
  ///         impl_t rather than deriving from it.
  /// \tparam TGivenDeleter The deleter the pointer carries. Must be
  ///         std::default_delete<TImpl> when TDeleter is delete_impl_tag, and
  ///         exactly TDeleter otherwise; anything else is rejected here rather
  ///         than quietly freed the wrong way. The constraints carry it, and
  ///         not an assert in the body, so a caller can probe acceptance.
  /// \param pimpl The implementation object, which must not be null.
  ///
  template<typename TImpl, typename TGivenDeleter>
    requires std::same_as<typename std::unique_ptr<TImpl, TGivenDeleter>::pointer, TImpl*> &&
             std::same_as<
               TGivenDeleter,
               std::conditional_t<std::same_as<TDeleter, delete_impl_tag>, std::default_delete<TImpl>, TDeleter>> &&
             (std::same_as<TDeleter, delete_impl_tag> || std::is_empty_v<TDeleter>)
  explicit pimpl_owner(std::unique_ptr<TImpl, TGivenDeleter> pimpl);

  ///
  /// Dtor, which destroys the implementation.
  /// Not a template, and so instantiated wherever the deriving class's own dtor
  /// is: that is why the deriving class has to define its dtor where impl_t is
  /// complete. \see the note on the class.
  ///
  ~pimpl_owner() noexcept;

  ///
  /// Move ctor, which leaves other holding nothing.
  /// Written out rather than defaulted, since a defaulted one would copy the
  /// pointer and leave both objects believing they own it.
  ///
  pimpl_owner(pimpl_owner&& other) noexcept;

private: // Operators
  ///
  /// Move operator.
  /// Destroys the implementation held until now, if any, and takes over the one
  /// held by other, which is left empty. Self-assignment is a no-op.
  ///
  auto operator=(pimpl_owner&& other) noexcept -> pimpl_owner&;

private: // Methods
  ///
  /// Returns the implementation object.
  ///
  /// \tparam TDeferred Never named by a caller. It exists so that the lookup of
  ///         TSelf::impl_t happens when this function is called rather than
  ///         when the class is instantiated, which is the whole reason the
  ///         deriving class may still be incomplete at that point.
  /// \return The implementation object.
  /// \pre The owner holds an implementation, which is to say it has not been
  ///      moved from. Reading it otherwise is undefined.
  ///
  template<typename TDeferred = TSelf>
  [[nodiscard]] auto impl() -> typename TDeferred::impl_t&;

  ///
  /// Returns the implementation object, const.
  /// \see impl()
  ///
  /// \tparam TDeferred Never named by a caller. \see impl()
  /// \return The implementation object, const, so that const propagates from
  ///         the deriving class to its implementation.
  /// \pre The owner holds an implementation. \see impl()
  ///
  template<typename TDeferred = TSelf>
  [[nodiscard]] auto impl() const -> const typename TDeferred::impl_t&;

  ///
  /// Whether the owner holds an implementation.
  /// False only for an owner that has been moved from, since the ctor rejects
  /// nothing else. Needs no knowledge of impl_t and is safe to call at any
  /// point in the object's life.
  ///
  /// \return true if impl() may be called.
  ///
  [[nodiscard]] auto has_impl() const noexcept -> bool;

  ///
  /// Destroys the implementation and leaves the owner empty.
  /// The one place that destroys, and so the one place impl_t has to be
  /// complete.
  ///
  auto reset() noexcept -> void;

private: // Variables
  void* pimpl_ = nullptr;
};

///
///
template<typename TSelf, typename TDeleter>
template<typename TImpl, typename TGivenDeleter>
  requires std::same_as<typename std::unique_ptr<TImpl, TGivenDeleter>::pointer, TImpl*> &&
           std::same_as<
             TGivenDeleter,
             std::conditional_t<std::same_as<TDeleter, delete_impl_tag>, std::default_delete<TImpl>, TDeleter>> &&
           (std::same_as<TDeleter, delete_impl_tag> || std::is_empty_v<TDeleter>)
pimpl_owner<TSelf, TDeleter>::pimpl_owner(std::unique_ptr<TImpl, TGivenDeleter> pimpl)
  : pimpl_{pimpl.release()}
{
  // Checked here rather than as a constraint on the ctor, because TSelf is
  // incomplete until the deriving class calls this and a constraint would be
  // evaluated too early. The cast in impl() is what makes this a requirement:
  // a void* may only be converted back to the type it came from.
  static_assert(
    std::is_same_v<TImpl, typename TSelf::impl_t>,
    "arude::pimpl_owner: the object passed to the ctor must be of the deriving class's own impl_t.");
}

///
///
template<typename TSelf, typename TDeleter>
pimpl_owner<TSelf, TDeleter>::~pimpl_owner() noexcept
{
  reset();
}

///
///
template<typename TSelf, typename TDeleter>
pimpl_owner<TSelf, TDeleter>::pimpl_owner(pimpl_owner&& other) noexcept
  : pimpl_{other.pimpl_}
{
  other.pimpl_ = nullptr;
}

///
///
template<typename TSelf, typename TDeleter>
auto pimpl_owner<TSelf, TDeleter>::operator=(pimpl_owner&& other) noexcept -> pimpl_owner&
{
  if(this != &other)
  {
    reset();
    pimpl_ = other.pimpl_;
    other.pimpl_ = nullptr;
  }

  return *this;
}

///
///
template<typename TSelf, typename TDeleter>
template<typename TDeferred>
auto pimpl_owner<TSelf, TDeleter>::impl() -> typename TDeferred::impl_t&
{
  return *static_cast<typename TDeferred::impl_t*>(pimpl_);
}

///
///
template<typename TSelf, typename TDeleter>
template<typename TDeferred>
auto pimpl_owner<TSelf, TDeleter>::impl() const -> const typename TDeferred::impl_t&
{
  return *static_cast<const typename TDeferred::impl_t*>(pimpl_);
}

///
///
template<typename TSelf, typename TDeleter>
auto pimpl_owner<TSelf, TDeleter>::has_impl() const noexcept -> bool
{
  return pimpl_ != nullptr;
}

///
///
template<typename TSelf, typename TDeleter>
auto pimpl_owner<TSelf, TDeleter>::reset() noexcept -> void
{
  if(pimpl_ != nullptr)
  {
    auto* const impl = static_cast<typename TSelf::impl_t*>(pimpl_);

    if constexpr(std::is_same_v<TDeleter, delete_impl_tag>)
    {
      delete impl;
    }
    else
    {
      TDeleter{}(impl);
    }

    pimpl_ = nullptr;
  }
}

} // namespace pimpl_owner_

///
/// The name to inherit from: arude::pimpl_owner.
/// The class itself is declared one namespace deeper so that deriving from it
/// does not pull arude into the deriving class's ADL set, exactly as
/// arude::noncopyable is; this alias is what makes that arrangement invisible
/// to a consumer. It is an alias template rather than an alias, which is still
/// a base-specifier, still nameable in a friend declaration, and still leaves
/// the injected-class-name spelled pimpl_owner in a member-initialiser list.
///
/// \tparam TSelf The deriving class.
/// \tparam TDeleter The stateless type that destroys the implementation,
///         defaulting to the ordinary delete. \see the class it names.
///
/// \see arude::pimpl_owner_::pimpl_owner
///
template<typename TSelf, typename TDeleter = pimpl_owner_::delete_impl_tag>
// NOLINTNEXTLINE(readability-identifier-naming): a _t suffix would rename the type consumers inherit from.
using pimpl_owner = pimpl_owner_::pimpl_owner<TSelf, TDeleter>;

} // namespace arude
