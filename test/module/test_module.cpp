///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// Guards the re-export list in src/arude.cppm. Nothing here includes an arude
/// header, deliberately: a module consumer must not have to, and a name missing
/// from the export list fails to compile here and nowhere else.
///

// Includes before imports, per docs/cpp-conventions.md. GCC rejects the other
// order, and Catch2 is a header library. Note that no arude header appears
// here: that a module consumer needs none is the thing being tested.
#include "test/module/header_consumer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <exception>
#include <format>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

import arude;

namespace module_test
{

///
/// Derived from the exported base, which is the only way to find out whether
/// the class itself came across rather than just the alias naming it.
///
class widget final : public arude::noncopyable
{};

} // namespace module_test

SCENARIO("the module exports the public interface", "[module]")
{
  GIVEN("nothing imported but arude")
  {
    THEN("the type aliases and concepts are reachable")
    {
      STATIC_REQUIRE(std::is_same_v<arude::exception_string_t, std::string>);
      STATIC_REQUIRE(arude::exception_user_data<int>);
      STATIC_REQUIRE(arude::exception_user_data<void>);
    }

    THEN("type_name is reachable and still constexpr")
    {
      STATIC_REQUIRE(arude::type_name<int>() == "int");
    }

    THEN("hello_world is reachable, so out-of-line definitions link")
    {
      REQUIRE(arude::hello_world(42) == 42);
    }
  }

  // A base class is only exported usefully if it can be derived from, which
  // needs the class reachable and not merely the alias that names it.
  GIVEN("a type derived from the exported noncopyable")
  {
    THEN("the copy members are gone and the move members are not")
    {
      STATIC_REQUIRE(!std::is_copy_constructible_v<module_test::widget>);
      STATIC_REQUIRE(!std::is_copy_assignable_v<module_test::widget>);
      STATIC_REQUIRE(std::is_move_constructible_v<module_test::widget>);
      STATIC_REQUIRE(std::is_move_assignable_v<module_test::widget>);
    }
  }
}

SCENARIO("the module exports the exception types", "[module]")
{
  GIVEN("an exception built through the module")
  {
    const auto ex = arude::exception{"module failure"};

    THEN("the accessors work")
    {
      REQUIRE(ex.str() == "module failure");
      REQUIRE(ex.stack().empty() != arude::stacktrace_available);
      REQUIRE(std::string_view{ex.where().file_name()}.contains("test_module.cpp"));
    }

    THEN("it is an exception_base, so the base type came across too")
    {
      STATIC_REQUIRE(std::is_base_of_v<arude::exception_base, decltype(ex)>);
    }
  }

  GIVEN("an exception carrying a payload")
  {
    const auto ex = arude::exception{"failed", 42};

    THEN("the class template and its deduction guide came across")
    {
      STATIC_REQUIRE(std::is_same_v<decltype(ex)::user_data_t, int>);
      REQUIRE(ex.data() == 42);
    }
  }
}

// The point of the whole arrangement: the specializations live in the module
// purview, so formatting works without the consumer including anything. If they
// were left in the global module fragment a conforming compiler would discard
// them, and this is what catches that.
SCENARIO("the module carries the formatter specializations", "[module]")
{
  GIVEN("an arude exception")
  {
    const auto ex = arude::exception{"formatted"};

    THEN("it is formattable without including a single arude header")
    {
      STATIC_REQUIRE(std::formattable<arude::exception_base, char>);
      REQUIRE(std::format("{}", static_cast<const arude::exception_base&>(ex)).contains("formatted"));
    }

    THEN("the source_location formatter came across as well")
    {
      REQUIRE(std::format("{}", ex.where()).contains("test_module.cpp"));
    }
  }
}

SCENARIO("the module exports both exception_report overloads", "[module]")
{
  GIVEN("an exception in flight")
  {
    auto report = arude::exception_string_t{};

    try
    {
      throw arude::exception{"reported"};
    }
    catch(...)
    {
      report = arude::exception_report();
    }

    THEN("the no-argument overload is reachable")
    {
      REQUIRE(report.contains("reported"));
    }
  }

  GIVEN("a captured exception")
  {
    auto eptr = std::exception_ptr{};

    try
    {
      throw arude::exception{"captured"};
    }
    catch(...)
    {
      eptr = std::current_exception();
    }

    THEN("the exception_ptr overload came from the same using-declaration")
    {
      REQUIRE(arude::exception_report(eptr).contains("captured"));
    }
  }
}

// A module consumer and a header consumer must agree on what arude::exception
// is. They do because the headers are included in the global module fragment,
// so the type keeps global-module linkage. Were it attached to the module, this
// throw would walk straight past the typed handler into catch(...).
SCENARIO("a throw from a header consumer is caught by an importer", "[module]")
{
  GIVEN("an exception thrown by a translation unit that included the header")
  {
    auto caught_typed = false;
    auto message = arude::exception_string_t{};

    try
    {
      throw_from_header_translation_unit();
    }
    catch(const arude::exception_base& ex)
    {
      caught_typed = true;
      message = ex.str();
    }
    catch(...) // NOLINT(bugprone-empty-catch): reaching here is the failure.
    {
    }

    THEN("the typed handler catches it, so both see one type")
    {
      REQUIRE(caught_typed);
      REQUIRE(message == "thrown from a header consumer");
    }
  }
}
