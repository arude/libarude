///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///

// Built into the executable without RTTI, beside test_type_key_no_rtti.cpp,
// against a library that was built with it. A config_file over the in-memory
// transport loads, migrates and stores in that build, so the configuration
// support needs no RTTI of its own either.

#include "arude/config.hpp"
#include "test/configuration/helpers/memory_transport.hpp"
#include "test/helpers/config_test_v1.hpp"
#include "test/helpers/config_test_v2.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#if (defined __cpp_rtti) || (defined __GXX_RTTI) || (defined _CPP_RTTI)
  #error "test_config_no_rtti.cpp must be built without RTTI; CMakeLists.txt passes -fno-rtti or /GR-."
#endif // #if (defined __cpp_rtti) || (defined __GXX_RTTI) || (defined _CPP_RTTI)

SCENARIO("a config_file loads, migrates and stores without RTTI", "[config][config_file][no_rtti]")
{
  GIVEN("a file whose source holds version 1, over the in-memory transport")
  {
    auto store = config_test::memory_transport::store_t{};
    store["app"] = arude::config::as_bytes(
      std::string{"version = 1\n"
                  "name = \"without rtti\"\n"
                  "retries = 7\n"
                  "secret = \"\"\n"});

    auto file = arude::config::config_file<test_helpers::config_test_t, config_test::memory_transport>{
      config_test::memory_transport{"app", store}
    };

    WHEN("it is loaded")
    {
      file.load();

      THEN("the root is migrated to the current version")
      {
        REQUIRE(file.get().name == "without rtti");
        REQUIRE(file.get().endpoint == test_helpers::config_test_t{}.endpoint);
      }

      THEN("a store writes the migrated root back")
      {
        file.store();

        REQUIRE(store.contains("app"));
        REQUIRE(arude::config::as_text(store.at("app")).contains("version = 2"));
      }
    }
  }
}
