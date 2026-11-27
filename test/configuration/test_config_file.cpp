///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// config_file: one versioned struct per file, loaded and stored whole.
/// The data-safety rules — a store never overwrites what this build cannot
/// read, a failed load changes nothing, and the lock discipline that keeps
/// get() off the I/O path — are each pinned here.
///

#include "arude/config.hpp"
#include "test/configuration/helpers/memory_transport.hpp"
#include "test/helpers/temp_directory.hpp"
#include "test/helpers/temp_file.hpp"

#include <catch2/catch_test_macros.hpp>

#if (defined _WIN32)
  #if !(defined WIN32_LEAN_AND_MEAN)
    #define WIN32_LEAN_AND_MEAN
  #endif // #if !(defined WIN32_LEAN_AND_MEAN)
  #if !(defined NOMINMAX)
    #define NOMINMAX
  #endif // #if !(defined NOMINMAX)
  #include <windows.h>
#endif // #if (defined _WIN32)

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <format>
#include <future>
#include <map>
#include <mutex>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

namespace config_file_test
{
namespace
{

///
/// Version 1 of a member that itself holds nothing versioned.
///
struct inner_v1
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
  /// A depth.
  ///
  std::int32_t depth = 7;
};

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
  /// The version carried in the text.
  ///
  arude::config::version_t version = config_version;

  ///
  /// The host, as version 1 names it.
  ///
  std::string host = "v1-host";
};

///
/// Version 2 of a network member, holding a nested versioned member and a
/// vector of versioned members.
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
  /// \see network_v1::version
  ///
  arude::config::version_t version = config_version;

  ///
  /// The host, as version 2 names it.
  ///
  std::string hostname = "v2-host";

  ///
  /// The port.
  ///
  std::uint16_t port = 80;

  ///
  /// A nested versioned member.
  ///
  arude::config::versioned<inner_v1> inner;

  ///
  /// Versioned members in a vector.
  ///
  std::vector<arude::config::versioned<inner_v1>> extras;
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
/// The root the file holds: two versioned members beside plain fields.
///
struct app
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
  /// The application's name.
  ///
  std::string name = "app";

  ///
  /// The network member, carrying its own version.
  ///
  arude::config::versioned<network_v2> network;

  ///
  /// The extra member, carrying its own version.
  ///
  arude::config::versioned<inner_v1> extra;
};

///
/// The [network] table at its current version: the member itself, the
/// nested member, and one vector element. A read without create requires
/// every field, so a complete document spells all three in full.
///
constexpr auto network_block = std::string_view{"[network]\nversion = 2\nhostname = \"source-host\"\nport = 80\n"
                                                "[network.inner]\nversion = 1\ndepth = 7\n"
                                                "[[network.extras]]\nversion = 1\ndepth = 7\n"};

///
/// A flag enum, whose value reflect-cpp writes as '|'-separated names.
///
enum class flags // NOLINT(performance-enum-size): kept signed, so a negative flag value stays negative.
{
  ///
  /// The first flag.
  ///
  a = 1,

  ///
  /// The second flag.
  ///
  b = 2
};

///
/// Combines two flags, which is what makes reflect-cpp treat the enum as flags.
/// \param left One flag.
/// \param right Another.
/// \return The two flags together.
///
constexpr auto operator|(flags left, flags right) -> flags
{
  return static_cast<flags>(std::to_underlying(left) | std::to_underlying(right));
}

///
/// A root holding one flag field, for a write that must refuse.
///
struct flaggy
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
  /// A flag field.
  ///
  flags mode = flags::a;
};

///
/// A root whose members may be missing from the source.
///
struct optionals
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
  /// A required field.
  ///
  std::string name = "optionals";

  ///
  /// An optional field with an initialiser, which a missing key does not
  /// restore.
  ///
  std::optional<std::int32_t> timeout = 30;

  ///
  /// An optional field without one.
  ///
  std::optional<std::string> label;
};

///
/// Version 1 of a member that holds a nested versioned member, so an older
/// table can hold a newer one.
///
struct layer_v1
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
  /// A nested versioned member.
  ///
  arude::config::versioned<inner_v1> inner;
};

///
/// Version 2 of the layered member, adding a field beside the nested one.
///
struct layer_v2
{
  ///
  /// The version before this one.
  ///
  using previous_t = layer_v1;

  ///
  /// \see layer_v1::config_version
  ///
  static constexpr auto config_version = arude::config::version_t{2};

  ///
  /// \see layer_v1::version
  ///
  arude::config::version_t version = config_version;

  ///
  /// A nested versioned member.
  ///
  arude::config::versioned<inner_v1> inner;

  ///
  /// A weight, which version 1 lacks.
  ///
  std::int32_t weight = 1;
};

///
/// Upgrades a layer from version 1 to version 2.
/// \param val The version 1 layer. Not retained.
/// \return The same layer as version 2.
///
[[nodiscard]] auto upgrade(const layer_v1& val) -> layer_v2
{
  return layer_v2{.inner = val.inner};
}

///
/// Downgrades a layer from version 2 to version 1.
/// \param val The version 2 layer. Not retained.
/// \return The same layer as version 1.
///
[[nodiscard]] auto downgrade(const layer_v2& val) -> layer_v1
{
  return layer_v1{.inner = val.inner};
}

///
/// A root holding the layered member.
///
struct layered
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
  /// The layered member, carrying its own version.
  ///
  arude::config::versioned<layer_v2> layer;
};

///
/// A struct an array holds, with a versioned member beside a plain field.
///
struct slot
{
  ///
  /// A weight.
  ///
  std::int32_t weight = 1;

  ///
  /// A versioned member.
  ///
  arude::config::versioned<inner_v1> inner;
};

///
/// A root holding versioned members in arrays, bare and inside a struct.
///
struct racked
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
  /// Structs holding a versioned member, in a vector.
  ///
  std::vector<slot> slots;

  ///
  /// Versioned members in a std::array, which reflect-cpp reads apart from a
  /// vector.
  ///
  std::array<arude::config::versioned<inner_v1>, 2> pair;
};

///
/// A root holding versioned members in tuples, which reflect-cpp reads as
/// arrays of mixed elements.
///
struct tupled
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
  /// A count and a versioned member, as a std::tuple.
  ///
  std::tuple<std::int32_t, arude::config::versioned<inner_v1>> counted;

  ///
  /// A count and a versioned member, as a std::pair.
  ///
  std::pair<std::int32_t, arude::config::versioned<inner_v1>> paired;
};

