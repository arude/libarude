///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///

// Built into the executable without RTTI, beside test_type_key_no_rtti.cpp,
// against a library that was built with it. The manager's type index is keyed
// on arude::type_key_t, which is a different type in the two builds, so this
// pins that a consumer without RTTI can still register a type and look it up.

#include "arude/config.hpp"
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

namespace
{
namespace config_no_rtti_test
{

///
/// A transport with nothing behind it, so a document can be registered without
/// touching the filesystem.
///
class transport_empty final
{
public: // Constants
  static constexpr auto name = std::string_view{"empty"};

public: // Accessors
  [[nodiscard]] auto location() const -> std::string;
  [[nodiscard]] auto writable() const -> bool;
  [[nodiscard]] auto read() const -> std::optional<std::vector<std::byte>>;

public: // Methods
  auto write(std::span<const std::byte> bytes) const -> void;
};

///
///
// NOLINTNEXTLINE(readability-convert-member-functions-to-static): the contract is an instance operation.
auto transport_empty::location() const -> std::string
{
  return std::string{name};
}

///
///
// NOLINTNEXTLINE(readability-convert-member-functions-to-static): the contract is an instance operation.
auto transport_empty::writable() const -> bool
{
  return true;
}

///
///
// NOLINTNEXTLINE(readability-convert-member-functions-to-static): the contract is an instance operation.
auto transport_empty::read() const -> std::optional<std::vector<std::byte>>
{
  return std::nullopt;
}

///
///
// NOLINTNEXTLINE(readability-convert-member-functions-to-static): the contract is an instance operation.
auto transport_empty::write([[maybe_unused]] const std::span<const std::byte> bytes) const -> void
{
}

} // namespace config_no_rtti_test

} // namespace

SCENARIO("the type index works without RTTI", "[config][config_manager][no_rtti]")
{
  GIVEN("a manager with a root section registered as a type")
  {
    auto manager = arude::config::config_manager{};
    auto document = manager.register_document(arude::config::endpoint_toml{}, config_no_rtti_test::transport_empty{});
    auto root = document.add_root<test_helpers::config_test_v1>();

    WHEN("the section is set")
    {
      auto written = test_helpers::config_test_v1{};
      written.name = "without rtti";

      root.set(written);

      THEN("get<T>() finds it by its type")
      {
        REQUIRE(manager.get<test_helpers::config_test_v1>().name == "without rtti");
      }
    }

    THEN("a type nobody registered is a not_found")
    {
      try
      {
        std::ignore = manager.get<test_helpers::config_test_t>();
        FAIL("getting an unregistered type should have thrown");
      }
      catch(const arude::config::exception_t& ex)
      {
        REQUIRE(ex.data() == arude::config::config_error::not_found);
      }
    }
  }
}
