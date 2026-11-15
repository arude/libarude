///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// The properties arude::type_key must have in every build, as Catch2
/// scenarios.
///
/// A header of test cases rather than a translation unit, because the same
/// cases have to run against both implementations: test/test_type_key.cpp
/// includes it into the main binary, where the build has RTTI, and
/// test/config/test_type_key_no_rtti.cpp into an executable built without.
/// Written once, the two cannot drift apart, and code that compiles against one
/// key behaves the same against the other. Include it into exactly one
/// translation unit per binary, or Catch2 sees every scenario twice.
///
#pragma once

#include "arude/type_key.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <functional>
#include <string>
#include <unordered_map>

namespace type_key_test
{

///
/// A class type, to key alongside the fundamental ones.
///
class widget final
{};

///
/// A second class type, so two user-defined types can be told apart.
///
struct gadget
{};

} // namespace type_key_test

SCENARIO("type_key is equal for one type", "[type_key]")
{
  GIVEN("the key of one type, asked for twice")
  {
    THEN("the two keys are equal")
    {
      REQUIRE(arude::type_key<int>() == arude::type_key<int>());
      REQUIRE(arude::type_key<type_key_test::widget>() == arude::type_key<type_key_test::widget>());
    }

    THEN("they hash alike, so an unordered container finds one from the other")
    {
      const auto hash = std::hash<arude::type_key_t>{};

      REQUIRE(hash(arude::type_key<int>()) == hash(arude::type_key<int>()));
    }
  }
}

SCENARIO("type_key tells different types apart", "[type_key]")
{
  GIVEN("pairs of types that differ")
  {
    THEN("their keys are unequal")
    {
      REQUIRE(arude::type_key<int>() != arude::type_key<unsigned int>());
      REQUIRE(arude::type_key<int>() != arude::type_key<long>());
      REQUIRE(arude::type_key<int>() != arude::type_key<int*>());
      REQUIRE(arude::type_key<char>() != arude::type_key<signed char>());
      REQUIRE(arude::type_key<type_key_test::widget>() != arude::type_key<type_key_test::gadget>());
    }
  }
}

SCENARIO("type_key keys an unordered_map", "[type_key]")
{
  GIVEN("a map holding an entry for each of two types")
  {
    auto names = std::unordered_map<arude::type_key_t, std::string>{};
    names.insert_or_assign(arude::type_key<int>(), "int");
    names.insert_or_assign(arude::type_key<type_key_test::widget>(), "widget");

    WHEN("each is looked up with a key made afresh")
    {
      const auto int_entry = names.find(arude::type_key<int>());
      const auto widget_entry = names.find(arude::type_key<type_key_test::widget>());

      THEN("each finds its own entry")
      {
        REQUIRE(int_entry != names.end());
        REQUIRE(int_entry->second == "int");
        REQUIRE(widget_entry != names.end());
        REQUIRE(widget_entry->second == "widget");
      }
    }

    WHEN("a type that was never inserted is looked up")
    {
      const auto entry = names.find(arude::type_key<type_key_test::gadget>());

      THEN("the lookup misses")
      {
        REQUIRE(entry == names.end());
      }
    }

    WHEN("a type already present is inserted again")
    {
      names.insert_or_assign(arude::type_key<int>(), "still int");

      THEN("its entry is replaced rather than a second one added")
      {
        REQUIRE(names.size() == std::size_t{2});
        REQUIRE(names.at(arude::type_key<int>()) == "still int");
      }
    }
  }
}

// What typeid does, measured with clang 21 and libstdc++: it drops a reference
// and then the top-level cv-qualifiers, and nothing deeper. The cv-qualifiers on
// an array's elements count as top-level, because an array of const T is
// itself a const type. The key without RTTI is held to the same answers.
SCENARIO("type_key treats qualifiers and references as typeid does", "[type_key]")
{
  GIVEN("cv-qualified types")
  {
    THEN("they share the key of the unqualified type")
    {
      REQUIRE(arude::type_key<const int>() == arude::type_key<int>());
      REQUIRE(arude::type_key<volatile int>() == arude::type_key<int>());
      REQUIRE(arude::type_key<const volatile int>() == arude::type_key<int>());
      REQUIRE(arude::type_key<const type_key_test::widget>() == arude::type_key<type_key_test::widget>());
    }
  }

  GIVEN("reference types")
  {
    THEN("they share the key of the type referred to, qualifiers and all dropped")
    {
      REQUIRE(arude::type_key<int&>() == arude::type_key<int>());
      REQUIRE(arude::type_key<int&&>() == arude::type_key<int>());
      REQUIRE(arude::type_key<const int&>() == arude::type_key<int>());
      REQUIRE(arude::type_key<const volatile type_key_test::widget&&>() == arude::type_key<type_key_test::widget>());
    }
  }

  GIVEN("qualifiers below the top level")
  {
    THEN("a const pointee is a different type, while a const pointer is not")
    {
      REQUIRE(arude::type_key<const int*>() != arude::type_key<int*>());
      REQUIRE(arude::type_key<int* const>() == arude::type_key<int*>());
    }
  }

  GIVEN("an array of const elements")
  {
    THEN("it shares the key of the array of unqualified elements")
    {
      // The array types are what is under test, so std::array would miss the point.
      // NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
      REQUIRE(arude::type_key<const int[3]>() == arude::type_key<int[3]>());
      REQUIRE(arude::type_key<int[3]>() != arude::type_key<int[4]>());
      // NOLINTEND(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
    }
  }
}
