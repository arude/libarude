///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///

#include "arude/noncopyable.hpp"

#include <catch2/catch_test_macros.hpp>

#include <type_traits>
#include <utility>

namespace arude
{

// Declared in this translation unit only, never defined and never called: the
// ADL scenario below asks whether name lookup reaches namespace arude for a
// given argument type, and nothing more. It has to live here rather than in the
// test namespace, because a declaration the test namespace can see by ordinary
// unqualified lookup would answer the question before ADL is consulted.
struct adl_control final
{};

auto adl_probe(const auto& value) -> void;

} // namespace arude

namespace noncopyable_test
{

///
/// A derived type carrying state, so a move can be observed to have moved it.
///
class widget final : public arude::noncopyable
{
public:
  constexpr explicit widget(int value);

  [[nodiscard]] constexpr auto value() const -> int;

private:
  int value_ = 0;
};

///
/// A derived type that inherits privately, which is the usual way to say
/// "implemented in terms of" rather than "is a".
///
class private_widget final : private arude::noncopyable
{};

///
/// A derived type that declares its own destructor.
/// Declaring one suppresses the implicit move members, so overload resolution
/// falls back on the copy members — which the base deleted. The result is a
/// type that is neither copyable nor movable, which is a real trap and is
/// pinned by a scenario below rather than left to be discovered.
///
// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions): declaring only the destructor is what is under test.
class guarded final : public arude::noncopyable
{
public:
  ~guarded() = default;
};

// Only ADL can reach arude::adl_probe from here: this namespace encloses no
// declaration of that name, so an unqualified call succeeds exactly when the
// argument's type drags namespace arude in with it.
template<typename T>
concept adl_reaches_arude = requires(const T& value) { adl_probe(value); };

///
///
constexpr widget::widget(const int value)
  : value_{value}
{
}

///
///
constexpr auto widget::value() const -> int
{
  return value_;
}

} // namespace noncopyable_test

SCENARIO("noncopyable is a base class and nothing else", "[noncopyable]")
{
  GIVEN("the type on its own")
  {
    THEN("a caller can neither create nor destroy one")
    {
      // Both structors are protected, so the traits report false for anyone
      // outside the hierarchy. The destructor half is what stops a derived
      // object being deleted through an arude::noncopyable*.
      STATIC_REQUIRE(!std::is_default_constructible_v<arude::noncopyable>);
      STATIC_REQUIRE(!std::is_destructible_v<arude::noncopyable>);
    }

    THEN("it carries no size and no vtable")
    {
      STATIC_REQUIRE(std::is_empty_v<arude::noncopyable>);
      STATIC_REQUIRE(!std::is_polymorphic_v<arude::noncopyable>);
      STATIC_REQUIRE(!std::has_virtual_destructor_v<arude::noncopyable>);
    }

    THEN("it is not final, since being derived from is the whole point")
    {
      STATIC_REQUIRE(!std::is_final_v<arude::noncopyable>);
      STATIC_REQUIRE(std::is_base_of_v<arude::noncopyable, noncopyable_test::widget>);
    }
  }
}

SCENARIO("inheriting noncopyable removes the copy members", "[noncopyable]")
{
  GIVEN("a publicly derived type")
  {
    THEN("it can be neither copy constructed nor copy assigned")
    {
      STATIC_REQUIRE(!std::is_copy_constructible_v<noncopyable_test::widget>);
      STATIC_REQUIRE(!std::is_copy_assignable_v<noncopyable_test::widget>);
    }
  }

  GIVEN("a privately derived type")
  {
    THEN("the copy members are gone just the same")
    {
      STATIC_REQUIRE(!std::is_copy_constructible_v<noncopyable_test::private_widget>);
      STATIC_REQUIRE(!std::is_copy_assignable_v<noncopyable_test::private_widget>);
    }
  }

  GIVEN("the derived type's own construction")
  {
    THEN("it is untouched")
    {
      STATIC_REQUIRE(std::is_constructible_v<noncopyable_test::widget, int>);
      STATIC_REQUIRE(std::is_default_constructible_v<noncopyable_test::private_widget>);
      STATIC_REQUIRE(std::is_destructible_v<noncopyable_test::widget>);
    }
  }
}