///
/// Seeds a slot with a current-version document, as text.
/// \return The text.
///
[[nodiscard]] auto current_text() -> std::string
{
  return std::format("version = 1\nname = \"from the source\"\n{}\n[extra]\nversion = 1\ndepth = 3\n", network_block);
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

} // namespace config_file_test

SCENARIO("a file is loaded and stored whole", "[config][config_file]")
{
  GIVEN("a store holding a current-version document, and a file over it")
  {
    auto store = config_test::memory_transport::store_t{};
    store["app"] = arude::config::as_bytes(config_file_test::current_text());

    auto file = arude::config::config_file<config_file_test::app, config_test::memory_transport>{
      config_test::memory_transport{"app", store}
    };

    WHEN("it is loaded")
    {
      file.load();

      THEN("every member holds what the source held")
      {
        REQUIRE(file.loaded());
        REQUIRE(file.get().name == "from the source");
        REQUIRE(file.get().network.value().hostname == "source-host");
        REQUIRE(file.get().extra.value().depth == 3);
      }
    }

    WHEN("a value is set and the file is stored")
    {
      file.load();

      auto value = file.get();
      value.name = "stored";
      file.set(value);
      file.store();

      THEN("a second file over the same source loads the stored root back")
      {
        auto second = arude::config::config_file<config_file_test::app, config_test::memory_transport>{
          config_test::memory_transport{"app", store}
        };
        second.load();

        REQUIRE(second.get().name == "stored");
        REQUIRE(second.get().network.value().hostname == "source-host");
        REQUIRE(second.get().network.value().version == 2);
        REQUIRE(second.get().extra.value().depth == 3);
      }
    }
  }
}

SCENARIO("versioned members migrate independently of each other", "[config][config_file]")
{
  GIVEN("a store whose network is at version 1 and whose extra is at version 1")
  {
    auto store = config_test::memory_transport::store_t{};
    store["app"] = arude::config::as_bytes(
      std::string{"version = 1\nname = \"app\"\n"
                  "[network]\nversion = 1\nhost = \"old-host\"\n"
                  "[extra]\nversion = 1\ndepth = 9\n"});

    auto file = arude::config::config_file<config_file_test::app, config_test::memory_transport>{
      config_test::memory_transport{"app", store}
    };

    WHEN("it is loaded")
    {
      file.load();

      THEN("the older member is migrated and the current one read, each as its own type")
      {
        REQUIRE(file.get().network.value().hostname == "old-host");
        REQUIRE(file.get().network.value().version == 2);
        REQUIRE(file.get().extra.value().depth == 9);
        REQUIRE(file.get().extra.value().version == 1);
      }
    }
  }
}

SCENARIO("a source written by a newer build is refused and left alone", "[config][config_file]")
{
  GIVEN("a store whose network member is at version 3, which this build has no type for")
  {
    auto store = config_test::memory_transport::store_t{};
    store["app"] = arude::config::as_bytes(
      std::string{"version = 1\nname = \"app\"\n[network]\nversion = 3\nhostname = \"future\"\n"});

    auto file = arude::config::config_file<config_file_test::app, config_test::memory_transport>{
      config_test::memory_transport{"app", store}
    };

    WHEN("it is loaded")
    {
      const auto failure = config_file_test::failure_of<arude::config::exception_t>([&file] -> void { file.load(); });

      THEN("it is an invalid_version naming the member and both versions")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_version);
        REQUIRE(failure.str().contains("is version 3, newer than version 2, the newest this build knows."));
      }

      THEN("nothing was loaded")
      {
        REQUIRE(!file.loaded());
      }
    }
  }
}

SCENARIO("a load that fails changes nothing", "[config][config_file]")
{
  GIVEN("a file that has been loaded, over a store that then holds a bad source")
  {
    auto store = config_test::memory_transport::store_t{};
    store["app"] = arude::config::as_bytes(config_file_test::current_text());

    auto file = arude::config::config_file<config_file_test::app, config_test::memory_transport>{
      config_test::memory_transport{"app", store}
    };
    file.load();

    WHEN("a load of text that is not TOML fails")
    {
      store["app"] = arude::config::as_bytes(std::string{"version = = 2\n"});

      const auto failure = config_file_test::failure_of<arude::config::exception_t>([&file] -> void { file.load(); });

      THEN("it is an invalid_payload, and the loaded value stands")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
        REQUIRE(file.get().name == "from the source");
      }
    }

    WHEN("a load of a newer version fails")
    {
      store["app"] = arude::config::as_bytes(std::string{"version = 2\nname = \"future\"\n"});

      const auto failure = config_file_test::failure_of<arude::config::exception_t>([&file] -> void { file.load(); });

      THEN("it is an invalid_version, and the loaded value stands")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_version);
        REQUIRE(file.get().name == "from the source");
      }
    }

    WHEN("a load of an out-of-range integer fails")
    {
      store["app"] =
        arude::config::as_bytes(std::string{"version = 1\nname = \"broken\"\n[network]\nversion = 2\nport = 70000\n"});

      const auto failure = config_file_test::failure_of<arude::config::exception_t>([&file] -> void { file.load(); });

      THEN("it is an invalid_payload, and the loaded value stands")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
        REQUIRE(file.get().name == "from the source");
      }
    }

    WHEN("a load of a source that is not there fails without create")
    {
      store.erase("app");

      const auto failure = config_file_test::failure_of<arude::config::exception_t>(
        [&file] -> void { file.load(arude::config::load_policy::none); });

      THEN("it is a not_found, and the loaded value stands")
      {
        REQUIRE(failure.data() == arude::config::config_error::not_found);
        REQUIRE(file.get().name == "from the source");
      }
    }
  }
}

SCENARIO("the create policy leaves the source alone", "[config][config_file]")
{
  GIVEN("a file whose source is not there")
  {
    auto store = config_test::memory_transport::store_t{};

    auto file = arude::config::config_file<config_file_test::app, config_test::memory_transport>{
      config_test::memory_transport{"app", store}
    };

    WHEN("it is loaded with create")
    {
      using arude::bitwise_operators::operator|;

      file.load(arude::config::load_policy::create | arude::config::load_policy::upgrade_to_current);

      THEN("the root is its default, and nothing was written")
      {
        REQUIRE(file.get().name == "app");
        REQUIRE(file.get().network.value().hostname == "v2-host");
        REQUIRE(!store.contains("app"));
      }

      THEN("a store afterwards writes the defaults, and only then is the source created")
      {
        file.store();

        REQUIRE(store.contains("app"));
        REQUIRE(arude::config::as_text(store.at("app")).contains("name = 'app'"));
      }
    }
  }
}

