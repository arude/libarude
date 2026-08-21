///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// The owners under test are declared in test/pimpl/widget.hpp and defined in
/// test/pimpl/widget.cpp. Nothing here completes an impl_t, deliberately: this
/// translation unit is standing in for a consumer, and everything below has to
/// work while the implementations are out of sight.
///

#include "arude/exception.hpp"
#include "arude/noncopyable.hpp"
#include "arude/pimpl_owner.hpp"
#include "test/pimpl/widget.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <type_traits>
#include <utility>

namespace arude
{

// Declared in this translation unit only, never defined and never called: the
// ADL scenario below asks whether name lookup reaches namespace arude for a
// given argument type, and nothing more.
auto adl_probe(const auto& value) -> void;

} // namespace arude

namespace pimpl_owner_test
{

///
/// An owner that declares a dtor and stops there.
/// Declaring one suppresses the implicit move members, and overload resolution
/// then finds the copy members arude::noncopyable deleted, so the type comes
/// out immovable. pimpl_test::widget is this shape with both moves defaulted
/// back, and the pair is what the scenario below compares.
///
// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions): declaring only the dtor is what is under test.
class guarded final : private arude::pimpl_owner<guarded>
{
public:
  class impl_t;

  guarded();
  ~guarded() = default;
};

///
/// A compile-only harness rather than a consumer: its impl_t is complete in
/// this translation unit, so the base's ctor and dtor can instantiate here.
/// That is exactly what a real owner's must not do — the split across
/// test/pimpl is the proof — but it is what lets a trait ask which pointers
/// the ctor accepts, and a constraint failing is then a false rather than an
/// error.
///
// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions): the copy members come deleted from arude::noncopyable.
class probe final : private arude::pimpl_owner<probe>
{
public:
  class impl_t final
  {};

  ///
  /// Whether the base's ctor accepts the pointer.
  /// The point of the harness: it restates the base's own verdict as a named
  /// constraint, so a constructibility trait can read it through this class's
  /// signature. A trait asked about the base directly would report false for
  /// everything, since the base's private dtor defeats it regardless. A
  /// variable template and not a concept, because a concept may not sit in a
  /// class, and this way the ctor's constraint is a stable spelling that the
  /// definition below can repeat exactly.
  ///
  template<typename TPtr>
  static constexpr bool ctor_accepts_v = requires(TPtr p) { pimpl_owner(std::move(p)); };

  ///
  /// Ctor forwarding the pointer to the base.
  /// \tparam TPtr The pointer type under test.
  /// \param pimpl The pointer to hand over.
  ///
  template<typename TPtr>
    requires probe::ctor_accepts_v<TPtr>
  explicit probe(TPtr pimpl);

  ~probe() = default;
  probe(probe&& other) noexcept = default;
  auto operator=(probe&& other) noexcept -> probe& = default;
};

///
/// A stateless custom deleter for custom_probe's implementation, empty so that
/// the base has nothing to store.
///
struct probe_deleter final
{
  ///
  /// Frees the implementation.
  /// \param impl The implementation to free.
  ///
  auto operator()(probe::impl_t* impl) const noexcept -> void;
};

///
/// The same harness, handed a custom deleter: what must compile for it is only
/// a pointer carrying exactly that deleter, and nothing else.
///
// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions): the copy members come deleted from arude::noncopyable.
class custom_probe final : private arude::pimpl_owner<custom_probe, probe_deleter>
{
public:
  class impl_t final
  {};

  ///
  /// Whether the base's ctor accepts the pointer. \see probe::ctor_accepts_v.
  ///
  template<typename TPtr>
  static constexpr bool ctor_accepts_v = requires(TPtr p) { pimpl_owner(std::move(p)); };

  ///
  /// Ctor forwarding the pointer to the base.
  /// \tparam TPtr The pointer type under test.
  /// \param pimpl The pointer to hand over.
  ///
  template<typename TPtr>
    requires custom_probe::ctor_accepts_v<TPtr>
  explicit custom_probe(TPtr pimpl);

