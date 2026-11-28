///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// config_member: one member of a file's root, injected into a subsystem
/// that must not see the whole root.
///

#include "arude/config.hpp"
#include "test/configuration/helpers/memory_transport.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace config_member_test
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
/// Version 2 of a network member.
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
/// The root: a versioned member and a plain one.
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

  ///
  /// A plain label.
  ///
  std::string label = "label";
};

///
/// Seeds a slot with a current-version document, as text.
/// \return The text.
///
[[nodiscard]] auto current_text() -> std::string
{
  return std::string{"version = 1\nname = \"from the source\"\nlabel = \"label\"\n"
                     "[network]\nversion = 2\nhostname = \"source-host\"\nport = 80\n"};
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

} // namespace config_member_test

SCENARIO("a member handle reads and writes one member under the file's lock", "[config][config_member]")
{
  GIVEN("a file with a set root, and a handle to its versioned member")
  {
    auto store = config_test::memory_transport::store_t{};

    auto file = arude::config::config_file<config_member_test::app, config_test::memory_transport>{
      config_test::memory_transport{"app", store}
    };
    file.set(config_member_test::app{});

    auto handle = file.member(&config_member_test::app::network);

    THEN("the handle is a config_member of the member's type, unwrapped of versioned<>")
    {
      STATIC_REQUIRE(std::is_same_v<decltype(handle), arude::config::config_member<config_member_test::network_v2>>);
    }

    WHEN("the member is read through the handle")
    {
      const auto network = handle.get();

      THEN("it is the root's member, and the handle knows the file")
      {
        REQUIRE(network.hostname == "v2-host");
        REQUIRE(handle.loaded());
        REQUIRE(handle.location() == "memory:app");
      }
    }

    WHEN("the member is replaced through the handle")
    {
      handle.set(config_member_test::network_v2{.hostname = "injected", .port = 443});

      THEN("the root holds the new member, and the rest of it is untouched")
      {
        REQUIRE(file.get().network.value().hostname == "injected");
        REQUIRE(file.get().network.value().port == 443);
        REQUIRE(file.get().name == "app");
      }
    }
  }

  GIVEN("a handle to a plain member")
  {
    auto store = config_test::memory_transport::store_t{};

    auto file = arude::config::config_file<config_member_test::app, config_test::memory_transport>{
      config_test::memory_transport{"app", store}
    };
    file.set(config_member_test::app{});

    auto handle = file.member(&config_member_test::app::label);

    THEN("the handle is a config_member of that member's type")
    {
      STATIC_REQUIRE(std::is_same_v<decltype(handle), arude::config::config_member<std::string>>);
    }

    WHEN("the member is replaced through the handle")
    {
      handle.set("new label");

      THEN("the root holds the new member")
      {
        REQUIRE(file.get().label == "new label");
      }
    }
  }
}

SCENARIO("a handle made before any load works over a set root, with no I/O", "[config][config_member]")
{
  GIVEN("an empty store, a file with a set root, and a handle")
  {
    auto store = config_test::memory_transport::store_t{};

    auto file = arude::config::config_file<config_member_test::app, config_test::memory_transport>{
      config_test::memory_transport{"app", store}
    };
    file.set(config_member_test::app{});

    auto handle = file.member(&config_member_test::app::network);

    WHEN("the member is read and replaced through the handle")
    {
      const auto before = handle.get();
      handle.set(config_member_test::network_v2{.hostname = "before any load"});

      THEN("nothing touched the medium")
      {
        REQUIRE(before.hostname == "v2-host");
        REQUIRE(!store.contains("app"));
        REQUIRE(file.get().network.value().hostname == "before any load");
      }
    }
  }
}

