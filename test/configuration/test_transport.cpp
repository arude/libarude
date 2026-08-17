///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///

#include "arude/config.hpp"
#include "arude/non_owning_t.hpp"
#include "test/helpers/as_text.hpp"
#include "test/helpers/config_test_v2.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <format>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace transport_test
{

///
/// A transport holding one slot's bytes in memory, which is what an
/// application's own looks like: no filesystem, no network, and read-only if
/// it says so. Stands in for the EEPROM case, and lets these tests run
/// without either.
///
class transport_memory final
{
public: // Typedefs / Constants
  ///
  /// Where the bytes live, one slot per test.
  ///
  using store_t = std::map<std::string, std::vector<std::byte>>;

  ///
  /// \see transport_c
  ///
  static constexpr auto name = std::string_view{"eeprom"};

public: // Structors
  ///
  /// Constructs a transport over one slot of a store the test keeps.
  ///
  /// \param slot Which slot this transport reads and writes.
  /// \param store Where the bytes live. Held by reference and must outlive this.
  /// \param writable Whether writes are allowed.
  ///
  transport_memory(std::string_view slot, store_t& store, bool writable);

public: // Accessors
  ///
  /// \see transport_c
  ///
  [[nodiscard]] auto location() const -> std::string;

  ///
  /// \see transport_c
  ///
  [[nodiscard]] auto writable() const -> bool;

  ///
  /// \see transport_c
  ///
  [[nodiscard]] auto exists() const -> bool;

  ///
  /// \see transport_c
  ///
  [[nodiscard]] auto read() const -> std::vector<std::byte>;

public: // Methods
  ///
  /// \see transport_c
  ///
  auto write(std::span<const std::byte> bytes) const -> void;

private: // Variables
  std::string slot_;
  arude::non_owning_t<store_t> store_;
  bool writable_;
};

///
///
transport_memory::transport_memory(const std::string_view slot, store_t& store, const bool writable)
  : slot_{slot}
  , store_{&store}
  , writable_{writable}
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
auto transport_memory::writable() const -> bool
{
  return writable_;
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
  const auto found = store_->find(slot_);

  if(found == store_->end())
  {
    throw arude::exception{"transport_test: nothing stored under that slot"};
  }

  return found->second;
}

///
///
auto transport_memory::write(const std::span<const std::byte> bytes) const -> void
{
  store_->insert_or_assign(slot_, std::vector<std::byte>{bytes.begin(), bytes.end()});
}

} // namespace transport_test

static_assert(arude::config::transport_c<transport_test::transport_memory>);

SCENARIO("a document round-trips through a transport of an application's own", "[config][transport]")
{
  GIVEN("a manager, a memory transport and a document over it")
  {
    auto store = transport_test::transport_memory::store_t{};
    auto manager = arude::config::config_manager{};
    auto document = manager.register_document(
      arude::config::endpoint_toml{}, transport_test::transport_memory{"device", store, true});
    auto root = document.add_root<test_helpers::config_test_t>();

    WHEN("a configuration is stored through it")
    {
      auto written = test_helpers::config_test_t{};
      written.name = "device";

      root.set(written);
      document.store();

      THEN("the bytes went to the store rather than to a file")
      {
        REQUIRE(store.contains("device"));
        REQUIRE(test_helpers::as_text(store.at("device")).contains("device"));
      }

      THEN("it reads back through the same transport")
      {
        document.load();
        REQUIRE(root.get().name == "device");
      }
    }
  }
}

SCENARIO("a document at a location with nothing behind it is a not_found", "[config][transport]")
{
  GIVEN("a document over a memory transport with nothing stored")
  {
    auto store = transport_test::transport_memory::store_t{};
    auto manager = arude::config::config_manager{};
    auto document = manager.register_document(
      arude::config::endpoint_toml{}, transport_test::transport_memory{"absent", store, true});
    std::ignore = document.add_root<test_helpers::config_test_t>();

    THEN("loading it is a not_found")
    {
      try
      {
        document.load();
        FAIL("loading an absent slot should have thrown");
      }
      catch(const arude::config::exception_t& ex)
      {
        REQUIRE(ex.data() == arude::config::config_error::not_found);
      }
    }
  }
}

SCENARIO("a read-only transport refuses to be written", "[config][transport]")
{
  GIVEN("a document over a read-only store already holding a configuration")
  {
    auto store = transport_test::transport_memory::store_t{};

    // Seeded through a writable document, so the read-only one has something
    // to read: what is being tested is the writing, not the reading.
    auto writer = arude::config::config_manager{};
    auto writer_document =
      writer.register_document(arude::config::endpoint_toml{}, transport_test::transport_memory{"device", store, true});
    auto writer_root = writer_document.add_root<test_helpers::config_test_t>();
    writer_root.set(test_helpers::config_test_t{});
    writer_document.store();

    auto manager = arude::config::config_manager{};
    auto document = manager.register_document(
      arude::config::endpoint_toml{}, transport_test::transport_memory{"device", store, false});
    auto root = document.add_root<test_helpers::config_test_t>();

    THEN("reading works")
    {
      document.load();
      REQUIRE(root.get().version == arude::config::version_of<test_helpers::config_test_t>());
    }

    THEN("storing is a read_only, and nothing was serialized to get there")
    {
      try
      {
        document.store();
        FAIL("storing through a read-only transport should have thrown");
      }
      catch(const arude::config::exception_t& ex)
      {
        REQUIRE(ex.data() == arude::config::config_error::read_only);
      }
    }

    THEN("the create policy cannot write one either")
    {
      auto absent_manager = arude::config::config_manager{};
      auto absent_document = absent_manager.register_document(
        arude::config::endpoint_toml{}, transport_test::transport_memory{"absent", store, false});
      std::ignore = absent_document.add_root<test_helpers::config_test_t>();

      constexpr auto policy = arude::config::load_policy::create;

      try
      {
        absent_document.load(policy);
        FAIL("creating through a read-only transport should have thrown");
      }
      catch(const arude::config::exception_t& ex)
      {
        REQUIRE(ex.data() == arude::config::config_error::read_only);
      }
    }
  }
}
