///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// versioned<T>: a member that carries its own version, read and written
/// under the same rule as the root.
///

#include "arude/config.hpp"
#include "arude/config/toml.hpp"
#include "test/helpers/config_test_v1.hpp"
#include "test/helpers/config_test_v2.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <map>
#include <optional>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace versioned_test
{
namespace
{

///
/// Version 1 of a network member.
///
struct network_v1
{
  ///
  /// The version this type is.
  ///
  static constexpr auto config_version = arude::config::version_t{1};

  ///
  /// The host, as version 1 names it.
  ///
  std::string host = "v1-host";
};

///
/// Version 2 of a network member, renaming host to hostname.
///
struct network_v2
{
  ///
  /// The version before this one.
  ///
  using previous_t = network_v1;

  ///
  /// \see network_v1::config_version
  ///
  static constexpr auto config_version = arude::config::version_t{2};

  ///
  /// The host, as version 2 names it.
  ///
  std::string hostname = "v2-host";

  ///
  /// The port.
  ///
  std::uint16_t port = 80;
};

///
/// Upgrades a network from version 1 to version 2.
/// \param val The version 1 network. Not retained.
/// \return The same network as version 2.
///
[[nodiscard]] auto upgrade(const network_v1& val) -> network_v2
{
  return network_v2{.hostname = val.host};
}

///
/// Downgrades a network from version 2 to version 1.
/// \param val The version 2 network. Not retained.
/// \return The same network as version 1.
///
[[nodiscard]] auto downgrade(const network_v2& val) -> network_v1
{
  return network_v1{.host = val.hostname};
}

///
/// A root holding one versioned member.
///
struct app
{
  ///
  /// The version this type is.
  ///
  static constexpr auto config_version = arude::config::version_t{1};

  ///
  /// The application's name.
  ///
  std::string name = "app";

  ///
  /// The network member, carrying its own version.
  ///
  arude::config::versioned<network_v2> network;
};

///
/// Version 1 of a member whose upgrade throws.
///
struct boom_v1
{
  ///
  /// The version this type is.
  ///
  static constexpr auto config_version = arude::config::version_t{1};

  ///
  /// The host.
  ///
  std::string host = "x";
};

///
/// Version 2 of a member whose upgrade from version 1 throws.
///
struct boom_v2
{
  ///
  /// The version before this one.
  ///
  using previous_t = boom_v1;

  ///
  /// \see boom_v1::config_version
  ///
  static constexpr auto config_version = arude::config::version_t{2};

  ///
  /// The host, renamed.
  ///
  std::string hostname = "y";
};

///
/// Upgrades a member by throwing, so a read reports the failure.
/// \param val The version 1 member, unused.
/// \return Nothing.
/// \throws std::runtime_error Always.
///
[[nodiscard]] auto upgrade([[maybe_unused]] const boom_v1& val) -> boom_v2
{
  throw std::runtime_error{"migration exploded"};
}

///
/// A root holding the member whose upgrade throws.
///
struct boom_app
{
  ///
  /// The version this type is.
  ///
  static constexpr auto config_version = arude::config::version_t{1};

  ///
  /// The member whose upgrade throws.
  ///
  arude::config::versioned<boom_v2> member;
};

///
/// A root holding versioned members nested every way the generic parsers
/// reach them: as a field, in a vector, in an optional, and as a map value.
///
struct holder
{
  ///
  /// The version this type is.
  ///
  static constexpr auto config_version = arude::config::version_t{1};

  ///
  /// A nested versioned member, as a field.
  ///
  arude::config::versioned<network_v1> plain;

  ///
  /// Nested versioned members in a vector.
  ///
  std::vector<arude::config::versioned<network_v1>> many;

  ///
  /// A nested versioned member in an optional.
  ///
  std::optional<arude::config::versioned<network_v1>> maybe_net;

  ///
  /// Nested versioned members as map values.
  ///
  std::map<std::string, arude::config::versioned<network_v1>> by_name;
};

///
/// Reads text as a configuration, under a policy.
/// \tparam T The configuration type.
/// \param text The text. Not retained.
/// \param policy The policy to read under.
/// \return The configuration.
/// \throws arude::config::exception_t As detail::read_root does.
///
template<typename T>
[[nodiscard]] auto read(std::string_view text, arude::config::load_policy policy) -> T
{
  auto document = arude::config::detail::parse_toml(text, "<text>", std::source_location::current());

  return arude::config::detail::read_root<T>(
    document, policy, arude::config::toml_format{}, "<text>", std::source_location::current());
}

///
/// Runs a call that is expected to throw, and returns what it threw.
/// \tparam E The exception type expected.
/// \param call The call. Not retained.
/// \return The exception the call threw.
/// \throws std::logic_error If the call returns instead, which fails the scenario.
///
template<typename E>
[[nodiscard]] auto failure_of(const std::invocable auto& call) -> E
{
  try
  {
    call();
  }
  catch(const E& ex)
  {
    return ex;
  }

  throw std::logic_error{"The call was expected to throw, and returned."};
}

} // namespace

} // namespace versioned_test