SCENARIO("a handle on a file that was never loaded or set is a not_found", "[config][config_member]")
{
  GIVEN("a file with no root, and a handle")
  {
    auto store = config_test::memory_transport::store_t{};
    store["app"] = arude::config::as_bytes(config_member_test::current_text());

    auto file = arude::config::config_file<config_member_test::app, config_test::memory_transport>{
      config_test::memory_transport{"app", store}
    };

    auto handle = file.member(&config_member_test::app::network);

    WHEN("the member is asked for")
    {
      const auto failure =
        config_member_test::failure_of<arude::config::exception_t>([&handle] -> void { std::ignore = handle.get(); });

      THEN("it is a not_found, and the handle does not engage the root")
      {
        REQUIRE(failure.data() == arude::config::config_error::not_found);
        REQUIRE(failure.str().contains("has not been loaded or set."));
        REQUIRE(!handle.loaded());
        REQUIRE(!file.loaded());
      }
    }

    WHEN("the member is replaced")
    {
      const auto failure = config_member_test::failure_of<arude::config::exception_t>(
        [&handle] -> void { handle.set(config_member_test::network_v2{}); });

      THEN("it is a not_found, for the same reason")
      {
        REQUIRE(failure.data() == arude::config::config_error::not_found);
        REQUIRE(!file.loaded());
      }
    }
  }
}

SCENARIO("a handle outlives its file", "[config][config_member]")
{
  GIVEN("a handle taken before its file goes away")
  {
    auto store = config_test::memory_transport::store_t{};

    auto handle = std::optional<arude::config::config_member<config_member_test::network_v2>>{};

    {
      auto file = arude::config::config_file<config_member_test::app, config_test::memory_transport>{
        config_test::memory_transport{"app", store}
      };
      file.set(config_member_test::app{});

      handle.emplace(file.member(&config_member_test::app::network));
    }

    WHEN("the handle is used after the file is gone")
    {
      THEN("it still reads and writes the member, and reports the file's location")
      {
        REQUIRE(handle->loaded());
        REQUIRE(handle->location() == "memory:app");

        handle->set(config_member_test::network_v2{.hostname = "outlived", .port = 1});
        REQUIRE(handle->get().hostname == "outlived");
      }
    }
  }
}

SCENARIO("copies of a handle share the member, and a move copies", "[config][config_member]")
{
  GIVEN("a file with a set root, and a handle")
  {
    auto store = config_test::memory_transport::store_t{};

    auto file = arude::config::config_file<config_member_test::app, config_test::memory_transport>{
      config_test::memory_transport{"app", store}
    };
    file.set(config_member_test::app{});

    auto handle = file.member(&config_member_test::app::network);

    WHEN("one copy replaces the member")
    {
      auto copy = handle;
      copy.set(config_member_test::network_v2{.hostname = "shared", .port = 2});

      THEN("the other copy sees it: they share the member")
      {
        REQUIRE(handle.get().hostname == "shared");
      }
    }

    WHEN("the handle is moved from")
    {
      const auto moved = std::move(handle);

      THEN("the moved-from handle still refers to the same member, as the moved-to one does")
      {
        // The moves copy the pointer by design, so a moved-from handle is
        // never empty; see the config_member documentation.
        // NOLINTBEGIN(bugprone-use-after-move): the moved-from handle is still valid, which is the point.
        REQUIRE(moved.get().hostname == "v2-host");
        REQUIRE(handle.get().hostname == "v2-host");
        // NOLINTEND(bugprone-use-after-move)
      }
    }
  }
}

SCENARIO("a store through a handle stores the whole file", "[config][config_member]")
{
  GIVEN("a loaded file, and a handle that replaces its member")
  {
    auto store = config_test::memory_transport::store_t{};
    store["app"] = arude::config::as_bytes(config_member_test::current_text());

    auto file = arude::config::config_file<config_member_test::app, config_test::memory_transport>{
      config_test::memory_transport{"app", store}
    };
    file.load();

    auto handle = file.member(&config_member_test::app::network);
    handle.set(config_member_test::network_v2{.hostname = "through the handle", .port = 443});

    WHEN("it is stored through the handle")
    {
      handle.store();

      THEN("a second file over the same source loads the new member and the rest of the root")
      {
        auto second = arude::config::config_file<config_member_test::app, config_test::memory_transport>{
          config_test::memory_transport{"app", store}
        };
        second.load();

        REQUIRE(second.get().name == "from the source");
        REQUIRE(second.get().network.value().hostname == "through the handle");
        REQUIRE(second.get().network.value().port == 443);
      }
    }
  }
}
