///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///

#include "arude/type_name.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string_view>
#include <vector>

namespace type_name_test
{

class my_class final
{};

struct my_struct
{};

enum class my_enum : std::uint8_t
{
  value
};

} // namespace type_name_test

// The signature parsing is the part that breaks on a toolchain bump, and only
// one of the three branches is ever compiled. Feeding captured signatures from
// all three compilers to the parser tests every branch on every platform.
SCENARIO("extract_type_name reads a compiler signature", "[type_name]")
{
  GIVEN("a clang signature")
  {
    const auto signature = std::string_view{"std::string_view arude::type_name() [T = int]"};

    WHEN("it is parsed")
    {
      const auto name = arude::detail::extract_type_name(signature, "T = ", "]");

      THEN("the type name comes back")
      {
        REQUIRE(name == "int");
      }
    }
  }

  GIVEN("a gcc signature, whose alias return type appends a semicolon clause")
  {
    const auto signature = std::string_view{
      "constexpr std::string_view arude::type_name() [with T = int; std::string_view = std::basic_string_view<char>]"};

    WHEN("it is parsed")
    {
      const auto name = arude::detail::extract_type_name(signature, "T = ", "]");

      THEN("the clause is not included in the name")
      {
        REQUIRE(name == "int");
      }
    }
  }

  GIVEN("a gcc signature with no semicolon clause, as a non-alias return type produces")
  {
    const auto signature = std::string_view{"const char* arude::type_name() [with T = int]"};

    WHEN("it is parsed")
    {
      const auto name = arude::detail::extract_type_name(signature, "T = ", "]");

      THEN("the closing bracket still ends the name")
      {
        REQUIRE(name == "int");
      }
    }
  }

  GIVEN("a signature for a type whose own name contains brackets")
  {
    const auto signature = std::string_view{"std::string_view arude::type_name() [T = int[3]]"};

    WHEN("it is parsed")
    {
      const auto name = arude::detail::extract_type_name(signature, "T = ", "]");

      THEN("the inner brackets are kept")
      {
        REQUIRE(name == "int[3]");
      }
    }
  }

  GIVEN("an msvc signature")
  {
    const auto signature = std::string_view{
      "class std::basic_string_view<char,struct std::char_traits<char> > __cdecl arude::type_name<int>(void)"};

    WHEN("it is parsed")
    {
      const auto name = arude::detail::extract_type_name(signature, "type_name<", ">(void)");

      THEN("the return type is not mistaken for the argument")
      {
        REQUIRE(name == "int");
      }
    }
  }

  GIVEN("an msvc signature for a nested template, closing its brackets apart")
  {
    const auto signature =
      std::string_view{"... __cdecl arude::type_name<class std::vector<int,class std::allocator<int> > >(void)"};

    WHEN("it is parsed")
    {
      const auto name = arude::detail::extract_type_name(signature, "type_name<", ">(void)");

      THEN("the outer bracket is cut off without leaving a trailing space")
      {
        REQUIRE(name == "class std::vector<int,class std::allocator<int> >");
      }
    }
  }

  GIVEN("a signature in a shape the parser does not know")
  {
    const auto signature = std::string_view{"a signature from some future compiler"};

    WHEN("it is parsed")
    {
      const auto name = arude::detail::extract_type_name(signature, "T = ", "]");

      THEN("the whole signature comes back, so the failure is visible rather than plausible")
      {
        REQUIRE(name == signature);
      }
    }
  }

  // The prefix is what the parser looks for first. Finding it and then failing
  // is a separate path from not finding it at all, and it is the one that would
  // read past the end of the name if it did not bail out.
  GIVEN("a signature carrying the prefix but neither a semicolon nor a terminator")
  {
    const auto signature = std::string_view{"void arude::type_name() T = int"};

    WHEN("it is parsed")
    {
      const auto name = arude::detail::extract_type_name(signature, "T = ", "]");

      THEN("the whole signature comes back rather than a truncated name")
      {
        REQUIRE(name == signature);
      }
    }
  }

  GIVEN("a signature whose terminator sits before the prefix")
  {
    const auto signature = std::string_view{"] void arude::type_name() T = int"};

    WHEN("it is parsed")
    {
      const auto name = arude::detail::extract_type_name(signature, "T = ", "]");

      THEN("the whole signature comes back rather than a reversed range")
      {
        REQUIRE(name == signature);
      }
    }
  }
}