  ~custom_probe() = default;
  custom_probe(custom_probe&& other) noexcept = default;
  auto operator=(custom_probe&& other) noexcept -> custom_probe& = default;
};

class stateful_probe;

///
/// A deleter with state, which pimpl_owner must reject: carrying it would cost
/// the owner the single-pointer size the whole class exists for.
/// Forward-declared here, since the base specifier needs only its name, and
/// defined after the owner, whose impl_t the operator() must name.
///
struct stateful_deleter;

///
/// The same harness, handed the stateful deleter above, so the rejection can
/// be observed at the owner's own size.
///
// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions): the copy members come deleted from arude::noncopyable.
class stateful_probe final : private arude::pimpl_owner<stateful_probe, stateful_deleter>
{
public:
  class impl_t final
  {};

  ///
  /// Whether the base's ctor accepts the pointer. \see probe::ctor_accepts_v.
  ///
  template<typename TPtr>
  static constexpr bool ctor_accepts_v = requires(TPtr p) { pimpl_owner(std::move(p)); };

  ///
  /// Ctor forwarding the pointer to the base.
  /// \tparam TPtr The pointer type under test.
  /// \param pimpl The pointer to hand over.
  ///
  template<typename TPtr>
    requires stateful_probe::ctor_accepts_v<TPtr>
  explicit stateful_probe(TPtr pimpl);

  ~stateful_probe() = default;
  stateful_probe(stateful_probe&& other) noexcept = default;
  auto operator=(stateful_probe&& other) noexcept -> stateful_probe& = default;
};

///
/// The stateful deleter stateful_probe was handed.
/// It is a single int, so an owner carrying it could not be one pointer wide,
/// which is why the base refuses it.
///
struct stateful_deleter final
{
  ///
  /// Frees the implementation.
  /// \param impl The implementation to free.
  ///
  auto operator()(stateful_probe::impl_t* impl) const noexcept -> void;

private: // Variables
  // Never read: the member exists only to make the deleter stateful, which is
  // what must be rejected.
  [[maybe_unused]] int pad_ = 0;
};

///
///
template<typename TPtr>
  requires probe::ctor_accepts_v<TPtr>
probe::probe(TPtr pimpl)
  : pimpl_owner{std::move(pimpl)}
{
}

///
///
template<typename TPtr>
  requires custom_probe::ctor_accepts_v<TPtr>
custom_probe::custom_probe(TPtr pimpl)
  : pimpl_owner{std::move(pimpl)}
{
}

///
///
template<typename TPtr>
  requires stateful_probe::ctor_accepts_v<TPtr>
stateful_probe::stateful_probe(TPtr pimpl)
  : pimpl_owner{std::move(pimpl)}
{
}

///
///
auto probe_deleter::operator()(probe::impl_t* const impl) const noexcept -> void
{
  delete impl;
}

///
///
auto stateful_deleter::operator()(stateful_probe::impl_t* const impl) const noexcept -> void
{
  delete impl;
}

// Only ADL can reach arude::adl_probe from here: this namespace encloses no
// declaration of that name, so an unqualified call succeeds exactly when the
// argument's type drags namespace arude in with it.
template<typename T>
concept adl_reaches_arude_c = requires(const T& value) { adl_probe(value); };

///
/// Whether a type is complete in this translation unit.
///
template<typename T>
concept complete_c = requires { sizeof(T); };

} // namespace pimpl_owner_test

SCENARIO("pimpl_owner is a base class and nothing else", "[pimpl_owner]")
{
  using owner_t = arude::pimpl_owner<pimpl_test::widget>;

  GIVEN("the type on its own")
  {
    THEN("a caller can neither create nor destroy one")
    {
      // The dtor is protected, so an owner cannot be deleted through a pointer
      // to this base, and nothing outside the hierarchy can hold one by value.
      STATIC_REQUIRE(!std::is_destructible_v<owner_t>);
      STATIC_REQUIRE(!std::is_default_constructible_v<owner_t>);
    }

    THEN("it carries no vtable, since nothing here is meant to be virtual")
    {
      STATIC_REQUIRE(!std::is_polymorphic_v<owner_t>);
      STATIC_REQUIRE(!std::has_virtual_destructor_v<owner_t>);
    }

    THEN("it takes its copy members from arude::noncopyable")
    {
      STATIC_REQUIRE(std::is_base_of_v<arude::noncopyable, owner_t>);
    }

    THEN("it is one pointer wide, exactly as a hand-written pimpl would be")
    {
      // Nothing is spent on carrying a deleter about; what pays for that is the
      // out-of-line dtor every owner has to define.
      STATIC_REQUIRE(sizeof(owner_t) == sizeof(void*));
    }
  }
}

