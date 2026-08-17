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

    THEN("dependent_t is reachable and still transparent")
    {
      STATIC_REQUIRE(std::is_same_v<arude::dependent_t<int, module_test::widget>, module_test::widget>);
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

// Guarded the way src/arude.cppm guards the export list itself: with the
// configuration support declined there is nothing here to reach. The macro
// arrives from the library target, so this file still includes no arude header.
#if !(defined ARUDE_NO_CONFIG)

namespace module_test
{

///
/// A module consumer's own configuration, version 1.
/// Declared here rather than taken from the library, because the library ships
/// no configuration type: what has to work is the versioning machinery applied
/// to a type the module has never seen, which is what a consumer actually has.
///
struct app_config_v1
{
  ///
  /// The version this type is.
  ///
  static constexpr auto config_version = arude::config::version_t{1};

  ///
  /// The version carried in the text.
  ///
  arude::config::version_t version = config_version;

  ///
  /// Name this configuration is known by.
  ///
  std::string name = "module";

  ///
  /// Opaque payload, written as base64 text.
  ///
  arude::config::binary secret;
};

///
/// The same configuration, version 2.
///
struct app_config_v2
{
  ///
  /// The version before this one, which migration walks back through.
  ///
  using previous_t = app_config_v1;

  ///
  /// \see app_config_v1::config_version
  ///
  static constexpr auto config_version = arude::config::version_t{2};

  ///
  /// \see app_config_v1::version
  ///
  arude::config::version_t version = config_version;

  ///
  /// \see app_config_v1::name
  ///
  std::string name = "module";

  ///
  /// Host the configured service is reached at. New in version 2.
  ///
  std::string endpoint = "localhost";

  ///
  /// \see app_config_v1::secret
  ///
  arude::config::binary secret;
};

///
/// The version this consumer builds against.
///
using app_config_t = app_config_v2;

///
/// Produces a version 2 configuration from a version 1 one.
/// Found by argument-dependent lookup from arude::config::migrate, which is
/// the whole mechanism: the step lives beside the types, in the consumer's
/// namespace, and the library never sees it declared.
///
/// \param val Configuration to upgrade. Not retained.
/// \return The same configuration as version 2.
///
[[nodiscard]] constexpr auto upgrade(const app_config_v1& val) -> app_config_v2;

///
/// Produces a version 1 configuration from a version 2 one.
/// \param val Configuration to downgrade. Not retained.
/// \return The same configuration as version 1, less what version 1 cannot hold.
///
[[nodiscard]] constexpr auto downgrade(const app_config_v2& val) -> app_config_v1;

///
///
constexpr auto upgrade(const app_config_v1& val) -> app_config_v2
{
  return app_config_v2{.name = val.name, .secret = val.secret};
}

///
///
constexpr auto downgrade(const app_config_v2& val) -> app_config_v1
{
  return app_config_v1{.name = val.name, .secret = val.secret};
}

} // namespace module_test

SCENARIO("the module exports the configuration interface", "[module][config]")
{
  GIVEN("nothing imported but arude")
  {
    THEN("the concepts and the version arithmetic came across")
    {
      STATIC_REQUIRE(arude::config::configuration_c<module_test::app_config_t>);
      STATIC_REQUIRE(arude::config::endpoint_c<arude::config::endpoint_toml>);
      STATIC_REQUIRE(std::is_same_v<module_test::app_config_t, module_test::app_config_v2>);
      STATIC_REQUIRE(arude::config::version_of<module_test::app_config_v2>() == 2);
    }

    THEN("base64 came across and is still constexpr")
    {
      STATIC_REQUIRE(arude::config::base64_encoded_size(3) == 4);
    }

    THEN("a migration runs, so the steps came across with the types")
    {
      REQUIRE(arude::config::migrate<module_test::app_config_v2>(module_test::app_config_v1{}).version == 2);
    }
  }
}

// The point of the anchors in src/arude.cppm. Both specializations are found
// by name lookup here rather than inside the module, so a conforming compiler
// that pruned them would fail on this and nowhere else.
SCENARIO("the module carries the configuration specializations", "[module][config]")
{
  GIVEN("a configuration holding a binary payload")
  {
    auto payload = arude::config::binary{};
    payload.base64("Zm9v");

    const auto text = arude::config::to_toml(module_test::app_config_t{.secret = payload});

    THEN("the reflector stored it as base64, and reads it back")
    {
      REQUIRE(text.contains("Zm9v"));
      REQUIRE(arude::config::from_toml<module_test::app_config_t>(text).secret == payload);
    }
  }

  GIVEN("a manager error")
  {
    THEN("it formats as its name, which is what the formatter anchor is for")
    {
      REQUIRE(std::format("{}", arude::config::config_manager::error_t::io_error) == "io_error");
      STATIC_REQUIRE(arude::enum_name(arude::config::config_manager::error_t::not_found) == "not_found");
    }
  }
}

#endif // #if !(defined ARUDE_NO_CONFIG)