SCENARIO("strip_elaborated_specifier normalises msvc type names", "[type_name]")
{
  GIVEN("names carrying a specifier")
  {
    THEN("the specifier is removed")
    {
      REQUIRE(arude::detail::strip_elaborated_specifier("class my_type") == "my_type");
      REQUIRE(arude::detail::strip_elaborated_specifier("struct my_type") == "my_type");
      REQUIRE(arude::detail::strip_elaborated_specifier("enum my_type") == "my_type");
      REQUIRE(arude::detail::strip_elaborated_specifier("union my_type") == "my_type");
    }
  }

  GIVEN("names carrying none")
  {
    THEN("they are returned unchanged")
    {
      REQUIRE(arude::detail::strip_elaborated_specifier("my_type") == "my_type");
      REQUIRE(arude::detail::strip_elaborated_specifier("int") == "int");
      REQUIRE(arude::detail::strip_elaborated_specifier("").empty());
    }
  }

  GIVEN("a name merely beginning with the letters of a specifier")
  {
    THEN("nothing is stripped, because the trailing space is part of the match")
    {
      REQUIRE(arude::detail::strip_elaborated_specifier("classy") == "classy");
      REQUIRE(arude::detail::strip_elaborated_specifier("enumerator") == "enumerator");
    }
  }
}

SCENARIO("type_name names fundamental types", "[type_name]")
{
  GIVEN("types every compiler spells identically")
  {
    THEN("the name matches exactly")
    {
      REQUIRE(arude::type_name<int>() == "int");
      REQUIRE(arude::type_name<bool>() == "bool");
      REQUIRE(arude::type_name<char>() == "char");
      REQUIRE(arude::type_name<double>() == "double");
      REQUIRE(arude::type_name<void>() == "void");
    }
  }
}

SCENARIO("type_name names user-defined types", "[type_name]")
{
  GIVEN("a class, a struct, and an enum")
  {
    THEN("each is named with its namespace and without an elaborated specifier")
    {
      REQUIRE(arude::type_name<type_name_test::my_class>() == "type_name_test::my_class");
      REQUIRE(arude::type_name<type_name_test::my_struct>() == "type_name_test::my_struct");
      REQUIRE(arude::type_name<type_name_test::my_enum>() == "type_name_test::my_enum");
    }
  }
}

// Spelling differs between toolchains by design, so these assert the parts
// that must appear rather than the whole name.
SCENARIO("type_name names compound types", "[type_name]")
{
  GIVEN("a pointer type")
  {
    const auto name = arude::type_name<int*>();

    THEN("both the pointee and the star appear")
    {
      REQUIRE(name.contains("int"));
      REQUIRE(name.contains('*'));
    }
  }

  GIVEN("a const reference type")
  {
    const auto name = arude::type_name<const int&>();

    THEN("constness and the reference appear")
    {
      REQUIRE(name.contains("const"));
      REQUIRE(name.contains("int"));
      REQUIRE(name.contains('&'));
    }
  }

  GIVEN("a template specialisation")
  {
    const auto name = arude::type_name<std::vector<int>>();

    THEN("the template and its argument appear")
    {
      REQUIRE(name.contains("vector"));
      REQUIRE(name.contains("int"));
    }
  }
}

SCENARIO("type_name distinguishes types", "[type_name]")
{
  GIVEN("two different types")
  {
    THEN("their names differ")
    {
      REQUIRE(arude::type_name<int>() != arude::type_name<unsigned int>());
      REQUIRE(arude::type_name<type_name_test::my_class>() != arude::type_name<type_name_test::my_struct>());
      REQUIRE(arude::type_name<int>() != arude::type_name<int*>());
    }
  }

  GIVEN("the same type asked for twice")
  {
    THEN("the name is stable and points at the same static storage")
    {
      REQUIRE(arude::type_name<int>() == arude::type_name<int>());
      REQUIRE(arude::type_name<int>().data() == arude::type_name<int>().data());
    }
  }
}

SCENARIO("type_name is usable at compile time", "[type_name]")
{
  GIVEN("a constant expression context")
  {
    THEN("the name is available to static_assert")
    {
      STATIC_REQUIRE(arude::type_name<int>() == "int");
      STATIC_REQUIRE(arude::type_name<type_name_test::my_class>() == "type_name_test::my_class");
      STATIC_REQUIRE(!arude::type_name<double>().empty());
    }
  }
}
