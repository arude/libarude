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
#include <memory>
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

///
/// Declared here rather than in a header, so the enum a module consumer
/// reflects on is one the module has never seen.
///
enum class colour
{
  red,
  green,
  blue
};

///
/// Derived from the exported alias template, which is the only way to find out
/// whether the class template it names came across too.
/// The implementation is completed in this translation unit rather than in a
/// second one: what is under test here is the export, not the hiding, which
/// test/test_pimpl_owner.cpp covers.
///
class boxed final : private arude::pimpl_owner<boxed>
{
public:
  class impl_t;

  boxed();

  [[nodiscard]] auto value() const -> int;
};

class boxed::impl_t final
{
public:
  [[nodiscard]] auto value() const -> int;
};

///
///
auto boxed::impl_t::value() const -> int
{
  return 7;
}

///
///
boxed::boxed()
  : pimpl_owner{std::make_unique<impl_t>()}
{
}

///
///
auto boxed::value() const -> int
{
  return impl().value();
}

} // namespace module_test

SCENARIO("the module exports the public interface", "[module]")
{
  GIVEN("nothing imported but arude")
  {
    THEN("the type aliases and concepts are reachable")
    {
      STATIC_REQUIRE(std::is_same_v<arude::exception_string_t, std::string>);
      STATIC_REQUIRE(arude::exception_user_data_c<int>);
      STATIC_REQUIRE(arude::exception_user_data_c<void>);
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

  // pimpl_owner is exported as an alias template, so this asks the same
  // question one step further along: an alias that named nothing reachable
  // would fail to serve as a base here.
  GIVEN("a type derived from the exported pimpl_owner")
  {
    const auto boxed = module_test::boxed{};

    THEN("the storage works and the type came out move-only")
    {
      REQUIRE(boxed.value() == 7);
      STATIC_REQUIRE(!std::is_copy_constructible_v<module_test::boxed>);
      STATIC_REQUIRE(std::is_move_constructible_v<module_test::boxed>);
    }
  }
}

// magic_enum is a dependency of the library, not of its consumers: a name that
// reached this file through <magic_enum/magic_enum.hpp> would pass here while
// arude::enum_name did not exist at all. Nothing here includes that header, so
// every name below has to have come through the module.
SCENARIO("the module exports the enum reflection", "[module]")
{
  using module_test::colour;

  GIVEN("an enumeration declared in the importing translation unit")
  {
    THEN("the names come across and are still constexpr")
    {
      STATIC_REQUIRE(arude::enum_name(colour::green) == "green");
      STATIC_REQUIRE(arude::enum_type_name<colour>() == "colour");
      STATIC_REQUIRE(arude::enum_count<colour>() == 3);
    }

    THEN("the sequence accessors come across")
    {
      STATIC_REQUIRE(arude::enum_value<colour>(0) == colour::red);
      STATIC_REQUIRE(arude::enum_values<colour>().size() == 3);
      STATIC_REQUIRE(arude::enum_names<colour>().front() == "red");
      STATIC_REQUIRE(arude::enum_entries<colour>().front().second == "red");
      STATIC_REQUIRE(arude::enum_index(colour::blue).value() == 2);
      STATIC_REQUIRE(arude::enum_integer(colour::blue) == 2);
    }

    THEN("the whole enum_cast overload set came from the one using-declaration")
    {
      STATIC_REQUIRE(arude::enum_cast<colour>(1).value() == colour::green);
      STATIC_REQUIRE(arude::enum_cast<colour>("green").value() == colour::green);
      STATIC_REQUIRE(!arude::enum_cast<colour>("GREEN").has_value());
    }

    THEN("case_insensitive is reachable and reaches the same overload")
    {
      STATIC_REQUIRE(arude::enum_cast<colour>("GREEN", arude::case_insensitive).value() == colour::green);
      STATIC_REQUIRE(arude::enum_contains<colour>("GREEN", arude::case_insensitive));
    }

    THEN("enum_contains rejects a value outside the enumeration")
    {
      STATIC_REQUIRE(arude::enum_contains(colour::red));
      STATIC_REQUIRE(!arude::enum_contains(static_cast<colour>(99)));
    }

    THEN("the concept came across")
    {
      STATIC_REQUIRE(arude::enum_c<colour>);
      STATIC_REQUIRE(!arude::enum_c<int>);
    }

    THEN("enum_cast_throw came across, both overloads")
    {
      STATIC_REQUIRE(arude::enum_cast_throw<colour>(1) == colour::green);
      REQUIRE(arude::enum_cast_throw<colour>("green") == colour::green);
      REQUIRE_THROWS_AS(arude::enum_cast_throw<colour>(99), arude::exception_base);
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