SCENARIO("reading a file that was never loaded is a not_found", "[config][config_file]")
{
  GIVEN("a file and no load or set")
  {
    auto store = config_test::memory_transport::store_t{};
    store["app"] = arude::config::as_bytes(config_file_test::current_text());

    auto file = arude::config::config_file<config_file_test::app, config_test::memory_transport>{
      config_test::memory_transport{"app", store}
    };

    THEN("it reports that it holds nothing")
    {
      REQUIRE(!file.loaded());
    }

    WHEN("its value is asked for")
    {
      const auto failure =
        config_file_test::failure_of<arude::config::exception_t>([&file] -> void { std::ignore = file.get(); });

      THEN("it is a not_found")
      {
        REQUIRE(failure.data() == arude::config::config_error::not_found);
        REQUIRE(failure.str().contains("has not been loaded or set."));
      }
    }

    WHEN("it is stored")
    {
      const auto failure = config_file_test::failure_of<arude::config::exception_t>([&file] -> void { file.store(); });

      THEN("it is a not_found saying there is nothing to store")
      {
        REQUIRE(failure.data() == arude::config::config_error::not_found);
        REQUIRE(failure.str().contains("there is nothing to store."));
      }
    }
  }
}

SCENARIO(
  "a member or field missing from the source is an invalid_payload, whatever the policy", "[config][config_file]")
{
  GIVEN("a store whose document has no network table, and a file over it")
  {
    auto store = config_test::memory_transport::store_t{};
    store["app"] =
      arude::config::as_bytes(std::string{"version = 1\nname = \"sparse\"\n[extra]\nversion = 1\ndepth = 3\n"});

    auto file = arude::config::config_file<config_file_test::app, config_test::memory_transport>{
      config_test::memory_transport{"app", store}
    };

    WHEN("it is loaded without create")
    {
      const auto failure = config_file_test::failure_of<arude::config::exception_t>([&file] -> void { file.load(); });

      THEN("it is an invalid_payload naming the missing member")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
        REQUIRE(failure.str().contains("'network'"));
      }
    }

    WHEN("it is loaded with create")
    {
      using arude::bitwise_operators::operator|;

      const auto policy = arude::config::load_policy::create | arude::config::load_policy::upgrade_to_current;

      const auto failure =
        config_file_test::failure_of<arude::config::exception_t>([&file, policy] -> void { file.load(policy); });

      THEN("create does not fill the member in: the source is there, so it is the same invalid_payload")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
        REQUIRE(failure.str().contains("'network'"));
        REQUIRE(!file.loaded());
      }
    }
  }

  GIVEN("a store whose network table lacks its port, and a file over it")
  {
    auto store = config_test::memory_transport::store_t{};
    store["app"] = arude::config::as_bytes(
      std::string{"version = 1\nname = \"sparse\"\n"
                  "[network]\nversion = 2\nhostname = \"source-host\"\n"
                  "[network.inner]\nversion = 1\ndepth = 7\n"
                  "[[network.extras]]\nversion = 1\ndepth = 7\n"
                  "[extra]\nversion = 1\ndepth = 3\n"});

    auto file = arude::config::config_file<config_file_test::app, config_test::memory_transport>{
      config_test::memory_transport{"app", store}
    };

    WHEN("it is loaded with create")
    {
      using arude::bitwise_operators::operator|;

      const auto policy = arude::config::load_policy::create | arude::config::load_policy::upgrade_to_current;

      const auto failure =
        config_file_test::failure_of<arude::config::exception_t>([&file, policy] -> void { file.load(policy); });

      THEN("it is an invalid_payload naming the missing field, which takes no default")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
        REQUIRE(failure.str().contains("'port'"));
        REQUIRE(!file.loaded());
      }
    }
  }
}

SCENARIO("a missing std::optional member reads as std::nullopt", "[config][config_file]")
{
  GIVEN("a source that holds neither of two optional members, one of them with an initialiser")
  {
    auto store = config_test::memory_transport::store_t{};
    store["app"] = arude::config::as_bytes(std::string{"version = 1\nname = \"bare\"\n"});

    auto file = arude::config::config_file<config_file_test::optionals, config_test::memory_transport>{
      config_test::memory_transport{"app", store}
    };

    WHEN("it is loaded")
    {
      file.load();

      THEN("both are std::nullopt: the initialiser of a missing optional is not what it reads as")
      {
        REQUIRE(config_file_test::optionals{}.timeout == 30);
        REQUIRE_FALSE(file.get().timeout.has_value());
        REQUIRE_FALSE(file.get().label.has_value());
        REQUIRE(file.get().name == "bare");
      }
    }
  }

  GIVEN("a source that holds both")
  {
    auto store = config_test::memory_transport::store_t{};
    store["app"] =
      arude::config::as_bytes(std::string{"version = 1\nname = \"full\"\ntimeout = 5\nlabel = \"named\"\n"});

    auto file = arude::config::config_file<config_file_test::optionals, config_test::memory_transport>{
      config_test::memory_transport{"app", store}
    };

    WHEN("it is loaded")
    {
      file.load();

      THEN("each holds what the source held")
      {
        REQUIRE(file.get().timeout == 5);
        REQUIRE(file.get().label == "named");
      }
    }
  }
}