SCENARIO("a versioned member holds its value transparently", "[config][versioned]")
{
  GIVEN("a network of version 2")
  {
    const auto network = versioned_test::network_v2{.hostname = "example.com", .port = 443};

    WHEN("it is wrapped and handed out again")
    {
      const auto wrapped = arude::config::versioned<versioned_test::network_v2>{network};
      auto copy = wrapped;
      copy.value().port = 80;

      THEN("the wrapper compares as its value and converts back")
      {
        REQUIRE(wrapped.value().hostname == network.hostname);
        REQUIRE(wrapped.value().port == network.port);
        REQUIRE(copy.value().hostname == "example.com");
        REQUIRE(copy.value().port == 80);
        REQUIRE(wrapped.value().port == 443);
      }
    }
  }
}

SCENARIO("a root and its versioned members are read under one version rule", "[config][versioned]")
{
  GIVEN("text whose root and member are both at the version their types declare")
  {
    constexpr auto text = std::string_view{
      "version = 1\nname = \"example\"\n[network]\nversion = 2\nhostname = \"example.com\"\nport = 443\n"};

    WHEN("it is read")
    {
      const auto loaded =
        versioned_test::read<versioned_test::app>(text, arude::config::load_policy::upgrade_to_current);

      THEN("each part is read as its own type")
      {
        REQUIRE(loaded.name == "example");
        REQUIRE(loaded.network.value().hostname == "example.com");
        REQUIRE(loaded.network.value().port == 443);
      }
    }
  }

  GIVEN("text whose member is at version 1")
  {
    constexpr auto text =
      std::string_view{"version = 1\nname = \"example\"\n[network]\nversion = 1\nhost = \"old-host\"\n"};

    WHEN("it is read with upgrade_to_current")
    {
      const auto loaded =
        versioned_test::read<versioned_test::app>(text, arude::config::load_policy::upgrade_to_current);

      THEN("the member is migrated to version 2")
      {
        REQUIRE(loaded.network.value().hostname == "old-host");
        REQUIRE(loaded.network.value().port == versioned_test::network_v2{}.port);
      }
    }

    WHEN("it is read without upgrade_to_current")
    {
      const auto failure = versioned_test::failure_of<arude::config::exception_t>(
        [&text] -> void
        { std::ignore = versioned_test::read<versioned_test::app>(text, arude::config::load_policy::none); });

      THEN("it is an invalid_version naming the member and both versions")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_version);
        REQUIRE(failure.str().contains("network_v2 is version 1, and version 2 was asked for."));
      }
    }
  }

  GIVEN("text whose member is at version 3")
  {
    constexpr auto text =
      std::string_view{"version = 1\nname = \"example\"\n[network]\nversion = 3\nhostname = \"future\"\n"};

    WHEN("it is read, whatever the policy")
    {
      const auto failure = versioned_test::failure_of<arude::config::exception_t>(
        [&text] -> void
        {
          std::ignore = versioned_test::read<versioned_test::app>(text, arude::config::load_policy::upgrade_to_current);
        });

      THEN("it is an invalid_version saying the member is newer than this build knows")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_version);
        REQUIRE(failure.str().contains("network_v2 is version 3, newer than version 2, the newest this build knows."));
      }
    }
  }

  GIVEN("text whose member declares a version no type knows")
  {
    constexpr auto text = std::string_view{"version = 1\n[network]\nversion = 0\nhost = \"ancient\"\n"};

    WHEN("it is read with upgrade_to_current")
    {
      const auto failure = versioned_test::failure_of<arude::config::exception_t>(
        [&text] -> void
        {
          std::ignore = versioned_test::read<versioned_test::app>(text, arude::config::load_policy::upgrade_to_current);
        });

      THEN("it is an invalid_version naming the unknown version")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_version);
        REQUIRE(failure.str().contains("version 0 is not a version of "));
        REQUIRE(failure.str().contains("network_v2 that this build knows."));
      }
    }
  }

  GIVEN("text whose member is not a table")
  {
    constexpr auto text = std::string_view{"version = 1\nnetwork = 5\n"};

    WHEN("it is read")
    {
      const auto failure = versioned_test::failure_of<arude::config::exception_t>(
        [&text] -> void
        {
          std::ignore = versioned_test::read<versioned_test::app>(text, arude::config::load_policy::upgrade_to_current);
        });

      THEN("it is an invalid_payload saying a table is what holds a versioned member")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
        REQUIRE(failure.str().contains("network_v2 is held in a table, and this is not one."));
      }
    }
  }
}