// The whole point of the class. If this ever became true, every scenario below
// would still pass while testing something else entirely.
SCENARIO("the implementation stays incomplete in a consumer's translation unit", "[pimpl_owner]")
{
  GIVEN("an owner declared in a header")
  {
    THEN("the owner is complete here and its implementation is not")
    {
      STATIC_REQUIRE(pimpl_owner_test::complete_c<pimpl_test::widget>);
      STATIC_REQUIRE(!pimpl_owner_test::complete_c<pimpl_test::widget::impl_t>);
    }
  }
}

SCENARIO("an owner is move-only and costs one pointer", "[pimpl_owner]")
{
  GIVEN("an owner type")
  {
    THEN("it can be neither copy constructed nor copy assigned")
    {
      STATIC_REQUIRE(!std::is_copy_constructible_v<pimpl_test::widget>);
      STATIC_REQUIRE(!std::is_copy_assignable_v<pimpl_test::widget>);
    }

    THEN("it can be move constructed and move assigned, and neither throws")
    {
      STATIC_REQUIRE(std::is_move_constructible_v<pimpl_test::widget>);
      STATIC_REQUIRE(std::is_move_assignable_v<pimpl_test::widget>);
      STATIC_REQUIRE(std::is_nothrow_move_constructible_v<pimpl_test::widget>);
      STATIC_REQUIRE(std::is_nothrow_move_assignable_v<pimpl_test::widget>);
    }

    THEN("it is the size of the base, so the arrangement adds nothing further")
    {
      STATIC_REQUIRE(sizeof(pimpl_test::widget) == sizeof(void*));

      // A custom deleter must be stateless, so naming one costs the owner
      // nothing either.
      STATIC_REQUIRE(sizeof(pimpl_test::custom_deleter_widget) == sizeof(void*));
    }

    THEN("its dtor is not trivial, since the implementation has to be freed")
    {
      STATIC_REQUIRE(!std::is_trivially_destructible_v<pimpl_test::widget>);
    }
  }
}

// The ctor releases the pointer it is handed, so its deleter is dropped: the
// base must therefore see to it that only a deleter it can stand in for ever
// arrives. The harness probes complete their impl_t in this file, so a
// constructibility trait can ask which pointers the ctor takes.
SCENARIO("a custom deleter is accepted, and only the one it asked for", "[pimpl_owner]")
{
  using impl_t = pimpl_owner_test::probe::impl_t;

  GIVEN("an owner that takes the ordinary delete")
  {
    THEN("a default-deleter pointer is accepted")
    {
      STATIC_REQUIRE(std::is_constructible_v<pimpl_owner_test::probe, std::unique_ptr<impl_t>>);
    }

    THEN("a pointer carrying another deleter is rejected, rather than freed the wrong way")
    {
      STATIC_REQUIRE_FALSE(
        std::is_constructible_v<pimpl_owner_test::probe, std::unique_ptr<impl_t, pimpl_owner_test::probe_deleter>>);
    }
  }

  GIVEN("an owner that names a custom deleter")
  {
    THEN("a pointer carrying exactly that deleter is accepted")
    {
      STATIC_REQUIRE(
        std::
          is_constructible_v<pimpl_owner_test::custom_probe, std::unique_ptr<impl_t, pimpl_owner_test::probe_deleter>>);
    }

    THEN("a default-deleter pointer is rejected: free() and delete are not interchangeable")
    {
      STATIC_REQUIRE_FALSE(std::is_constructible_v<pimpl_owner_test::custom_probe, std::unique_ptr<impl_t>>);
    }

    THEN("a pointer carrying some third deleter is rejected too")
    {
      STATIC_REQUIRE_FALSE(
        std::is_constructible_v<
          pimpl_owner_test::custom_probe,
          std::unique_ptr<impl_t, pimpl_owner_test::stateful_deleter>>);
    }
  }

  GIVEN("an owner that names a stateful deleter")
  {
    THEN("not even its own pointer is accepted, since the deleter would need somewhere to live")
    {
      STATIC_REQUIRE_FALSE(
        std::is_constructible_v<
          pimpl_owner_test::stateful_probe,
          std::unique_ptr<pimpl_owner_test::stateful_probe::impl_t, pimpl_owner_test::stateful_deleter>>);
    }
  }
}

