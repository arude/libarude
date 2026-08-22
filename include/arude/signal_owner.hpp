///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
#pragma once

#include <concepts>
#include <type_traits>
#include <utility>

namespace arude
{

namespace detail
{

///
/// Whether a type can be built from a pack of arguments with braces.
/// std::constructible_from answers for parentheses, which of an aggregate asks
/// a different question: parentheses permit a narrowing conversion where braces
/// reject it, so a pack it accepted could still fail inside the ctor that has
/// to do the work rather than be turned away at it.
///
/// \tparam T The type to build.
/// \tparam TArgs The arguments to build it from.
///
template<typename T, typename... TArgs>
concept brace_constructible_c = requires(TArgs&&... args) { T{std::forward<TArgs>(args)...}; };

} // namespace detail

// NOLINTNEXTLINE(readability-identifier-naming): the suffix is what keeps the name out of a client's ADL set.
namespace signal_owner_ // Protection from unintended ADL.
{

///
/// The base every signal owner shares, carrying nothing at all.
/// It exists so that a relay can hold a pointer to an owner without naming the
/// bundle that owner keeps, which is a type it cannot look up where the pointer
/// is declared. \see arude::signal_relay, which is the only thing that ever
/// holds one.
///
/// A consumer has no reason to name it anywhere else. It is not an interface:
/// it declares nothing but its structors, and every use of it goes through the
/// downcast back to arude::signal_owner.
///
/// \note Deriving from this directly makes a class no kind of owner. A relay is
/// bound to an arude::signal_owner and refuses whatever it could not cast back
/// to one, which is what keeps that downcast defined.
///
/// \note The dtor is protected, so no owner can be deleted through a pointer to
/// this base. Nothing here is virtual, and such a delete would run no dtor at
/// all rather than the wrong one.
///
class signal_owner_base
{
protected: // Structors
  ///
  /// Default ctor.
  /// This is provided to be called by a derived default ctor.
  ///
  constexpr signal_owner_base() = default;

  ///
  /// Default dtor.
  /// This is provided to be called by a derived dtor, and is protected so that
  /// it can be called by nothing else.
  ///
  constexpr ~signal_owner_base() noexcept = default;

  ///
  /// Copy ctor.
  /// Defaulted rather than deleted: whether an owner may be copied is a
  /// question about the bundle it keeps, and an empty base is in no position to
  /// answer it.
  ///
  constexpr signal_owner_base(const signal_owner_base&) = default;

  ///
  /// Move ctor.
  ///
  constexpr signal_owner_base(signal_owner_base&&) noexcept = default;

protected: // Operators
  ///
  /// Copy operator.
  /// \see the copy ctor for why this is not deleted.
  ///
  constexpr auto operator=(const signal_owner_base&) -> signal_owner_base& = default;

  ///
  /// Move operator.
  ///
  constexpr auto operator=(signal_owner_base&&) noexcept -> signal_owner_base& = default;
};

///
/// Storage for the signals a class announces through.
/// A class deriving from this keeps a bundle of signals as a member and hands
/// it out through sigs(), which is what a consumer connects to. What the bundle
/// is made of is the consumer's business entirely: libarude supplies no signal
/// type and never looks inside T.
///
/// The member is not the point; a bundle declared by hand costs one line. The
/// base that comes with it is: arude::signal_relay finds a bundle through that
/// base, so a collaborator can announce through its owner's signals while
/// naming neither the owner's type nor, where it is declared, the bundle's.
///
/// \code{.cpp}
/// struct widget_sigs final // The bundle, of whatever signal type the consumer uses.
/// {
///   my::signal<void(int)> value_changed;
/// };
///
/// class widget final : public arude::signal_owner<widget_sigs>
/// {
/// public:
///   auto value(int value) -> void;
///
/// private:
///   int value_ = 0;
/// };
///
/// auto widget::value(const int value) -> void
/// {
///   value_ = value;
///   sigs().value_changed(value); // Announced through the bundle the base keeps.
/// }
/// \endcode
///
/// \tparam T The bundle, which is constructed and destroyed with the owner.
///         Whether an owner can be copied or moved is whatever T answers: no
///         copy, move or dtor is declared here, so the rule of zero decides.
///
/// \note The bundle is value initialised where the owner was built from
/// nothing, so one that is an aggregate of plain members starts out zeroed
/// rather than indeterminate. A bundle that has to be told something, or that
/// cannot be default constructed at all, is built through the ctor taking
/// arguments instead.
///
/// \note Unlike arude::signal_relay, this class says nothing about who may
/// derive from it. Its parameter is the bundle rather than the deriving class,
/// and two unrelated classes announcing the same bundle is a use rather than a
/// mistake.
///
/// \see arude::signal_relay for the other half.
///
template<typename T>
class signal_owner : public signal_owner_base
{
public: // Typedefs
  ///
  /// The bundle, published so that a relay can ask an owner what it announces.
  ///
  using sigs_t = T;

public: // Structors
  ///
  /// Default ctor, which leaves the bundle value initialised.
  /// Declared because the ctor below is one: a user-declared ctor of any shape
  /// suppresses the implicit default ctor, and running the bundle's default
  /// member initialiser is what this is here for.
  ///
  /// The constraint is not decoration. Without it, asking whether an owner is
  /// default constructible instantiates that member initialiser in order to
  /// work out this ctor's exception specification, and for a bundle that cannot
  /// be default constructed clang makes the answer an error rather than a no.
  /// GCC reports false and carries on, so left off, this would be a divergence
  /// for a consumer to run into rather than us.
  ///
  signal_owner()
    requires std::default_initializable<T>
  = default;