SCENARIO("a store refuses to write over what a newer build wrote, and replaces anything else", "[config][config_file]")
{
  GIVEN("a loaded file whose source is then replaced by a newer version")
  {
    auto store = config_test::memory_transport::store_t{};
    store["app"] = arude::config::as_bytes(config_file_test::current_text());

    auto file = arude::config::config_file<config_file_test::app, config_test::memory_transport>{
      config_test::memory_transport{"app", store}
    };
    file.load();

    WHEN("the newer value is the root, the member, a nested member, a vector element, or a member beside a typo")
    {
      const auto newer_root = std::string{"version = 2\nname = \"future\"\n"};
      const auto newer_member =
        std::string{"version = 1\nname = \"app\"\n[network]\nversion = 3\nhostname = \"future\"\n"};
      const auto newer_nested = std::string{"version = 1\nname = \"app\"\n[network]\nversion = 2\nhostname = \"now\"\n"
                                            "[network.inner]\nversion = 2\ndepth = 8\n"
                                            "[[network.extras]]\nversion = 1\ndepth = 7\n"};
      const auto newer_element = std::string{"version = 1\nname = \"app\"\n[network]\nversion = 2\nhostname = \"now\"\n"
                                             "[network.inner]\nversion = 1\ndepth = 7\n"
                                             "[[network.extras]]\nversion = 2\ndepth = 8\n"};
      // The root's own name is of the wrong type, which on its own would be
      // replaced; the newer member beside it is what refuses the store.
      const auto newer_beside_typo =
        std::string{"version = 1\nname = 7\n[network]\nversion = 3\nhostname = \"future\"\n"};

      THEN("each refusal is an invalid_version naming the newer table, and the source is untouched")
      {
        for(const auto& text : {newer_root, newer_member, newer_nested, newer_element, newer_beside_typo})
        {
          store["app"] = arude::config::as_bytes(text);
          const auto before = store.at("app");

          const auto failure =
            config_file_test::failure_of<arude::config::exception_t>([&file] -> void { file.store(); });

          INFO(std::format("The message is: {}", failure.str()));
          REQUIRE(failure.data() == arude::config::config_error::invalid_version);
          REQUIRE(failure.str().contains("refusing to store over 'memory:app', which a newer build wrote"));
          REQUIRE(failure.str().contains("newer than version"));
          REQUIRE(store.at("app") == before);
        }
      }
    }

    WHEN("the newer value is a nested member inside a member table that declares no version")
    {
      // The network table lacks its version key, so it does not read; the
      // probe still reads it for what it holds, and finds the newer inner.
      store["app"] = arude::config::as_bytes(
        std::string{"version = 1\nname = \"app\"\n[network]\nhostname = \"now\"\nport = 80\n"
                    "[network.inner]\nversion = 2\ndepth = 8\n"
                    "[[network.extras]]\nversion = 1\ndepth = 7\n"});
      const auto before = store.at("app");

      const auto failure = config_file_test::failure_of<arude::config::exception_t>([&file] -> void { file.store(); });

      THEN("the store is refused as an invalid_version, and the source is untouched")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_version);
        REQUIRE(failure.str().contains("inner_v1 is version 2, newer than version 1"));
        REQUIRE(store.at("app") == before);
      }
    }

    WHEN("the newer value is an array element after one that lacks a field")
    {
      // An element missing a required field fails the array, and the probe
      // reads on past it to the newer element.
      store["app"] = arude::config::as_bytes(
        std::string{"version = 1\nname = \"app\"\n[network]\nversion = 2\nhostname = \"now\"\nport = 80\n"
                    "[network.inner]\nversion = 1\ndepth = 7\n"
                    "[[network.extras]]\nversion = 1\n"
                    "[[network.extras]]\nversion = 2\ndepth = 8\n"
                    "[extra]\nversion = 1\ndepth = 3\n"});
      const auto before = store.at("app");

      const auto load_failure =
        config_file_test::failure_of<arude::config::exception_t>([&file] -> void { file.load(); });
      const auto failure = config_file_test::failure_of<arude::config::exception_t>([&file] -> void { file.store(); });

      THEN("the load still fails on the first element, and the store is refused, leaving the source untouched")
      {
        REQUIRE(load_failure.data() == arude::config::config_error::invalid_payload);
        REQUIRE(load_failure.str().contains("'depth'"));
        REQUIRE(failure.data() == arude::config::config_error::invalid_version);
        REQUIRE(failure.str().contains("inner_v1 is version 2, newer than version 1"));
        REQUIRE(store.at("app") == before);
      }
    }

    WHEN("the source is TOML that does not read as the root type")
    {
      store["app"] =
        arude::config::as_bytes(std::string{"version = 1\nname = \"app\"\n[network]\nversion = 2\nport = -1\n"});

      file.store();

      THEN("it is replaced by the cached root, which loads back")
      {
        auto second = arude::config::config_file<config_file_test::app, config_test::memory_transport>{
          config_test::memory_transport{"app", store}
        };
        second.load();

        REQUIRE(second.get().name == file.get().name);
        REQUIRE(second.get().network.value().port == file.get().network.value().port);
      }
    }

    WHEN("the source is not valid TOML at all")
    {
      store["app"] = arude::config::as_bytes(std::string{"not toml {{{"});

      file.store();

      THEN("it is replaced by the cached root, which loads back")
      {
        auto second = arude::config::config_file<config_file_test::app, config_test::memory_transport>{
          config_test::memory_transport{"app", store}
        };
        second.load();

        REQUIRE(second.get().name == file.get().name);
      }
    }
  }
}

