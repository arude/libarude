///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///

#include "arude/non_owning_t.hpp"

#include <catch2/catch_test_macros.hpp>

#include <type_traits>

namespace non_owning_t_test
{

///
/// A small stateful type, so a test can hold a pointer to a real object and
/// read through it.
///
class widget final
{
public:
  constexpr explicit widget(int value);

  [[nodiscard]] constexpr auto value() const -> int;

private:
  int value_ = 0;
};

///
/// True when the alias can be named for T at all. Naming it is what runs the
/// constraint, so a requires-expression is how a rejected pointee is observed
/// without producing a hard error.
///
template<typename T>
concept well_formed_c = requires { typename arude::non_owning_t<T>; };

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

} // namespace non_owning_t_test

// The name is an annotation, not a wrapper: nothing between the caller and a
// plain pointer may change how the type behaves.
SCENARIO("non_owning_t is exactly a pointer to its pointee", "[non_owning_t]")
{
  GIVEN("a class type and a fundamental type")
  {
    THEN("the alias is the pointer type itself")
    {
      STATIC_REQUIRE(std::is_same_v<arude::non_owning_t<int>, int*>);
      STATIC_REQUIRE(std::is_same_v<arude::non_owning_t<non_owning_t_test::widget>, non_owning_t_test::widget*>);
    }
  }

  GIVEN("a const-qualified pointee")
  {
    THEN("the constness stays with the pointee, not the pointer")
    {
      STATIC_REQUIRE(std::is_same_v<arude::non_owning_t<const int>, const int*>);
      STATIC_REQUIRE(
        std::is_same_v<arude::non_owning_t<const non_owning_t_test::widget>, const non_owning_t_test::widget*>);
    }
  }

  GIVEN("void as the pointee")
  {
    THEN("it works like any other non-pointer, so the constraint rejects pointers and nothing else")
    {
      STATIC_REQUIRE(std::is_same_v<arude::non_owning_t<void>, void*>);
    }
  }
}

SCENARIO("non_owning_t carries no ownership machinery", "[non_owning_t]")
{
  GIVEN("an alias for a class type")
  {
    THEN("it is a trivially copyable, trivially destructible pointer")
    {
      STATIC_REQUIRE(std::is_pointer_v<arude::non_owning_t<non_owning_t_test::widget>>);
      STATIC_REQUIRE(std::is_trivially_copyable_v<arude::non_owning_t<non_owning_t_test::widget>>);
      STATIC_REQUIRE(std::is_trivially_destructible_v<arude::non_owning_t<non_owning_t_test::widget>>);
    }

    THEN("it is the size of any other pointer to that type, so no bookkeeping rides along")
    {
      STATIC_REQUIRE(sizeof(arude::non_owning_t<non_owning_t_test::widget>) == sizeof(non_owning_t_test::widget*));
    }
  }
}

// The whole point of the constraint is to catch the mistake of applying the
// alias twice, or to a type that is already a pointer, before the pointer to
// pointer can go anywhere.
SCENARIO("non_owning_t rejects a pointee that is itself a pointer", "[non_owning_t]")
{
  GIVEN("a raw pointer type")
  {
    THEN("the alias does not name a type")
    {
      STATIC_REQUIRE(!non_owning_t_test::well_formed_c<int*>);
      STATIC_REQUIRE(!non_owning_t_test::well_formed_c<const int*>);
      STATIC_REQUIRE(!non_owning_t_test::well_formed_c<non_owning_t_test::widget*>);
    }
  }

  GIVEN("the alias applied twice")
  {
    THEN("that is rejected too, which is the spelling a user would actually reach for")
    {
      STATIC_REQUIRE(!non_owning_t_test::well_formed_c<arude::non_owning_t<int>>);
    }
  }
}

SCENARIO("a non_owning pointer observes its pointee", "[non_owning_t]")
{
  GIVEN("an object and a non-owning pointer to it")
  {
    auto owned = non_owning_t_test::widget{42};
    auto* const view = arude::non_owning_t<non_owning_t_test::widget>{&owned};

    THEN("the object is readable through the pointer")
    {
      REQUIRE(view->value() == 42);
      REQUIRE((*view).value() == 42);
    }

    WHEN("the pointer is copied")
    {
      auto* const copy = view;

      THEN("the address is copied, not the object: both point at the one object")
      {
        REQUIRE(copy == view);
        REQUIRE(&*copy == &*view);
        REQUIRE(&*copy == &owned);
      }
    }

    WHEN("the pointer goes out of scope")
    {
      THEN("the pointee is untouched, because the pointer owns nothing")
      {
        {
          auto* const scoped_view = arude::non_owning_t<non_owning_t_test::widget>{&owned};
          REQUIRE(scoped_view->value() == 42);
        }
        REQUIRE(owned.value() == 42);
      }
    }
  }

  GIVEN("a const object and a non-owning pointer to it")
  {
    const auto owned = non_owning_t_test::widget{7};
    const auto* const view = arude::non_owning_t<const non_owning_t_test::widget>{&owned};

    THEN("the pointer is read-only")
    {
      STATIC_REQUIRE(std::is_same_v<std::remove_pointer_t<decltype(view)>, const non_owning_t_test::widget>);
      REQUIRE(view->value() == 7);
    }
  }
}

SCENARIO("a non_owning pointer stays usable at compile time", "[non_owning_t]")
{
  GIVEN("a constant expression context")
  {
    static constexpr auto owned = non_owning_t_test::widget{7};

    THEN("a non-owning pointer can be formed and read through there")
    {
      STATIC_REQUIRE(arude::non_owning_t<const non_owning_t_test::widget>{&owned}->value() == 7);
    }
  }
}