  ///
  /// Ctor building the bundle from the arguments given.
  /// The bundle is private, so this is the only way a deriving class can start
  /// it as anything but value initialised — and the only way at all to own a
  /// bundle that cannot be default constructed.
  ///
  /// \tparam TArgs The bundle's own ctor arguments, deduced and forwarded. They
  ///         are braced rather than parenthesised, so an aggregate bundle is
  ///         built member by member and a narrowing conversion is turned away
  ///         here rather than performed. The second constraint is what keeps
  ///         this ctor clear of the copy and move ctors: handed another owner
  ///         it would otherwise be the better match for a non-const one, and
  ///         would go on to build a bundle out of it.
  /// \param args The arguments the bundle is built from.
  ///
  template<typename... TArgs>
    requires detail::brace_constructible_c<T, TArgs...> &&
             (!std::same_as<std::remove_cvref_t<TArgs>, signal_owner> && ...)
  explicit signal_owner(TArgs&&... args);

public: // Methods
  ///
  /// The bundle this owner announces through.
  /// Public, since connecting to a signal is what a consumer of the owner does.
  ///
  /// \return The bundle.
  ///
  [[nodiscard]] auto sigs() -> T&;

  ///
  /// The bundle this owner announces through, const.
  ///
  /// \return The bundle, const, so that const propagates from the owner to what
  ///         it announces.
  ///
  [[nodiscard]] auto sigs() const -> const T&;

private: // Variables
  T sigs_{};
};

///
/// Access to an owner's signal bundle, for a class that announces through
/// another object's signals rather than keeping any of its own.
/// A relaying class derives from this, publishes the bundle type as a nested
/// sigs_t, and hands its owner to the ctor. From there sigs() reads exactly as
/// it does inside the owner itself, which is the point: a method that announces
/// something does not have to know which of the two it is sitting in.
///
/// \code{.cpp}
/// class widget_task final : public arude::signal_relay<widget_task>
/// {
/// public: // Typedefs
///   using sigs_t = widget_sigs; // The owner's bundle, which is what this base looks up.
///
///   explicit widget_task(widget& owner);
///
///   auto run() -> void;
/// };
///
/// widget_task::widget_task(widget& owner)
///   : signal_relay{owner}
/// {
/// }
///
/// auto widget_task::run() -> void
/// {
///   sigs().value_changed(42); // The widget's subscribers hear it.
/// }
/// \endcode
///
/// What is stored is an arude::signal_owner_base*, rather than the owner's type
/// or a pointer to its bundle, because neither is available where the member is
/// declared: TSelf is incomplete while its own base is being instantiated, so
/// TSelf::sigs_t cannot be looked up yet. The erased pointer needs nothing, and
/// the lookup waits until sigs() is called, by which point TSelf is complete.
/// The cast back on the way out is a downcast, and what makes it defined is the
/// ctor refusing an owner that is not the arude::signal_owner it casts to.
///
/// \tparam TSelf The relaying class, which must declare the owner's bundle type
///         as a nested sigs_t reachable from here. Declaring it in a public
///         section does that; a class that would rather not publish the name
///         declares friend signal_relay; instead, naming this base's
///         injected-class-name, which docs/cpp-conventions.md places above the
///         first access specifier. It is incomplete while this base is
///         instantiated and is only looked into when sigs() or the ctor is
///         called. It is also the only class that can use this one: the ctor is
///         private to it, so a class deriving from signal_relay<widget_task>
///         without being widget_task gets a base it cannot build.
///
/// \note The owner is retained rather than copied, so it must outlive the
/// relay. Copying the owner afterwards leaves the relay where it was, still
/// announcing through the original.
///
/// \note A relay is a non-owning reference: one pointer wide, copyable and
/// assignable, and a copy announces through the same owner.
///
/// \note const does not propagate through sigs(), where an owner's propagates
/// it. The relay refers to a bundle it does not own, exactly as a pointer
/// member would, and its own constness says nothing about what it points at. So
/// a const member function of the relaying class can announce, where a const
/// member function of the owner cannot.
///
/// \see arude::signal_owner
///
template<typename TSelf>
class signal_relay
{
  // The ctor below is private, with the relaying class named as a friend,
  // rather than protected. A protected ctor would let any class at all derive
  // from signal_relay<widget_task> — the parameter says which class this base
  // belongs to, and nothing about deriving checks it — and sigs() would then
  // hand that interloper whatever its own sigs_t happens to name. Naming TSelf
  // is what ties the two together.
  friend TSelf;

private: // Structors
  ///
  /// Ctor binding the relay to the owner it announces through.
  ///
  /// \tparam TOwner The owner's type, deduced and never named by a caller. It
  ///         must derive from arude::signal_owner<TSelf::sigs_t>: that, and not
  ///         the static type of the pointer stored, is what makes the downcast
  ///         in sigs() defined. The second constraint asks for the erased base
  ///         as well, which follows from the first everywhere except an owner
  ///         deriving from two different arude::signal_owner instantiations.
  ///         There the erased base is ambiguous, and asking for it here turns
  ///         what would be an error inside the body into a ctor that simply
  ///         does not take part.
  /// \tparam TDeferred Never named by a caller. It exists so that the lookup of
  ///         TSelf::sigs_t happens when this ctor is called rather than when the
  ///         class is instantiated, which is the whole reason the relaying class
  ///         may still be incomplete at that point. The constraint carries the
  ///         requirement, and not an assert in the body, so a caller can probe
  ///         acceptance.
  /// \param owner The owner, which is retained and must outlive this relay.
  ///
  template<typename TOwner, typename TDeferred = TSelf>
    requires std::derived_from<TOwner, signal_owner<typename TDeferred::sigs_t>> &&
             std::derived_from<TOwner, signal_owner_base>
  explicit signal_relay(TOwner& owner);

public: // Methods
  ///
  /// The bundle the owner announces through.
  /// Public, like the owner's own accessor: what a consumer connects to is the
  /// same bundle whichever of the two it reaches it through.
  ///
  /// There is one overload rather than the owner's pair, and it is the const
  /// one. Announcing something changes the owner's bundle and leaves the relay
  /// exactly as it was, so const is what holds here, and the bundle handed back
  /// is no more const for it.
  ///
  /// \tparam TDeferred Never named by a caller. \see the ctor.
  /// \return The owner's bundle, mutable.
  ///
  template<typename TDeferred = TSelf>
  [[nodiscard]] auto sigs() const -> typename TDeferred::sigs_t&;

private: // Variables
  // Never null: the ctor above is the only one there is, and it takes a
  // reference. The initialiser is what keeps that true of any ctor added later.
  signal_owner_base* owner_ = nullptr;
};

///
///
template<typename T>
template<typename... TArgs>
  requires detail::brace_constructible_c<T, TArgs...> &&
           (!std::same_as<std::remove_cvref_t<TArgs>, signal_owner<T>> && ...)
signal_owner<T>::signal_owner(TArgs&&... args)
  : sigs_{std::forward<TArgs>(args)...}
{
}

///
///
template<typename T>
auto signal_owner<T>::sigs() -> T&
{
  return sigs_;
}

///
///
template<typename T>
auto signal_owner<T>::sigs() const -> const T&
{
  return sigs_;
}

///
///
template<typename TSelf>
template<typename TOwner, typename TDeferred>
  requires std::derived_from<TOwner, signal_owner<typename TDeferred::sigs_t>> &&
           std::derived_from<TOwner, signal_owner_base>
signal_relay<TSelf>::signal_relay(TOwner& owner)
  : owner_{&owner}
{
}

///
///
template<typename TSelf>
template<typename TDeferred>
auto signal_relay<TSelf>::sigs() const -> typename TDeferred::sigs_t&
{
  return static_cast<signal_owner<typename TDeferred::sigs_t>*>(owner_)->sigs();
}

} // namespace signal_owner_

///
/// The name a relay is bound to: arude::signal_owner_base.
/// The class itself is declared one namespace deeper so that deriving from it
/// does not pull arude into the deriving class's ADL set, exactly as
/// arude::noncopyable is; this alias is what makes that arrangement invisible
/// to a consumer.
///
/// \see arude::signal_owner_::signal_owner_base
///
// NOLINTNEXTLINE(readability-identifier-naming): a _t suffix would rename the type consumers name.
using signal_owner_base = signal_owner_::signal_owner_base;

///
/// The name to inherit from: arude::signal_owner.
/// The class itself is declared one namespace deeper, for the reason given on
/// arude::signal_owner_base. It is an alias template rather than an alias, which
/// is still a base-specifier and still leaves the injected-class-name spelled
/// signal_owner in the deriving class.
///
/// \tparam T The bundle of signals the owner announces through.
///
/// \see arude::signal_owner_::signal_owner
///
template<typename T>
// NOLINTNEXTLINE(readability-identifier-naming): a _t suffix would rename the type consumers inherit from.
using signal_owner = signal_owner_::signal_owner<T>;

///
/// The name to inherit from: arude::signal_relay.
/// The class itself is declared one namespace deeper, for the reason given on
/// arude::signal_owner_base.
///
/// \tparam TSelf The relaying class, which is the only one that may name this.
///
/// \see arude::signal_owner_::signal_relay
///
template<typename TSelf>
// NOLINTNEXTLINE(readability-identifier-naming): a _t suffix would rename the type consumers inherit from.
using signal_relay = signal_owner_::signal_relay<TSelf>;

} // namespace arude