SCENARIO("a store probe reads every element of an array past one that fails", "[config][config_file]")
{
  GIVEN("a file with a cached value, over a store")
  {
    auto store = config_test::memory_transport::store_t{};

    auto file = arude::config::config_file<config_file_test::racked, config_test::memory_transport>{
      config_test::memory_transport{"app", store}
    };
    file.set(config_file_test::racked{});

    const auto pair_block = std::string_view{"[[pair]]\nversion = 1\ndepth = 7\n[[pair]]\nversion = 1\ndepth = 7\n"};

    WHEN("a struct in a vector holds a newer member, after a struct that does not read")
    {
      store["app"] = arude::config::as_bytes(
        std::format(
          "version = 1\n"
          "[[slots]]\nweight = \"heavy\"\n[slots.inner]\nversion = 1\ndepth = 7\n"
          "[[slots]]\nweight = 2\n[slots.inner]\nversion = 2\ndepth = 8\n"
          "{}",
          pair_block));
      const auto before = store.at("app");

      const auto failure = config_file_test::failure_of<arude::config::exception_t>([&file] -> void { file.store(); });

      THEN("the store is refused as an invalid_version, and the source is untouched")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_version);
        REQUIRE(failure.str().contains("inner_v1 is version 2, newer than version 1"));
        REQUIRE(store.at("app") == before);
      }
    }

    WHEN("a std::array holds a newer element, after one that does not read")
    {
      store["app"] = arude::config::as_bytes(
        std::string{"version = 1\nslots = []\n"
                    "[[pair]]\nversion = 1\ndepth = \"deep\"\n[[pair]]\nversion = 2\ndepth = 8\n"});
      const auto before = store.at("app");

      const auto failure = config_file_test::failure_of<arude::config::exception_t>([&file] -> void { file.store(); });

      THEN("the store is refused as an invalid_version, and the source is untouched")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_version);
        REQUIRE(failure.str().contains("inner_v1 is version 2, newer than version 1"));
        REQUIRE(store.at("app") == before);
      }
    }

    WHEN("the elements after one that does not read are all current")
    {
      store["app"] = arude::config::as_bytes(
        std::format(
          "version = 1\n"
          "[[slots]]\nweight = \"heavy\"\n[slots.inner]\nversion = 1\ndepth = 7\n"
          "[[slots]]\nweight = 2\n[slots.inner]\nversion = 1\ndepth = 8\n"
          "{}",
          pair_block));

      file.store();

      THEN("the source is replaced by the cached value, which loads back")
      {
        auto second = arude::config::config_file<config_file_test::racked, config_test::memory_transport>{
          config_test::memory_transport{"app", store}
        };
        second.load();

        REQUIRE(second.get().slots.empty());
      }
    }
  }

  GIVEN("a file with a cached value, over a store holding tuples")
  {
    auto store = config_test::memory_transport::store_t{};

    auto file = arude::config::config_file<config_file_test::tupled, config_test::memory_transport>{
      config_test::memory_transport{"app", store}
    };
    file.set(config_file_test::tupled{});

    WHEN("a std::tuple holds a newer element, after one that does not read")
    {
      store["app"] = arude::config::as_bytes(
        std::string{"version = 1\n"
                    "counted = [\"many\", {version = 2, depth = 8}]\n"
                    "paired = [1, {version = 1, depth = 7}]\n"});
      const auto before = store.at("app");

      const auto failure = config_file_test::failure_of<arude::config::exception_t>([&file] -> void { file.store(); });

      THEN("the store is refused as an invalid_version, and the source is untouched")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_version);
        REQUIRE(failure.str().contains("inner_v1 is version 2, newer than version 1"));
        REQUIRE(store.at("app") == before);
      }
    }

    WHEN("a std::pair holds a newer element, after one that does not read")
    {
      store["app"] = arude::config::as_bytes(
        std::string{"version = 1\n"
                    "counted = [1, {version = 1, depth = 7}]\n"
                    "paired = [\"many\", {version = 2, depth = 8}]\n"});
      const auto before = store.at("app");

      const auto failure = config_file_test::failure_of<arude::config::exception_t>([&file] -> void { file.store(); });

      THEN("the store is refused as an invalid_version, and the source is untouched")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_version);
        REQUIRE(failure.str().contains("inner_v1 is version 2, newer than version 1"));
        REQUIRE(store.at("app") == before);
      }
    }

    WHEN("the element after one that does not read is current")
    {
      store["app"] = arude::config::as_bytes(
        std::string{"version = 1\n"
                    "counted = [\"many\", {version = 1, depth = 8}]\n"
                    "paired = [\"many\", {version = 1, depth = 8}]\n"});

      file.store();

      THEN("the source is replaced by the cached value, which loads back")
      {
        auto second = arude::config::config_file<config_file_test::tupled, config_test::memory_transport>{
          config_test::memory_transport{"app", store}
        };
        second.load();

        REQUIRE(std::get<1>(second.get().counted).value().depth == 7);
      }
    }

    WHEN("a tuple holds an element past its own size, after one that does not read")
    {
      store["app"] = arude::config::as_bytes(
        std::string{"version = 1\n"
                    "counted = [\"many\", {version = 1, depth = 7}, {version = 9, depth = 9}]\n"
                    "paired = [1, {version = 1, depth = 7}]\n"});
      const auto before = store.at("app");

      file.store();

      THEN("the tuple has no field for it, so reflect-cpp refuses the array and the store replaces the source")
      {
        REQUIRE(store.at("app") != before);
      }
    }
  }
}

SCENARIO("a store probe reaches into a member table of an older version", "[config][config_file]")
{
  GIVEN("a loaded file whose source then holds an older member holding a newer nested member")
  {
    auto store = config_test::memory_transport::store_t{};
    store["layered"] = arude::config::as_bytes(
      std::string{"version = 1\n[layer]\nversion = 2\nweight = 1\n[layer.inner]\nversion = 1\ndepth = 7\n"});

    auto file = arude::config::config_file<config_file_test::layered, config_test::memory_transport>{
      config_test::memory_transport{"layered", store}
    };
    file.load();

    store["layered"] = arude::config::as_bytes(
      std::string{"version = 1\n[layer]\nversion = 1\n[layer.inner]\nversion = 2\ndepth = 8\n"});
    const auto before = store.at("layered");

    WHEN("it is stored")
    {
      const auto failure = config_file_test::failure_of<arude::config::exception_t>([&file] -> void { file.store(); });

      THEN("the newer nested member is found, the store is refused, and the source is untouched")
      {
        INFO(std::format("The message is: {}", failure.str()));
        REQUIRE(failure.data() == arude::config::config_error::invalid_version);
        REQUIRE(failure.str().contains("inner_v1 is version 2, newer than version 1"));
        REQUIRE(store.at("layered") == before);
      }
    }
  }
}

SCENARIO("overwrite() is the deliberate way past a refused store", "[config][config_file]")
{
  GIVEN("a loaded file whose source holds a newer version")
  {
    auto store = config_test::memory_transport::store_t{};
    store["app"] = arude::config::as_bytes(
      std::string{"version = 1\nname = \"app\"\n[network]\nversion = 3\nhostname = \"future\"\n"});

    auto file = arude::config::config_file<config_file_test::app, config_test::memory_transport>{
      config_test::memory_transport{"app", store}
    };

    auto value = config_file_test::app{};
    value.name = "stored";
    file.set(value);

    WHEN("it is overwritten")
    {
      file.overwrite();

      THEN("the newer source is replaced by the stored root")
      {
        auto second = arude::config::config_file<config_file_test::app, config_test::memory_transport>{
          config_test::memory_transport{"app", store}
        };
        second.load();

        REQUIRE(second.get().name == "stored");
        REQUIRE(second.get().network.value().hostname == "v2-host");
      }
    }
  }
}

SCENARIO("a store on a read-only transport is refused before anything is serialised", "[config][config_file]")
{
  GIVEN("a file over a transport that reports itself read-only, and a set root")
  {
    auto store = config_test::memory_transport::store_t{};

    auto file = arude::config::config_file<config_file_test::app, config_test::memory_transport>{
      config_test::memory_transport{"app", store, false}
    };
    file.set(config_file_test::app{});

    WHEN("it is stored")
    {
      const auto failure = config_file_test::failure_of<arude::config::exception_t>([&file] -> void { file.store(); });

      THEN("it is a read_only")
      {
        REQUIRE(failure.data() == arude::config::config_error::read_only);
        REQUIRE(failure.str().contains("the transport at 'memory:app' is read-only."));
      }
    }
  }
}