SCENARIO("the root itself is versioned like a member", "[config][versioned]")
{
  GIVEN("text written for a newer root")
  {
    constexpr auto text = std::string_view{"version = 2\nname = \"future\"\n"};

    WHEN("it is read")
    {
      const auto failure = versioned_test::failure_of<arude::config::exception_t>(
        [&text] -> void
        {
          std::ignore = versioned_test::read<versioned_test::app>(text, arude::config::load_policy::upgrade_to_current);
        });

      THEN("it is an invalid_version naming the root")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_version);
        REQUIRE(failure.str().contains("app is version 2, newer than version 1, the newest this build knows."));
      }
    }
  }

  GIVEN("text written for an older root, of the example configuration's versions")
  {
    constexpr auto text = std::string_view{"version = 1\nname = \"example\"\nretries = 7\nsecret = \"\"\n"};

    WHEN("it is read as the current version with upgrade_to_current")
    {
      const auto loaded =
        versioned_test::read<test_helpers::config_test_t>(text, arude::config::load_policy::upgrade_to_current);

      THEN("the root is migrated, and what version 2 added takes its default")
      {
        REQUIRE(loaded.name == "example");
        REQUIRE(loaded.endpoint == "localhost");
      }
    }
  }

  GIVEN("text that declares no version")
  {
    constexpr auto text = std::string_view{"name = \"unversioned\"\n"};

    WHEN("it is read without create")
    {
      const auto failure = versioned_test::failure_of<arude::config::exception_t>(
        [&text] -> void
        { std::ignore = versioned_test::read<versioned_test::app>(text, arude::config::load_policy::none); });

      THEN("it is an invalid_payload saying the root declares none")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
        REQUIRE(failure.str().contains("declares no configuration version."));
      }
    }

    WHEN("it is read with create and upgrade_to_current")
    {
      using arude::bitwise_operators::operator|;

      const auto policy = arude::config::load_policy::create | arude::config::load_policy::upgrade_to_current;

      const auto failure = versioned_test::failure_of<arude::config::exception_t>(
        [&text, policy] -> void { std::ignore = versioned_test::read<versioned_test::app>(text, policy); });

      THEN("it is the same invalid_payload: no policy reads a missing version key as the newest version")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
        REQUIRE(failure.str().contains("declares no configuration version."));
      }
    }
  }

  GIVEN("a member's table that declares no version")
  {
    constexpr auto text = std::string_view{"version = 1\nname = \"app\"\n[network]\nhostname = \"bare\"\nport = 80\n"};

    WHEN("it is read with create and upgrade_to_current")
    {
      using arude::bitwise_operators::operator|;

      const auto policy = arude::config::load_policy::create | arude::config::load_policy::upgrade_to_current;

      const auto failure = versioned_test::failure_of<arude::config::exception_t>(
        [&text, policy] -> void { std::ignore = versioned_test::read<versioned_test::app>(text, policy); });

      THEN("it is an invalid_payload naming the member")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
        REQUIRE(failure.str().contains("'network'"));
        REQUIRE(failure.str().contains("the table declares no configuration version."));
      }
    }
  }
}