SCENARIO("an owner works while its implementation is out of sight", "[pimpl_owner]")
{
  GIVEN("a freshly built owner")
  {
    auto widget = pimpl_test::widget{};

    THEN("it starts out holding an implementation")
    {
      REQUIRE(widget.holds_impl());
      REQUIRE(widget.count() == 0);
    }

    WHEN("it is asked to do something")
    {
      widget.do_something();
      widget.do_something();

      THEN("the implementation kept the state")
      {
        REQUIRE(widget.count() == 2);
      }
    }
  }
}

SCENARIO("an owner destroys its implementation exactly once", "[pimpl_owner]")
{
  GIVEN("the number of implementations alive")
  {
    const auto before = pimpl_test::impl_live_count();

    WHEN("an owner is created and goes out of scope")
    {
      {
        const auto widget = pimpl_test::widget{};
        REQUIRE(pimpl_test::impl_live_count() == before + 1);
      }

      THEN("the implementation went with it")
      {
        REQUIRE(pimpl_test::impl_live_count() == before);
      }
    }

    WHEN("an owner is moved from")
    {
      {
        auto source = pimpl_test::widget{};
        const auto moved = pimpl_test::widget{std::move(source)};

        THEN("the implementation was not copied, only handed over")
        {
          REQUIRE(pimpl_test::impl_live_count() == before + 1);
        }
      }

      THEN("one destruction is all it takes")
      {
        REQUIRE(pimpl_test::impl_live_count() == before);
      }
    }

    WHEN("an owner is move assigned to")
    {
      auto target = pimpl_test::widget{};
      auto source = pimpl_test::widget{};
      source.do_something();
      REQUIRE(pimpl_test::impl_live_count() == before + 2);

      target = std::move(source);

      THEN("the implementation the target held is destroyed and the other one arrives")
      {
        REQUIRE(pimpl_test::impl_live_count() == before + 1);
        REQUIRE(target.count() == 1);
      }
    }

    // Guarding against self-assignment is a rule the conventions ask for, and
    // the answer here comes from std::unique_ptr, which is specified to
    // reset(release()) and so leaves the pointer where it was. Written through
    // a reference because the compilers warn about the direct spelling.
    WHEN("an owner is move assigned to itself")
    {
      auto widget = pimpl_test::widget{};
      widget.do_something();
      auto& alias = widget;

      widget = std::move(alias);

      THEN("it still holds the same implementation")
      {
        REQUIRE(pimpl_test::impl_live_count() == before + 1);
        REQUIRE(widget.holds_impl());
        REQUIRE(widget.count() == 1);
      }
    }
  }
}

// A custom-deleter owner must reach its deleter exactly as often as the
// ordinary owner reaches delete: once per implementation, on destruction.
SCENARIO("an owner with a custom deleter frees through it", "[pimpl_owner]")
{
  GIVEN("the number of custom deletions so far")
  {
    const auto before = pimpl_test::custom_delete_count();

    WHEN("an owner is created and goes out of scope")
    {
      {
        const auto widget = pimpl_test::custom_deleter_widget{};
        REQUIRE(widget.holds_impl());
      }

      THEN("the deleter ran exactly once")
      {
        REQUIRE(pimpl_test::custom_delete_count() == before + 1);
      }
    }

    WHEN("an owner is moved from")
    {
      {
        auto source = pimpl_test::custom_deleter_widget{};
        const auto moved = pimpl_test::custom_deleter_widget{std::move(source)};

        THEN("moving freed nothing")
        {
          REQUIRE(pimpl_test::custom_delete_count() == before);
        }
      }

      THEN("the deleter ran exactly once, at the end")
      {
        REQUIRE(pimpl_test::custom_delete_count() == before + 1);
      }
    }
  }
}

