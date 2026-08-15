///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///

#include "arude/config/config_test_v1.hpp"
#include "arude/config/config_test_v2.hpp"
#include "arude/config/migration.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <type_traits>

namespace migration_test
{

///
/// A version 1 configuration with nothing left at its default.
/// Built by a function so the compile-time tests below can have one too.
///
/// \return The configuration.
///
[[nodiscard]] constexpr auto make_v1() -> arude::config::config_test_v1
{
  constexpr auto secret = std::array{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};

  return arude::config::config_test_v1{.name = "example", .retries = 7, .secret = arude::config::binary{secret}};
}

} // namespace migration_test

SCENARIO("a configuration states its version", "[config][migration]")
{
  GIVEN("the example configuration's versions")
  {
    THEN("each satisfies the concept and names its own number")
    {
      STATIC_REQUIRE(arude::config::configuration_c<arude::config::config_test_v1>);
      STATIC_REQUIRE(arude::config::configuration_c<arude::config::config_test_v2>);
      STATIC_REQUIRE(arude::config::version_of<arude::config::config_test_v1>() == 1);
      STATIC_REQUIRE(arude::config::version_of<arude::config::config_test_v2>() == 2);
    }

    THEN("the member carried in the file agrees with the constant")
    {
      STATIC_REQUIRE(arude::config::config_test_v1{}.version == arude::config::config_test_v1::config_version);
      STATIC_REQUIRE(arude::config::config_test_v2{}.version == arude::config::config_test_v2::config_version);
    }

    THEN("the current alias is the newest of them")
    {
      STATIC_REQUIRE(std::is_same_v<arude::config::config_test_t, arude::config::config_test_v2>);
    }
  }

  GIVEN("a type that is not a configuration at all")
  {
    THEN("the concept says so")
    {
      STATIC_REQUIRE(!arude::config::configuration_c<int>);
      STATIC_REQUIRE(!arude::config::configuration_c<arude::config::binary>);
    }
  }
}

SCENARIO("the versions are linked in both directions", "[config][migration]")
{
  GIVEN("version 2, which was released after version 1")
  {
    THEN("it names version 1 as the one before it")
    {
      STATIC_REQUIRE(arude::config::has_previous_configuration_c<arude::config::config_test_v2>);
      STATIC_REQUIRE(std::is_same_v<arude::config::config_test_v2::previous_t, arude::config::config_test_v1>);
    }

    THEN("version 1 names nothing before it, which is what ends the chain")
    {
      STATIC_REQUIRE(!arude::config::has_previous_configuration_c<arude::config::config_test_v1>);
    }

    THEN("the steps between the two exist")
    {
      STATIC_REQUIRE(arude::config::upgradable_configuration_c<arude::config::config_test_v1>);
      STATIC_REQUIRE(arude::config::downgradable_configuration_c<arude::config::config_test_v2>);
    }

    THEN("the ends of the chain have no step past them")
    {
      STATIC_REQUIRE(!arude::config::downgradable_configuration_c<arude::config::config_test_v1>);
      STATIC_REQUIRE(!arude::config::upgradable_configuration_c<arude::config::config_test_v2>);
    }
  }
}

SCENARIO("upgrade carries a configuration forward", "[config][migration]")
{
  GIVEN("a version 1 configuration")
  {
    const auto val = migration_test::make_v1();

    WHEN("it is upgraded")
    {
      const auto upgraded = arude::config::upgrade(val);

      THEN("what version 2 kept comes across unchanged")
      {
        REQUIRE(upgraded.name == val.name);
        REQUIRE(upgraded.retries == val.retries);
        REQUIRE(upgraded.secret == val.secret);
      }

      THEN("what version 2 added takes its default")
      {
        REQUIRE(upgraded.endpoint == "localhost");
      }

      THEN("the version follows the type rather than the file it came from")
      {
        REQUIRE(upgraded.version == 2);
      }
    }
  }
}

SCENARIO("downgrade drops what the older version cannot hold", "[config][migration]")
{
  GIVEN("a version 2 configuration with an endpoint")
  {
    auto val = arude::config::upgrade(migration_test::make_v1());
    val.endpoint = "example.invalid";

    WHEN("it is downgraded and upgraded again")
    {
      const auto round_trip = arude::config::upgrade(arude::config::downgrade(val));

      THEN("everything version 1 can hold survives")
      {
        REQUIRE(round_trip.name == val.name);
        REQUIRE(round_trip.retries == val.retries);
        REQUIRE(round_trip.secret == val.secret);
      }

      THEN("the endpoint does not, which is what makes the step lossy")
      {
        REQUIRE(round_trip.endpoint == "localhost");
      }
    }
  }
}

SCENARIO("migrate walks the chain in either direction", "[config][migration]")
{
  GIVEN("a version 1 configuration")
  {
    const auto val = migration_test::make_v1();

    WHEN("it is migrated to the current version")
    {
      const auto migrated = arude::config::migrate<arude::config::config_test_t>(val);

      THEN("it arrives as version 2, carrying what it had")
      {
        STATIC_REQUIRE(std::is_same_v<decltype(migrated), const arude::config::config_test_v2>);
        REQUIRE(migrated.version == 2);
        REQUIRE(migrated.name == val.name);
      }
    }

    WHEN("it is migrated to its own version")
    {
      const auto migrated = arude::config::migrate<arude::config::config_test_v1>(val);

      THEN("it is copied rather than sent through a step")
      {
        REQUIRE(migrated.name == val.name);
        REQUIRE(migrated.version == val.version);
      }
    }
  }

  GIVEN("a version 2 configuration")
  {
    const auto val = arude::config::upgrade(migration_test::make_v1());

    WHEN("it is migrated back to version 1")
    {
      const auto migrated = arude::config::migrate<arude::config::config_test_v1>(val);

      THEN("it arrives as version 1")
      {
        STATIC_REQUIRE(std::is_same_v<decltype(migrated), const arude::config::config_test_v1>);
        REQUIRE(migrated.version == 1);
        REQUIRE(migrated.retries == val.retries);
      }
    }
  }

  GIVEN("a constant expression context")
  {
    THEN("a migration is one too, because every step is")
    {
      STATIC_REQUIRE(arude::config::migrate<arude::config::config_test_v2>(migration_test::make_v1()).retries == 7);
      STATIC_REQUIRE(
        arude::config::migrate<arude::config::config_test_v2>(migration_test::make_v1()).endpoint == "localhost");
    }
  }
}