SCENARIO("a migration whose upgrade throws is reported as an invalid_version", "[config][versioned]")
{
  GIVEN("text whose member is at the version whose upgrade throws")
  {
    constexpr auto text = std::string_view{"version = 1\n[member]\nversion = 1\nhost = \"x\"\n"};

    WHEN("it is read with upgrade_to_current")
    {
      const auto failure = versioned_test::failure_of<arude::config::exception_t>(
        [&text] -> void
        {
          std::ignore =
            versioned_test::read<versioned_test::boom_app>(text, arude::config::load_policy::upgrade_to_current);
        });

      THEN("it is an invalid_version quoting the thrown message")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_version);
        REQUIRE(failure.str().contains("cannot be migrated to version 2"));
        REQUIRE(failure.str().contains("migration exploded"));
      }
    }
  }
}

SCENARIO("a versioned member may nest anywhere a member can sit", "[config][versioned]")
{
  GIVEN("a holder whose versioned members are all at version 1")
  {
    constexpr auto text =
      std::string_view{"version = 1\n[plain]\nversion = 1\nhost = \"field\"\n"
                       "[[many]]\nversion = 1\nhost = \"first\"\n[[many]]\nversion = 1\nhost = \"second\"\n"
                       "[maybe_net]\nversion = 1\nhost = \"optional\"\n"
                       "[by_name.one]\nversion = 1\nhost = \"mapped\"\n"};

    WHEN("it is read")
    {
      const auto loaded =
        versioned_test::read<versioned_test::holder>(text, arude::config::load_policy::upgrade_to_current);

      THEN("every nested member is read")
      {
        REQUIRE(loaded.plain.value().host == "field");
        REQUIRE(loaded.many.size() == 2);
        REQUIRE(loaded.many.at(0).value().host == "first");
        REQUIRE(loaded.many.at(1).value().host == "second");

        if(loaded.maybe_net.has_value())
        {
          REQUIRE(loaded.maybe_net->value().host == "optional");
        }
        else
        {
          FAIL("the nested optional was expected to be engaged");
        }

        REQUIRE(loaded.by_name.at("one").value().host == "mapped");
      }
    }
  }

  GIVEN("a holder whose optional versioned member is missing")
  {
    constexpr auto text =
      std::string_view{"version = 1\nmany = []\nby_name = {}\n[plain]\nversion = 1\nhost = \"field\"\n"};

    WHEN("it is read")
    {
      const auto loaded =
        versioned_test::read<versioned_test::holder>(text, arude::config::load_policy::upgrade_to_current);

      THEN("the optional is empty, and the rest is read")
      {
        REQUIRE_FALSE(loaded.maybe_net.has_value());
        REQUIRE(loaded.plain.value().host == "field");
      }
    }
  }

  GIVEN("a holder whose vector holds a newer member")
  {
    constexpr auto text =
      std::string_view{"version = 1\nby_name = {}\n[plain]\nversion = 1\nhost = \"field\"\n"
                       "[[many]]\nversion = 1\nhost = \"first\"\n[[many]]\nversion = 2\nhostname = \"newer\"\n"};

    WHEN("it is read")
    {
      const auto failure = versioned_test::failure_of<arude::config::exception_t>(
        [&text] -> void
        {
          std::ignore =
            versioned_test::read<versioned_test::holder>(text, arude::config::load_policy::upgrade_to_current);
        });

      THEN("the newer element is refused")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_version);
        REQUIRE(failure.str().contains("network_v1 is version 2, newer than version 1, the newest this build knows."));
      }
    }
  }
}

SCENARIO("a write gives every versioned table the version its type declares", "[config][versioned]")
{
  GIVEN("a root holding a versioned member of another version")
  {
    const auto value = versioned_test::app{};

    WHEN("it is written")
    {
      const auto document = arude::config::detail::write_root(value, std::source_location::current());

      THEN("the root and the member each carry their own type's version")
      {
        REQUIRE(document["version"].value<std::int64_t>() == 1);
        REQUIRE(document["network"]["version"].value<std::int64_t>() == 2);
      }
    }
  }
}