// The name says non-copyable rather than non-movable, and the defaulted move
// members in the base are what makes the difference. A derived type is meant to
// come out move-only, so this is interface, not an accident to be tidied away.
SCENARIO("inheriting noncopyable leaves the move members", "[noncopyable]")
{
  GIVEN("a derived type")
  {
    THEN("it can be move constructed and move assigned, and neither throws")
    {
      STATIC_REQUIRE(std::is_move_constructible_v<noncopyable_test::widget>);
      STATIC_REQUIRE(std::is_move_assignable_v<noncopyable_test::widget>);
      STATIC_REQUIRE(std::is_nothrow_move_constructible_v<noncopyable_test::widget>);
      STATIC_REQUIRE(std::is_nothrow_move_assignable_v<noncopyable_test::widget>);
    }

    THEN("the moves stay trivial, so the base costs nothing at runtime either")
    {
      STATIC_REQUIRE(std::is_trivially_move_constructible_v<noncopyable_test::widget>);
      STATIC_REQUIRE(std::is_trivially_move_assignable_v<noncopyable_test::widget>);
      STATIC_REQUIRE(std::is_trivially_destructible_v<noncopyable_test::widget>);
    }
  }

  GIVEN("a derived object with state")
  {
    auto source = noncopyable_test::widget{42};

    WHEN("it is move constructed from")
    {
      const auto moved = noncopyable_test::widget{std::move(source)};

      THEN("the state comes across")
      {
        REQUIRE(moved.value() == 42);
      }
    }

    WHEN("it is move assigned from")
    {
      auto target = noncopyable_test::widget{0};
      // NOLINTNEXTLINE(bugprone-use-after-move): Catch2 re-enters the GIVEN per section, so source is fresh here.
      target = std::move(source);

      THEN("the state comes across")
      {
        REQUIRE(target.value() == 42);
      }
    }
  }
}

// Declaring a destructor is ordinary enough — an RAII type that closes a handle
// does it — and doing so silently takes the move members away. A derived type
// that wants both has to say = default for all four.
SCENARIO("a derived type that declares a destructor loses the move as well", "[noncopyable]")
{
  GIVEN("a derived type with a user-declared destructor")
  {
    THEN("it is neither copyable nor movable")
    {
      STATIC_REQUIRE(!std::is_copy_constructible_v<noncopyable_test::guarded>);
      STATIC_REQUIRE(!std::is_copy_assignable_v<noncopyable_test::guarded>);
      STATIC_REQUIRE(!std::is_move_constructible_v<noncopyable_test::guarded>);
      STATIC_REQUIRE(!std::is_move_assignable_v<noncopyable_test::guarded>);
    }

    THEN("it can still be created and destroyed")
    {
      STATIC_REQUIRE(std::is_default_constructible_v<noncopyable_test::guarded>);
      STATIC_REQUIRE(std::is_destructible_v<noncopyable_test::guarded>);
    }
  }
}

SCENARIO("the base occupies no space in a derived object", "[noncopyable]")
{
  GIVEN("a derived type holding a single int")
  {
    THEN("it is the size of that int, so the empty base is optimised away")
    {
      STATIC_REQUIRE(sizeof(noncopyable_test::widget) == sizeof(int));
    }
  }
}

SCENARIO("a derived type stays usable at compile time", "[noncopyable]")
{
  GIVEN("a constant expression context")
  {
    THEN("a derived object can be built, moved and read there")
    {
      STATIC_REQUIRE(noncopyable_test::widget{7}.value() == 7);
      STATIC_REQUIRE(noncopyable_test::widget{noncopyable_test::widget{7}}.value() == 7);
    }
  }
}

// The base sits in its own namespace so that deriving from it does not make
// every unqualified call on a derived object search namespace arude. Without
// that, an arude overload could be picked up by client code that never asked
// for one, and the failure mode is a call quietly going somewhere else.
SCENARIO("deriving from noncopyable does not drag namespace arude into ADL", "[noncopyable]")
{
  GIVEN("a type declared in namespace arude, as a control")
  {
    THEN("ADL reaches the namespace, so the probe is capable of finding a name there")
    {
      STATIC_REQUIRE(noncopyable_test::adl_reaches_arude<arude::adl_control>);
    }
  }

  GIVEN("a type derived from noncopyable")
  {
    THEN("ADL does not reach namespace arude")
    {
      STATIC_REQUIRE(!noncopyable_test::adl_reaches_arude<noncopyable_test::widget>);
      STATIC_REQUIRE(!noncopyable_test::adl_reaches_arude<noncopyable_test::private_widget>);
    }
  }

  GIVEN("the base type itself")
  {
    THEN("it too is associated with the nested namespace rather than arude")
    {
      STATIC_REQUIRE(!noncopyable_test::adl_reaches_arude<arude::noncopyable>);
    }
  }
}

// arude::noncopyable is the spelling consumers write, and the class itself sits
// one namespace further down. Renaming that namespace is therefore invisible to
// them, but it is what the ADL scenario above rests on, so the two names are
// pinned to each other here.
SCENARIO("the published name denotes the class in the nested namespace", "[noncopyable]")
{
  GIVEN("the alias in arude and the class it names")
  {
    THEN("they are the same type")
    {
      STATIC_REQUIRE(std::is_same_v<arude::noncopyable, arude::non_copyable_::noncopyable>);
    }
  }
}