SCENARIO("a store that fails below the transport is an io_error", "[config][config_file]")
{
  GIVEN("a loaded file whose transport is made to fail its next write")
  {
    auto store = config_test::memory_transport::store_t{};
    store["app"] = arude::config::as_bytes(config_file_test::current_text());

    // The failure flag lives in the transport, so it is set before the
    // transport is moved into the file.
    auto transport = config_test::memory_transport{"app", store};
    transport.fail_writes();

    auto file = arude::config::config_file<config_file_test::app, config_test::memory_transport>{std::move(transport)};
    file.load();

    WHEN("it is stored")
    {
      const auto failure = config_file_test::failure_of<arude::config::exception_t>([&file] -> void { file.store(); });

      THEN("it is an io_error quoting the transport's failure as text")
      {
        REQUIRE(failure.data() == arude::config::config_error::io_error);
        REQUIRE(failure.str().contains("'memory:app' could not be written"));
        REQUIRE(failure.str().contains("memory transport: the write was made to fail."));
      }
    }
  }

  GIVEN("a file whose transport is made to fail its next read")
  {
    auto store = config_test::memory_transport::store_t{};
    store["app"] = arude::config::as_bytes(config_file_test::current_text());

    auto transport = config_test::memory_transport{"app", store};
    transport.fail_reads();

    auto file = arude::config::config_file<config_file_test::app, config_test::memory_transport>{std::move(transport)};

    WHEN("it is loaded")
    {
      const auto failure = config_file_test::failure_of<arude::config::exception_t>([&file] -> void { file.load(); });

      THEN("it is an io_error quoting the transport's failure as text")
      {
        REQUIRE(failure.data() == arude::config::config_error::io_error);
        REQUIRE(failure.str().contains("'memory:app' could not be read"));
        REQUIRE(failure.str().contains("memory transport: the read was made to fail."));
      }
    }
  }
}

SCENARIO("a load overlapping a store of the same file reads one whole file", "[config][config_file]")
{
  GIVEN("a file stored by one config_file, and a second config_file over the same file")
  {
    // The file gets its own directory, so the count of its siblings is exact:
    // a temporary a store leaves beside it would be found.
    const auto directory = test_helpers::temp_directory{"arude_config_file_overlap"};
    const auto file_path = directory.path() / "app.toml";

    auto writer = arude::config::config_file<config_file_test::app, arude::config::transport_file>{
      arude::config::transport_file{file_path}};
    auto value = config_file_test::app{};
    value.name = "stored";
    writer.set(value);
    writer.store();

    auto reader = arude::config::config_file<config_file_test::app, arude::config::transport_file>{
      arude::config::transport_file{file_path}};

    WHEN("one thread stores it over and over while another loads it with the create policy")
    {
      static constexpr auto rounds = 300;

      using arude::bitwise_operators::operator|;

      auto loads_at_defaults = 0;
      auto loads_failed = 0;
      auto stores_refused = 0;

      {
        auto storing = std::jthread{
          [&writer, &stores_refused] -> void
          {
            for(auto round = 0; round < rounds; ++round)
            {
              try
              {
                writer.store();
              }
              catch(const arude::config::exception_t&)
              {
                ++stores_refused;
              }
            }
          }};

        for(auto round = 0; round < rounds; ++round)
        {
          try
          {
            reader.load(arude::config::load_policy::create | arude::config::load_policy::upgrade_to_current);

            if(reader.get().name != "stored")
            {
              ++loads_at_defaults;
            }
          }
          catch(const arude::config::exception_t&)
          {
            ++loads_failed;
          }
        }
      }

      THEN("no load fell back to defaults, and none failed")
      {
        REQUIRE(stores_refused < rounds);
        REQUIRE(loads_at_defaults == 0);
        REQUIRE(loads_failed == 0);
      }

      THEN("no temporary file was left beside it")
      {
        const auto siblings = std::ranges::distance(
          std::filesystem::directory_iterator{directory.path()} |
          std::views::filter(
            [&file_path](const std::filesystem::directory_entry& entry) -> bool
            { return entry.path().filename().string().contains(file_path.filename().string()); }));
        REQUIRE(siblings == 1);
      }
    }
  }
}

#if (defined _WIN32)

// On Windows a store replaces the file with POSIX rename semantics: a reader
// that shares deletion, as read_file opens it, does not stop the store, while
// a foreign handle without FILE_SHARE_DELETE fails it at once. These pin both.
SCENARIO("a store over a file held open without shared deletion fails quickly", "[config][config_file][windows]")
{
  GIVEN("a stored file held open by a handle that does not share its deletion")
  {
    const auto directory = test_helpers::temp_directory{"arude_config_file_held"};
    const auto file_path = directory.path() / "app.toml";

    auto file = arude::config::config_file<config_file_test::app, arude::config::transport_file>{
      arude::config::transport_file{file_path}};
    auto value = config_file_test::app{};
    value.name = "stored";
    file.set(value);
    file.store();

    // As a foreign reader (an editor, or another process still on
    // std::ifstream) opens it: read and write, but no deletion.
    const auto handle = ::CreateFileW(
      file_path.c_str(),
      GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_WRITE,
      nullptr,
      OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL,
      nullptr);
    REQUIRE(handle != INVALID_HANDLE_VALUE);

    value.name = "newer";
    file.set(value);

    WHEN("it is stored")
    {
      THEN("the store is an io_error within a second, and no temporary file is left behind")
      {
        const auto started = std::chrono::steady_clock::now();
        auto io_error = false;

        try
        {
          file.store();
        }
        catch(const arude::config::exception_t& ex)
        {
          io_error = ex.data() == arude::config::config_error::io_error;
        }

        ::CloseHandle(handle);

        REQUIRE(io_error);
        REQUIRE(std::chrono::steady_clock::now() - started < std::chrono::seconds{1});
        REQUIRE(std::ranges::distance(std::filesystem::directory_iterator{directory.path()}) == 1);
      }
    }
  }
}

SCENARIO("a reader sharing the file's deletion does not stop a store", "[config][config_file][windows]")
{
  GIVEN("a stored file held open the way read_file holds it")
  {
    const auto directory = test_helpers::temp_directory{"arude_config_file_sharing"};
    const auto file_path = directory.path() / "app.toml";

    auto file = arude::config::config_file<config_file_test::app, arude::config::transport_file>{
      arude::config::transport_file{file_path}};
    auto value = config_file_test::app{};
    value.name = "stored";
    file.set(value);
    file.store();

    // read_file grants FILE_SHARE_DELETE, which is what lets a store replace
    // the file while it is open.
    const auto handle = ::CreateFileW(
      file_path.c_str(),
      GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
      nullptr,
      OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL,
      nullptr);
    REQUIRE(handle != INVALID_HANDLE_VALUE);

    value.name = "newer";
    file.set(value);

    WHEN("it is stored while the reader holds the file")
    {
      THEN("the store succeeds, and a load afterwards sees the new value")
      {
        file.store();
        ::CloseHandle(handle);

        auto reader = arude::config::config_file<config_file_test::app, arude::config::transport_file>{
          arude::config::transport_file{file_path}};
        reader.load();

        REQUIRE(reader.get().name == "newer");
      }
    }
  }
}

