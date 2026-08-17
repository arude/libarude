///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///

#include "arude/config.hpp"
#include "arude/exception.hpp"
#include "arude/non_owning_t.hpp"
#include "test/helpers/config_test_v2.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace config_manager_test
{

///
/// A transport holding one slot's bytes in memory, so a test can register
/// several documents without touching the filesystem.
///
class transport_memory final
{
public: // Typedefs
  using store_t = std::map<std::string, std::vector<std::byte>>;

public: // Constants
  static constexpr auto name = std::string_view{"eeprom"};

public: // Structors
  ///
  /// \param slot Which slot this transport reads and writes.
  /// \param store Where the bytes live. Held by reference and must outlive this.
  ///
  transport_memory(std::string_view slot, store_t& store);

public: // Accessors
  [[nodiscard]] auto location() const -> std::string;
  [[nodiscard]] auto writable() const -> bool;
  [[nodiscard]] auto exists() const -> bool;
  [[nodiscard]] auto read() const -> std::vector<std::byte>;

public: // Methods
  auto write(std::span<const std::byte> bytes) const -> void;

private: // Variables
  std::string slot_;
  arude::non_owning_t<store_t> store_;
};

///
///
transport_memory::transport_memory(const std::string_view slot, store_t& store)
  : slot_{slot}
  , store_{&store}
{
}

///
///
auto transport_memory::location() const -> std::string
{
  return std::format("{}:{}", name, slot_);
}

///
///
// NOLINTNEXTLINE(readability-convert-member-functions-to-static): the contract is an instance operation.
auto transport_memory::writable() const -> bool
{
  return true;
}

///
///
auto transport_memory::exists() const -> bool
{
  return store_->contains(slot_);
}

///
///
auto transport_memory::read() const -> std::vector<std::byte>
{
  return store_->at(slot_);
}

///
///
auto transport_memory::write(const std::span<const std::byte> bytes) const -> void
{
  store_->insert_or_assign(slot_, std::vector<std::byte>{cbegin(bytes), cend(bytes)});
}

} // namespace config_manager_test

static_assert(arude::config::transport_c<config_manager_test::transport_memory>);

SCENARIO("a location can only be registered once", "[config][config_manager]")
{
  GIVEN("a manager and one registered document")
  {
    auto store = config_manager_test::transport_memory::store_t{};
    auto manager = arude::config::config_manager{};

    std::ignore =
      manager.register_document(arude::config::endpoint_toml{}, config_manager_test::transport_memory{"slot", store});

    THEN("the manager knows the location and its size")
    {
      REQUIRE(manager.contains("eeprom:slot"));
      REQUIRE(manager.size() == 1);
    }

    THEN("registering the same location again is an already_registered_location")
    {
      try
      {
        std::ignore = manager.register_document(
          arude::config::endpoint_toml{}, config_manager_test::transport_memory{"slot", store});
        FAIL("registering a location twice should have thrown");
      }
      catch(const arude::config::exception_t& ex)
      {
        // Not invalid_location: there is nothing wrong with the location, only
        // with registering it a second time.
        REQUIRE(ex.data() == arude::config::config_error::already_registered_location);
      }
    }
  }
}

SCENARIO("get<T> answers for the type index", "[config][config_manager]")
{
  GIVEN("a manager with one document holding a root section")
  {
    auto store = config_manager_test::transport_memory::store_t{};
    auto manager = arude::config::config_manager{};
    auto document =
      manager.register_document(arude::config::endpoint_toml{}, config_manager_test::transport_memory{"slot", store});
    auto root = document.add_root<test_helpers::config_test_t>();

    WHEN("the section is set")
    {
      auto written = test_helpers::config_test_t{};
      written.name = "through the index";

      root.set(written);

      THEN("get<T>() returns it")
      {
        REQUIRE(manager.get<test_helpers::config_test_t>().name == "through the index");
      }

      THEN("find<T>() is the same handle")
      {
        REQUIRE(manager.find<test_helpers::config_test_t>().get().name == "through the index");
      }
    }

    THEN("a type nobody registered is a not_found")
    {
      try
      {
        std::ignore = manager.get<test_helpers::config_test_v1>();
        FAIL("getting an unregistered type should have thrown");
      }
      catch(const arude::config::exception_t& ex)
      {
        REQUIRE(ex.data() == arude::config::config_error::not_found);
      }
    }
  }

  GIVEN("a type registered in two documents")
  {
    auto store = config_manager_test::transport_memory::store_t{};
    auto manager = arude::config::config_manager{};
    auto first =
      manager.register_document(arude::config::endpoint_toml{}, config_manager_test::transport_memory{"first", store});
    auto second =
      manager.register_document(arude::config::endpoint_toml{}, config_manager_test::transport_memory{"second", store});

    std::ignore = first.add_root<test_helpers::config_test_t>();
    std::ignore = second.add_root<test_helpers::config_test_t>();

    THEN("get<T>() is an ambiguous_type naming the type and both locations")
    {
      try
      {
        std::ignore = manager.get<test_helpers::config_test_t>();
        FAIL("getting an ambiguous type should have thrown");
      }
      catch(const arude::config::exception_t& ex)
      {
        REQUIRE(ex.data() == arude::config::config_error::ambiguous_type);
        REQUIRE(ex.str().contains("eeprom:first"));
        REQUIRE(ex.str().contains("eeprom:second"));
      }
    }
  }
}

SCENARIO("a document handle outlives its manager", "[config][config_manager]")
{
  GIVEN("a manager that registered a document and then went away")
  {
    auto store = config_manager_test::transport_memory::store_t{};
    auto root = [&]() -> arude::config::config_handle<test_helpers::config_test_t>
    {
      auto manager = arude::config::config_manager{};
      auto document =
        manager.register_document(arude::config::endpoint_toml{}, config_manager_test::transport_memory{"slot", store});
      return document.add_root<test_helpers::config_test_t>();
    }();

    WHEN("the handle is used after the manager's destruction")
    {
      auto written = test_helpers::config_test_t{};
      written.name = "outliving";

      root.set(written);
      root.store();
      root.load();

      THEN("it still reaches its document")
      {
        REQUIRE(root.get().name == "outliving");
      }
    }
  }
}
