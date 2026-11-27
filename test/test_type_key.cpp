///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///

// The main binary is built with RTTI, so this covers the std::type_index
// branch. test/config/test_type_key_no_rtti.cpp runs the same scenarios
// against the other one.
//
// Here type_key is typeid, so the shared scenarios' expected answers for
// qualified and reference types are checked against typeid itself. The binary
// without RTTI is then held to answers this one has confirmed.

#include "arude/type_key.hpp"
#include "test/helpers/type_key_scenarios.hpp"

#include <catch2/catch_test_macros.hpp>

#include <type_traits>
#include <typeindex>
#include <typeinfo>

#if !(defined ARUDE_TYPE_KEY_RTTI)
  #error "The main test binary is expected to have RTTI; the branch without it is test_type_key_no_rtti.cpp's."
#endif // #if !(defined ARUDE_TYPE_KEY_RTTI)

static_assert(std::is_same_v<arude::type_key_t, std::type_index>, "With RTTI, type_key_t is std::type_index.");

SCENARIO("type_key is typeid where the build has RTTI", "[type_key]")
{
  GIVEN("a key")
  {
    THEN("it is the std::type_index of the type")
    {
      REQUIRE(arude::type_key<type_key_test::widget>() == std::type_index{typeid(type_key_test::widget)});
    }
  }
}