// A hand-written std::unique_ptr<impl_t> member gives a const member function a
// mutable implementation, because the const applies to the pointer rather than
// to what it points at. The const overload of impl() is what fixes that, and
// the two tag() overloads are how a test can tell which one it reached.
SCENARIO("const propagates through to the implementation", "[pimpl_owner]")
{
  GIVEN("an owner and a const reference to it")
  {
    auto widget = pimpl_test::widget{};
    const auto& const_widget = widget;

    THEN("each reaches the matching overload on the implementation")
    {
      REQUIRE(widget.tag() == 1);
      REQUIRE(const_widget.tag() == 2);
    }
  }
}

SCENARIO("a moved-from owner is valid and empty", "[pimpl_owner]")
{
  GIVEN("an owner that has been moved from")
  {
    auto source = pimpl_test::widget{};
    source.do_something();
    auto moved = pimpl_test::widget{std::move(source)};

    THEN("the implementation and its state arrived intact")
    {
      REQUIRE(moved.holds_impl());
      REQUIRE(moved.count() == 1);
    }

    THEN("the source holds nothing and says so")
    {
      // NOLINTNEXTLINE(bugprone-use-after-move): asking what state it was left in is the test.
      REQUIRE(!source.holds_impl());
    }

    WHEN("it is assigned to again")
    {
      // NOLINTNEXTLINE(bugprone-use-after-move): a moved-from owner is required to be assignable.
      source = pimpl_test::widget{};

      THEN("it is usable once more")
      {
        REQUIRE(source.holds_impl());
        REQUIRE(source.count() == 0);
      }
    }
  }
}

// Every owner has to declare a dtor, and declaring one suppresses the implicit
// move members: what overload resolution finds instead are the copy members
// arude::noncopyable deleted, leaving the owner immovable. An owner that wants
// to stay movable therefore declares all three, which is what widget does and
// what guarded leaves out.
SCENARIO("an owner that declares a dtor has to declare its moves too", "[pimpl_owner]")
{
  GIVEN("an owner that declares a dtor and both moves")
  {
    THEN("it stays movable")
    {
      STATIC_REQUIRE(std::is_move_constructible_v<pimpl_test::widget>);
      STATIC_REQUIRE(std::is_move_assignable_v<pimpl_test::widget>);
    }
  }

  GIVEN("an owner that declares a dtor and nothing else")
  {
    THEN("it is neither copyable nor movable")
    {
      STATIC_REQUIRE(!std::is_copy_constructible_v<pimpl_owner_test::guarded>);
      STATIC_REQUIRE(!std::is_copy_assignable_v<pimpl_owner_test::guarded>);
      STATIC_REQUIRE(!std::is_move_constructible_v<pimpl_owner_test::guarded>);
      STATIC_REQUIRE(!std::is_move_assignable_v<pimpl_owner_test::guarded>);
    }

    THEN("it can still be destroyed")
    {
      STATIC_REQUIRE(std::is_destructible_v<pimpl_owner_test::guarded>);
    }
  }
}

// Bases are initialised in declaration order, so an owner may hand impl() to a
// collaborator listed after pimpl_owner. Listed before, the storage would still
// be empty. Nothing in the language stops the wrong order compiling, which is
// why the right one is worth a test.
SCENARIO("a base listed after pimpl_owner may be initialised from impl()", "[pimpl_owner]")
{
  GIVEN("an owner whose second base was handed the implementation")
  {
    const auto widget = pimpl_test::relay_widget{};

    THEN("the collaborator was handed the implementation the owner still uses")
    {
      REQUIRE(widget.relayed() != nullptr);
      REQUIRE(widget.relayed() == widget.impl_address());
    }
  }
}

