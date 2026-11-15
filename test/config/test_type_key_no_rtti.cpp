///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///

// Built into its own executable without RTTI, with -fno-rtti or /GR- set by
// CMake rather than here, so arude/type_key.hpp takes the branch the main test
// binary never compiles. It cannot join that binary: the two branches define
// arude::type_key() differently, and two definitions of one inline function in
// a program is an ODR violation.

#include "arude/type_key.hpp"
#include "test/helpers/type_key_scenarios.hpp"

#include <catch2/catch_test_macros.hpp>

#include <concepts>
#include <type_traits>

// Checked on the compiler's own macros as well as on the header's, so a build
// that kept RTTI fails here even if the header's detection were to go wrong.
#if (defined __cpp_rtti) || (defined __GXX_RTTI) || (defined _CPP_RTTI)
  #error "test_type_key_no_rtti.cpp must be built without RTTI; CMakeLists.txt passes -fno-rtti or /GR-."
#endif // #if (defined __cpp_rtti) || (defined __GXX_RTTI) || (defined _CPP_RTTI)

#if (defined ARUDE_TYPE_KEY_RTTI)
  #error "arude/type_key.hpp took the RTTI branch in a build without RTTI."
#endif // #if (defined ARUDE_TYPE_KEY_RTTI)

// The pointer wrapper, and not std::type_index, is what was compiled.
static_assert(std::is_class_v<arude::type_key_t>);
static_assert(std::is_constructible_v<arude::type_key_t, const void*>);
static_assert(!std::is_convertible_v<const void*, arude::type_key_t>, "The address constructor is explicit.");
static_assert(requires(const arude::type_key_t& key) {
  { key.id() } -> std::same_as<const void*>;
});

// The storage has to be writable, or identical-data folding may merge two
// types' bytes and hand them one key; see arude/type_key.hpp.
static_assert(!std::is_const_v<decltype(arude::detail::type_key_storage<int>)>);

namespace
{

///
/// Declared and never defined, which only the branch without RTTI can key.
///
class never_defined;

} // namespace

SCENARIO("type_key without RTTI is the address of a per-type object", "[type_key][no_rtti]")
{
  GIVEN("the key of a type")
  {
    THEN("it carries the address of that type's storage")
    {
      REQUIRE(arude::type_key<int>().id() == &arude::detail::type_key_storage<int>);
      REQUIRE(arude::type_key<type_key_test::widget>().id() == &arude::detail::type_key_storage<type_key_test::widget>);
    }
  }

  GIVEN("the key of a qualified reference type")
  {
    THEN("it carries the address of the unqualified type's storage")
    {
      REQUIRE(arude::type_key<const int&>().id() == &arude::detail::type_key_storage<int>);
    }
  }

  GIVEN("a type that is declared but never defined")
  {
    THEN("it can still be keyed, and apart from the types that are")
    {
      REQUIRE(arude::type_key<never_defined>() == arude::type_key<never_defined>());
      REQUIRE(arude::type_key<never_defined>() != arude::type_key<type_key_test::widget>());
    }
  }
}