#endif // #if (defined _WIN32)

SCENARIO("of two loads under way at once, the later one's read is the one that stands", "[config][config_file]")
{
  GIVEN("a store holding one text, read by a load that a second load overtakes")
  {
    auto store = config_test::memory_transport::store_t{};
    store["app"] = arude::config::as_bytes(
      std::string{"version = 1\nname = \"first read\"\n"
                  "[network]\nversion = 1\nhost = \"old-host\"\n"
                  "[extra]\nversion = 1\ndepth = 3\n"});

    // The step runs inside the first load's read, which starts a second
    // load of a text the source has moved on to; the first load then
    // finishes with the first text, and must not install it over the
    // second. The step takes the file by pointer, filled in once the file
    // is constructed, because the transport the step runs on is moved into
    // that construction.
    auto* file_pointer =
      static_cast<arude::config::config_file<config_file_test::app, config_test::memory_transport>*>(nullptr);

    auto transport = config_test::memory_transport{"app", store};
    transport.run_on_read(
      [&store, &file_pointer] -> void
      {
        store["app"] = arude::config::as_bytes(
          std::string{"version = 1\nname = \"second read\"\n"
                      "[network]\nversion = 2\nhostname = \"new-host\"\nport = 80\n"
                      "[network.inner]\nversion = 1\ndepth = 7\n"
                      "[[network.extras]]\nversion = 1\ndepth = 7\n"
                      "[extra]\nversion = 1\ndepth = 3\n"});
        file_pointer->load();
      });

    auto file = arude::config::config_file<config_file_test::app, config_test::memory_transport>{std::move(transport)};
    file_pointer = &file;

    WHEN("the first load finishes after the second")
    {
      file.load();

      THEN("the cached root holds the second read")
      {
        REQUIRE(file.get().name == "second read");
        REQUIRE(file.get().network.value().hostname == "new-host");
      }
    }
  }
}

SCENARIO("of two stores under way at once, the newer snapshot is written last", "[config][config_file]")
{
  GIVEN("a loaded file whose transport holds its next write until the test lets it go")
  {
    auto store = config_test::memory_transport::store_t{};
    store["app"] = arude::config::as_bytes(config_file_test::current_text());

    // Declared in this order so that a held write is let go on every way
    // out of the scope, a failed REQUIRE included: release is destroyed
    // before the futures, which wait for their threads, and a promise
    // destroyed unset still wakes whoever waits on it.
    auto held = std::promise<void>{};
    auto first = std::future<void>{};
    auto second = std::future<void>{};
    auto release = std::promise<void>{};

    auto transport = config_test::memory_transport{"app", store};
    transport.run_on_write(
      [&held, released = release.get_future().share()] -> void
      {
        held.set_value();
        released.wait();
      });

    auto file = arude::config::config_file<config_file_test::app, config_test::memory_transport>{std::move(transport)};
    file.load();

    WHEN("one store is held in its write, and a second starts once a newer value is set")
    {
      auto value = config_file_test::app{};
      value.name = "older";
      file.set(value);

      first = std::async(std::launch::async, [&file] -> void { file.store(); });
      held.get_future().wait();

      // The held store holds no lock on the cache, so this does not wait.
      value.name = "newer";
      file.set(value);
      second = std::async(std::launch::async, [&file] -> void { file.store(); });

      // Bounded, as a second store that waits for the first would otherwise
      // never return here. Where stores are ordered the result does not
      // depend on the bound.
      const auto overtook = second.wait_for(std::chrono::milliseconds{200}) == std::future_status::ready;

      release.set_value();
      first.get();
      second.get();

      THEN("the second store did not finish while the first was held")
      {
        REQUIRE(!overtook);
      }

      THEN("the source holds the newer value")
      {
        REQUIRE(arude::config::as_text(store.at("app")).contains("name = 'newer'"));
        REQUIRE(!arude::config::as_text(store.at("app")).contains("name = 'older'"));
      }
    }
  }
}

SCENARIO("a slow load on one file does not block a get on another", "[config][config_file]")
{
  GIVEN("two files, one of them slow, and a load of the slow one in flight")
  {
    auto store = config_test::memory_transport::store_t{};
    store["slow"] = arude::config::as_bytes(
      std::format(
        "version = 1\nname = \"loaded\"\n{}\n[extra]\nversion = 1\ndepth = 3\n", config_file_test::network_block));
    store["fast"] = arude::config::as_bytes(
      std::format(
        "version = 1\nname = \"fast\"\n{}\n[extra]\nversion = 1\ndepth = 3\n", config_file_test::network_block));

    auto gate = std::make_shared<config_test::gate>();
    auto slow_transport = config_test::memory_transport{"slow", store};
    slow_transport.hold_reads_on(gate);

    auto slow =
      arude::config::config_file<config_file_test::app, config_test::memory_transport>{std::move(slow_transport)};

    auto fast = arude::config::config_file<config_file_test::app, config_test::memory_transport>{
      config_test::memory_transport{"fast", store}
    };
    fast.load();

    auto loading = std::thread{[&slow] -> void { slow.load(); }};

    WHEN("get() is called on the other file while the slow load is blocked")
    {
      // A value before the release is the whole proof: had the slow load
      // held anything get() needs, this call would still be blocked and the
      // test would time out.
      const auto seen = fast.get().name;

      {
        const auto lock = std::scoped_lock{gate->mutex};
        gate->released = true;
      }
      gate->ready.notify_all();
      loading.join();

      THEN("it returns without waiting for the slow load")
      {
        REQUIRE(seen == "fast");
      }

      THEN("the slow load finishes with its file's value")
      {
        REQUIRE(slow.get().name == "loaded");
      }
    }
  }
}

