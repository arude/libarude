///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///

#include "arude/type_traits.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <type_traits>
#include <utility>

namespace type_traits_test
{

///
/// Declared and never defined, so a test can pass a type that has no
/// definition anywhere in the program.
///
class incomplete;

///
/// Declared here and defined below the template that holds one, which is the
/// situation dependent_t exists for.
///
class late_type;

///
/// A class template holding, and reaching through, a type that is incomplete
/// where the template is parsed.
/// Written without dependent_t the member's type would be a non-dependent name
/// and the call in value() a non-dependent expression, so a compiler is free to
/// check it here, against a late_type that has no members yet.
///
/// \tparam T Type the member's type is made to depend on.
///
template<typename T>
class holder final
{
public:
  explicit holder(std::shared_ptr<arude::dependent_t<T, late_type>> target);

  [[nodiscard]] auto value() const -> int;

private:
  std::shared_ptr<arude::dependent_t<T, late_type>> target_;
};

///
/// A small stateful type, completed after the template above rather than
/// before it.
///
class late_type final
{
public:
  constexpr explicit late_type(int value);

  [[nodiscard]] constexpr auto value() const -> int;

private:
  int value_ = 0;
};

///
///
template<typename T>
holder<T>::holder(std::shared_ptr<arude::dependent_t<T, late_type>> target)
  : target_{std::move(target)}
{
}

///
///
template<typename T>
auto holder<T>::value() const -> int
{
  return target_->value();
}

///
///
constexpr late_type::late_type(const int value)
  : value_{value}
{
}

///
///
constexpr auto late_type::value() const -> int
{
  return value_;
}

} // namespace type_traits_test

SCENARIO("dependent_t yields its second argument unchanged", "[type_traits]")
{
  GIVEN("fundamental and class types")
  {
    THEN("the alias is that type and nothing else")
    {
      STATIC_REQUIRE(std::is_same_v<arude::dependent_t<int, char>, char>);
      STATIC_REQUIRE(std::is_same_v<arude::dependent_t<int, void>, void>);
      STATIC_REQUIRE(std::is_same_v<arude::dependent_t<int, type_traits_test::late_type>, type_traits_test::late_type>);
    }
  }

  GIVEN("types the standard traits are known to decay")
  {
    THEN("nothing is decayed: no qualifier, reference, extent or signature is lost")
    {
      STATIC_REQUIRE(std::is_same_v<arude::dependent_t<int, const volatile char>, const volatile char>);
      STATIC_REQUIRE(std::is_same_v<arude::dependent_t<int, char&>, char&>);
      STATIC_REQUIRE(std::is_same_v<arude::dependent_t<int, char&&>, char&&>);
      STATIC_REQUIRE(std::is_same_v<arude::dependent_t<int, char* const>, char* const>);
      // The array is the point: the alias must hand it back undecayed.
      // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
      STATIC_REQUIRE(std::is_same_v<arude::dependent_t<int, char[4]>, char[4]>);
      STATIC_REQUIRE(std::is_same_v<arude::dependent_t<int, char(double)>, char(double)>);
    }
  }

  GIVEN("the alias applied twice")
  {
    THEN("it composes, because each application is transparent")
    {
      STATIC_REQUIRE(std::is_same_v<arude::dependent_t<int, arude::dependent_t<char, double>>, double>);
    }
  }
}

// The first argument is there to be spelled and not to be used, so a type that
// cannot be used at all still has to work.
SCENARIO("dependent_t ignores its first argument", "[type_traits]")
{
  GIVEN("first arguments that share nothing with each other")
  {
    THEN("the alias is the same type regardless")
    {
      STATIC_REQUIRE(std::is_same_v<arude::dependent_t<void, int>, int>);
      STATIC_REQUIRE(std::is_same_v<arude::dependent_t<double&, int>, int>);
      // The array is the point: the first argument is only spelled, never touched.
      // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
      STATIC_REQUIRE(std::is_same_v<arude::dependent_t<char[4], int>, int>);
      STATIC_REQUIRE(std::is_same_v<arude::dependent_t<type_traits_test::late_type, int>, int>);
    }
  }

  GIVEN("a first argument that is never defined")
  {
    THEN("the alias still names its second argument, so completeness is not required")
    {
      STATIC_REQUIRE(std::is_same_v<arude::dependent_t<type_traits_test::incomplete, int>, int>);
    }
  }
}

// The point of the alias: naming a type is not using it, and neither argument
// has to be complete for the name to be formed.
SCENARIO("dependent_t names an incomplete type", "[type_traits]")
{
  GIVEN("a type that is never defined")
  {
    THEN("the alias names it, and names it as incomplete")
    {
      STATIC_REQUIRE(
        std::is_same_v<arude::dependent_t<int, type_traits_test::incomplete>, type_traits_test::incomplete>);
      STATIC_REQUIRE(std::is_class_v<arude::dependent_t<int, type_traits_test::incomplete>>);
    }
  }

  GIVEN("a pointer to the alias for that type")
  {
    THEN("it is exactly a pointer to the type itself")
    {
      STATIC_REQUIRE(
        std::is_same_v<arude::dependent_t<int, type_traits_test::incomplete>*, type_traits_test::incomplete*>);
    }
  }
}

// This case is the reason the alias exists, and the one a plain typedef cannot
// carry: that it compiles at all is half the assertion.
SCENARIO("dependent_t defers a member access to the point of instantiation", "[type_traits]")
{
  GIVEN("a class template written against a type that was incomplete there")
  {
    auto target = std::make_shared<type_traits_test::late_type>(42);
    const auto held = type_traits_test::holder<int>{target};

    THEN("the member access is checked on instantiation, where the type is complete")
    {
      REQUIRE(held.value() == 42);
    }

    THEN("the member's type is the one the alias named, with the pointer shared")
    {
      STATIC_REQUIRE(std::is_same_v<arude::dependent_t<int, type_traits_test::late_type>, type_traits_test::late_type>);
      REQUIRE(target.use_count() == 2);
    }
  }
}