// The stored pointer is a void*, and the type only comes back at the delete, in
// the translation unit that completes impl_t. If it did not, an over-aligned
// implementation would reach the ordinary operator delete and the mismatch
// would be undefined rather than visibly wrong, so this is a scenario worth
// running under a sanitiser.
SCENARIO("an over-aligned implementation survives the round trip through void*", "[pimpl_owner]")
{
  GIVEN("an owner whose implementation is over-aligned")
  {
    const auto widget = pimpl_test::aligned_widget{};

    THEN("the alignment is wider than a pointer, so it cannot have been met by luck")
    {
      REQUIRE(pimpl_test::aligned_widget::impl_alignment() > sizeof(void*));
    }

    THEN("the implementation sits on its boundary")
    {
      REQUIRE(widget.impl_aligned());
    }
  }
}

SCENARIO("an exception while constructing leaves nothing behind", "[pimpl_owner]")
{
  GIVEN("the number of implementations alive")
  {
    const auto before = pimpl_test::impl_live_count();

    WHEN("the implementation itself throws")
    {
      THEN("the exception reaches the caller and nothing is left alive")
      {
        REQUIRE_THROWS_AS(pimpl_test::throwing_widget{}, arude::exception_base);
        REQUIRE(pimpl_test::impl_live_count() == before);
      }
    }

    // The interesting half: the base is fully constructed by the time this
    // throws, so its dtor runs during unwinding and destroys the implementation
    // without the owner doing anything about it.
    WHEN("the owner throws after its implementation was built")
    {
      const auto built_before = pimpl_test::impl_total_count();

      THEN("the implementation was built and then destroyed again")
      {
        REQUIRE_THROWS_AS(pimpl_test::late_throwing_widget{}, arude::exception_base);
        REQUIRE(pimpl_test::impl_total_count() == built_before + 1);
        REQUIRE(pimpl_test::impl_live_count() == before);
      }
    }
  }
}

SCENARIO("two owners in one program keep their implementations apart", "[pimpl_owner]")
{
  GIVEN("two unrelated owners")
  {
    auto widget = pimpl_test::widget{};
    auto gadget = pimpl_test::gadget{10};

    WHEN("each is used")
    {
      widget.do_something();
      gadget.bump();

      THEN("neither sees the other's implementation")
      {
        REQUIRE(widget.count() == 1);
        REQUIRE(gadget.value() == 11);
      }
    }
  }
}

// The base sits in its own namespace so that deriving from it does not make
// every unqualified call on an owner search namespace arude. Without that, an
// arude overload could be picked up by client code that never asked for one.
SCENARIO("deriving from pimpl_owner does not drag namespace arude into ADL", "[pimpl_owner]")
{
  GIVEN("a type declared in namespace arude, as a control")
  {
    THEN("ADL reaches the namespace, so the probe is capable of finding a name there")
    {
      STATIC_REQUIRE(pimpl_owner_test::adl_reaches_arude_c<arude::exception_base>);
    }
  }

  GIVEN("a type derived from pimpl_owner")
  {
    THEN("ADL does not reach namespace arude, through either base")
    {
      STATIC_REQUIRE(!pimpl_owner_test::adl_reaches_arude_c<pimpl_test::widget>);
      STATIC_REQUIRE(!pimpl_owner_test::adl_reaches_arude_c<pimpl_test::relay_widget>);
    }
  }
}

// arude::pimpl_owner is the spelling consumers write, and the class itself sits
// one namespace further down. Renaming that namespace is invisible to them, but
// it is what the ADL scenario above rests on, so the two are pinned together.
SCENARIO("the published name denotes the class template in the nested namespace", "[pimpl_owner]")
{
  GIVEN("the alias in arude and the class it names")
  {
    THEN("they are the same type")
    {
      STATIC_REQUIRE(
        std::is_same_v<arude::pimpl_owner<pimpl_test::widget>, arude::pimpl_owner_::pimpl_owner<pimpl_test::widget>>);
    }
  }
}