SCENARIO("a get on a file is serialized against a load of its own", "[config][config_file]")
{
  GIVEN("a file with a cached value and a slow load of it in flight")
  {
    auto store = config_test::memory_transport::store_t{};
    store["slow"] = arude::config::as_bytes(
      std::format(
        "version = 1\nname = \"loaded\"\n{}\n[extra]\nversion = 1\ndepth = 3\n", config_file_test::network_block));

    auto gate = std::make_shared<config_test::gate>();
    auto transport = config_test::memory_transport{"slow", store};
    transport.hold_reads_on(gate);

    auto file = arude::config::config_file<config_file_test::app, config_test::memory_transport>{std::move(transport)};

    auto before = config_file_test::app{};
    before.name = "before";
    file.set(before);

    auto loading = std::thread{[&file] -> void { file.load(); }};

    WHEN("get() is called while the load has not finished parsing")
    {
      // The slow read runs outside the file's lock, so this returns promptly
      // with the value from before the load started.
      const auto seen = file.get().name;

      {
        const auto lock = std::scoped_lock{gate->mutex};
        gate->released = true;
      }
      gate->ready.notify_all();
      loading.join();

      THEN("it sees the cached value, not a half-loaded one")
      {
        REQUIRE(seen == "before");
      }

      THEN("once the load has committed, the new value is what get() sees")
      {
        REQUIRE(file.get().name == "loaded");
      }
    }
  }
}

SCENARIO("a store probe reads the source leniently", "[config][config_file]")
{
  GIVEN("a file with a set root, whose source declares a version and nothing else")
  {
    auto store = config_test::memory_transport::store_t{};
    store["app"] = arude::config::as_bytes(std::string{"version = 1\n"});

    auto file = arude::config::config_file<config_file_test::app, config_test::memory_transport>{
      config_test::memory_transport{"app", store}
    };

    auto value = config_file_test::app{};
    value.name = "stored";
    file.set(value);

    WHEN("it is stored")
    {
      file.store();

      THEN("the probe accepted the sparse source, and the store replaced it")
      {
        auto second = arude::config::config_file<config_file_test::app, config_test::memory_transport>{
          config_test::memory_transport{"app", store}
        };
        second.load();

        REQUIRE(second.get().name == "stored");
      }
    }
  }
}

SCENARIO("a store of a value TOML cannot hold leaves the source untouched", "[config][config_file]")
{
  GIVEN("a file whose root holds a flag value naming no flags")
  {
    auto store = config_test::memory_transport::store_t{};
    store["flags"] = arude::config::as_bytes(std::string{"version = 1\nmode = \"a\"\n"});

    auto file = arude::config::config_file<config_file_test::flaggy, config_test::memory_transport>{
      config_test::memory_transport{"flags", store}
    };
    file.load();

    const auto before = store.at("flags");

    auto value = file.get();
    value.mode = static_cast<config_file_test::flags>(-2);
    file.set(value);

    WHEN("it is stored")
    {
      const auto failure = config_file_test::failure_of<arude::config::exception_t>([&file] -> void { file.store(); });

      THEN("it is an invalid_payload, and the source is untouched")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
        REQUIRE(failure.str().contains("'mode' holds -2, which names no enumerator of "));
        REQUIRE(store.at("flags") == before);
      }
    }
  }
}

SCENARIO("an exception_t from the transport passes through unchanged", "[config][config_file]")
{
  GIVEN("a file whose transport throws an exception_t on its next read")
  {
    auto store = config_test::memory_transport::store_t{};
    store["app"] = arude::config::as_bytes(config_file_test::current_text());

    auto transport = config_test::memory_transport{"app", store};
    transport.throw_on_read(
      arude::config::exception_t{
        "arude::config: the transport refused.", arude::config::config_error::invalid_location});

    auto file = arude::config::config_file<config_file_test::app, config_test::memory_transport>{std::move(transport)};

    WHEN("it is loaded")
    {
      const auto failure = config_file_test::failure_of<arude::config::exception_t>([&file] -> void { file.load(); });

      THEN("the code and the message are the transport's own")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_location);
        REQUIRE(failure.str() == "arude::config: the transport refused.");
      }
    }
  }
}

SCENARIO("reject_unknown_fields refuses what the root does not declare", "[config][config_file]")
{
  GIVEN("a file over a strict format, with a cached value, and the three sources that hold an extra key")
  {
    auto store = config_test::memory_transport::store_t{};

    auto file = arude::config::config_file<config_file_test::app, config_test::memory_transport>{
      config_test::memory_transport{"app", store},
      arude::config::toml_format{.reject_unknown_fields = true}
    };

    auto lenient = arude::config::config_file<config_file_test::app, config_test::memory_transport>{
      config_test::memory_transport{"app", store}
    };

    auto cached = config_file_test::app{};
    cached.name = "cached";
    file.set(cached);

    const auto root_extra = std::format(
      "version = 1\nname = \"app\"\nstray = 7\n{}\n[extra]\nversion = 1\ndepth = 3\n", config_file_test::network_block);
    const auto member_extra = std::string{"version = 1\nname = \"app\"\n"
                                          "[network]\nversion = 2\nhostname = \"source-host\"\nport = 80\nbogus = 1\n"
                                          "[network.inner]\nversion = 1\ndepth = 7\n"
                                          "[[network.extras]]\nversion = 1\ndepth = 7\n"
                                          "[extra]\nversion = 1\ndepth = 3\n"};
    const auto older_member_extra = std::string{"version = 1\nname = \"app\"\n"
                                                "[network]\nversion = 1\nhost = \"old-host\"\nbogus = 1\n"
                                                "[extra]\nversion = 1\ndepth = 3\n"};

    WHEN("each source is loaded through the strict file")
    {
      THEN("the root's extra key is an invalid_payload, and the cache is untouched")
      {
        store["app"] = arude::config::as_bytes(root_extra);

        const auto failure = config_file_test::failure_of<arude::config::exception_t>([&file] -> void { file.load(); });

        REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
        REQUIRE(failure.str().contains("not used"));
        REQUIRE(file.get().name == "cached");
      }

      THEN("the member's extra key is an invalid_payload, and the cache is untouched")
      {
        store["app"] = arude::config::as_bytes(member_extra);

        const auto failure = config_file_test::failure_of<arude::config::exception_t>([&file] -> void { file.load(); });

        REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
        REQUIRE(failure.str().contains("not used"));
        REQUIRE(file.get().name == "cached");
      }

      THEN("an extra key in an older member being migrated is an invalid_payload, and the cache is untouched")
      {
        store["app"] = arude::config::as_bytes(older_member_extra);

        const auto failure = config_file_test::failure_of<arude::config::exception_t>([&file] -> void { file.load(); });

        REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
        REQUIRE(failure.str().contains("not used"));
        REQUIRE(file.get().name == "cached");
      }
    }

    WHEN("each source is loaded through the default format")
    {
      THEN("all three are accepted")
      {
        for(const auto& text : {root_extra, member_extra, older_member_extra})
        {
          store["app"] = arude::config::as_bytes(text);
          lenient.load();
          REQUIRE(lenient.get().name == "app");
        }
      }
    }
  }
}
