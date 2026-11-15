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
#include <format>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
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

///
/// A transport with nothing behind it, holding a token that every copy shares,
/// so a test can tell from a std::weak_ptr to the token whether the document
/// the transport was moved into still exists.
///
class transport_tracked final
{
public: // Constants
  static constexpr auto name = std::string_view{"tracked"};

public: // Structors
  ///
  /// \param token The token to hold. Kept, in this and in every copy.
  ///
  explicit transport_tracked(std::shared_ptr<const int> token);

public: // Accessors
  [[nodiscard]] auto location() const -> std::string;
  [[nodiscard]] auto writable() const -> bool;
  [[nodiscard]] auto exists() const -> bool;
  [[nodiscard]] auto read() const -> std::vector<std::byte>;

public: // Methods
  auto write(std::span<const std::byte> bytes) const -> void;

private: // Variables
  std::shared_ptr<const int> token_;
};

///
///
transport_tracked::transport_tracked(std::shared_ptr<const int> token)
  : token_{std::move(token)}
{
}

///
///
// NOLINTNEXTLINE(readability-convert-member-functions-to-static): the contract is an instance operation.
auto transport_tracked::location() const -> std::string
{
  return std::format("{}:slot", name);
}

///
///
// NOLINTNEXTLINE(readability-convert-member-functions-to-static): the contract is an instance operation.
auto transport_tracked::writable() const -> bool
{
  return true;
}

///
///
// NOLINTNEXTLINE(readability-convert-member-functions-to-static): the contract is an instance operation.
auto transport_tracked::exists() const -> bool
{
  return false;
}

///
///
// NOLINTNEXTLINE(readability-convert-member-functions-to-static): the contract is an instance operation.
auto transport_tracked::read() const -> std::vector<std::byte>
{
  return {};
}

///
///
// NOLINTNEXTLINE(readability-convert-member-functions-to-static): the contract is an instance operation.
auto transport_tracked::write([[maybe_unused]] const std::span<const std::byte> bytes) const -> void
{
}

} // namespace config_manager_test

static_assert(arude::config::transport_c<config_manager_test::transport_memory>);
static_assert(arude::config::transport_c<config_manager_test::transport_tracked>);

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

  GIVEN("a manager that registered a document and went away before any section was added")
  {
    auto store = config_manager_test::transport_memory::store_t{};
    auto document = [&]() -> arude::config::config_document_handle
    {
      auto manager = arude::config::config_manager{};
      return manager.register_document(
        arude::config::endpoint_toml{}, config_manager_test::transport_memory{"slot", store});
    }();

    WHEN("a root and a named section are added through the surviving handle")
    {
      // Adding a section registers its type, and the index the registration
      // goes into was the manager's.
      auto root = document.add_root<test_helpers::config_test_t>();
      auto legacy = document.add_section<test_helpers::config_test_v1>("legacy");

      THEN("the sections are attached, and as empty as any other new section")
      {
        REQUIRE(root.name().empty());
        REQUIRE(legacy.name() == "legacy");
        REQUIRE_FALSE(root.loaded());
        REQUIRE_FALSE(legacy.loaded());
      }

      AND_WHEN("both are set and stored, and the cache is then changed and reloaded")
      {
        auto written_root = test_helpers::config_test_t{};
        written_root.name = "stored root";

        auto written_legacy = test_helpers::config_test_v1{};
        written_legacy.name = "stored section";

        root.set(written_root);
        legacy.set(written_legacy);
        root.store();

        // Reaches the cache only, so reading the stored names back below
        // shows that load() went to the transport.
        auto unsaved = test_helpers::config_test_t{};
        unsaved.name = "cache only";
        root.set(unsaved);

        legacy.load();

        THEN("the transport was written")
        {
          REQUIRE(store.contains("slot"));
        }

        THEN("both handles return what was stored")
        {
          REQUIRE(root.get().name == "stored root");
          REQUIRE(legacy.get().name == "stored section");
        }
      }
    }
  }

  // The manager's index holds every document and section registered with it,
  // so a document that held the index in turn would keep both alive for good.
  GIVEN("a manager, a document registered with it and sections added to that, all gone")
  {
    auto token = std::make_shared<const int>(0);
    const auto tracked = std::weak_ptr<const int>{token};

    {
      auto manager = arude::config::config_manager{};
      auto document = manager.register_document(
        arude::config::endpoint_toml{}, config_manager_test::transport_tracked{std::move(token)});
      auto root = document.add_root<test_helpers::config_test_t>();
      auto legacy = document.add_section<test_helpers::config_test_v1>("legacy");

      REQUIRE(!tracked.expired());
    }

    THEN("the document went with them, as nothing it holds keeps it alive")
    {
      REQUIRE(tracked.expired());
    }
  }
}

SCENARIO("a manager is constructed in place", "[config][config_manager]")
{
  GIVEN("the manager type")
  {
    THEN("it cannot be copied")
    {
      STATIC_REQUIRE_FALSE(std::is_copy_constructible_v<arude::config::config_manager>);
      STATIC_REQUIRE_FALSE(std::is_copy_assignable_v<arude::config::config_manager>);
    }

    THEN("it cannot be moved either")
    {
      // The handles are what travels; the manager stays where it was built.
      STATIC_REQUIRE_FALSE(std::is_move_constructible_v<arude::config::config_manager>);
      STATIC_REQUIRE_FALSE(std::is_move_assignable_v<arude::config::config_manager>);
    }
  }
}
