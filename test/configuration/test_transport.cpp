///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// config_file over a transport of an application's own: the contract is a
/// concept, so anything that satisfies transport_c works, no filesystem or
/// network required.
///

#include "arude/config.hpp"
#include "arude/non_owning_t.hpp"
#include "test/helpers/config_test_v2.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <format>
#include <map>
#include <optional>
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
  [[nodiscard]] auto read() const -> std::optional<std::vector<std::byte>>;

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
auto transport_memory::read() const -> std::optional<std::vector<std::byte>>
{
  if(const auto found = store_->find(slot_); found != store_->end())
  {
    return found->second;
  }

  return std::nullopt;
}

///
///
auto transport_memory::write(const std::span<const std::byte> bytes) const -> void
{
  store_->insert_or_assign(slot_, std::vector<std::byte>{bytes.begin(), bytes.end()});
}

///
/// A transport whose every read fails, as a device does that is there but
/// cannot be read just now.
///
class transport_failing final
{
public: // Typedefs / Constants
  ///
  /// \see transport_c
  ///
  static constexpr auto name = std::string_view{"failing"};

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
  /// \throws arude::config::exception_t With io_error, always.
  ///
  [[nodiscard]] auto read() const -> std::optional<std::vector<std::byte>>;

public: // Methods
  ///
  /// \see transport_c
  ///
  auto write(std::span<const std::byte> bytes) const -> void;
};

///
///
// NOLINTNEXTLINE(readability-convert-member-functions-to-static): the contract is an instance operation.
auto transport_failing::location() const -> std::string
{
  return std::format("{}:device", name);
}

///
///
// NOLINTNEXTLINE(readability-convert-member-functions-to-static): the contract is an instance operation.
auto transport_failing::writable() const -> bool
{
  return true;
}

///
///
// NOLINTNEXTLINE(readability-convert-member-functions-to-static): the contract is an instance operation.
auto transport_failing::read() const -> std::optional<std::vector<std::byte>>
{
  throw arude::config::exception_t{"transport_test: the device did not answer.", arude::config::config_error::io_error};
}

///
///
// NOLINTNEXTLINE(readability-convert-member-functions-to-static): the contract is an instance operation.
auto transport_failing::write([[maybe_unused]] const std::span<const std::byte> bytes) const -> void
{
}

} // namespace transport_test

static_assert(arude::config::transport_c<transport_test::transport_memory>);
static_assert(arude::config::transport_c<transport_test::transport_failing>);

SCENARIO("a file round-trips through a transport of an application's own", "[config][transport]")
{
  GIVEN("a memory transport and a file over it")
  {
    auto store = transport_test::transport_memory::store_t{};
    auto file = arude::config::config_file<test_helpers::config_test_t, transport_test::transport_memory>{
      transport_test::transport_memory{"device", store, true}
    };

    WHEN("a configuration is stored through it")
    {
      auto written = test_helpers::config_test_t{};
      written.name = "device";

      file.set(written);
      file.store();

      THEN("the bytes went to the store rather than to a file")
      {
        REQUIRE(store.contains("device"));
        REQUIRE(arude::config::as_text(store.at("device")).contains("device"));
      }

      THEN("it reads back through the same transport")
      {
        file.load();
        REQUIRE(file.get().name == "device");
      }
    }
  }
}

SCENARIO("a file at a location with nothing behind it is a not_found", "[config][transport]")
{
  GIVEN("a file over a memory transport with nothing stored")
  {
    auto store = transport_test::transport_memory::store_t{};
    auto file = arude::config::config_file<test_helpers::config_test_t, transport_test::transport_memory>{
      transport_test::transport_memory{"absent", store, true}
    };

    THEN("loading it is a not_found")
    {
      try
      {
        file.load();
        FAIL("loading an absent slot should have thrown");
      }
      catch(const arude::config::exception_t& ex)
      {
        REQUIRE(ex.data() == arude::config::config_error::not_found);
      }
    }
  }
}

// Only std::nullopt says that nothing is there. A read that fails is not that,
// and must not be taken for it: the create policy would hand out defaults, and
// the next store would write them over a source that was there all along.
SCENARIO("a transport whose read fails is not a missing source", "[config][transport]")
{
  GIVEN("a file over a transport whose read throws")
  {
    auto file = arude::config::config_file<test_helpers::config_test_t, transport_test::transport_failing>{
      transport_test::transport_failing{}};

    THEN("a load with the create policy is the transport's io_error, and creates nothing")
    {
      try
      {
        file.load(arude::config::load_policy::create);
        FAIL("a load whose read fails should have thrown");
      }
      catch(const arude::config::exception_t& ex)
      {
        REQUIRE(ex.data() == arude::config::config_error::io_error);
        REQUIRE(ex.str().contains("did not answer"));
      }

      REQUIRE(!file.loaded());
    }
  }
}

SCENARIO("a read-only transport refuses to be written", "[config][transport]")
{
  GIVEN("a file over a read-only store already holding a configuration")
  {
    auto store = transport_test::transport_memory::store_t{};

    // Seeded through a writable file, so the read-only one has something to
    // read: what is being tested is the writing, not the reading.
    auto writer = arude::config::config_file<test_helpers::config_test_t, transport_test::transport_memory>{
      transport_test::transport_memory{"device", store, true}
    };
    writer.set(test_helpers::config_test_t{});
    writer.store();

    auto file = arude::config::config_file<test_helpers::config_test_t, transport_test::transport_memory>{
      transport_test::transport_memory{"device", store, false}
    };

    THEN("reading works")
    {
      file.load();
      REQUIRE(file.get().version == arude::config::version_of<test_helpers::config_test_t>());
    }

    THEN("storing is a read_only, and nothing was serialized to get there")
    {
      try
      {
        file.store();
        FAIL("storing through a read-only transport should have thrown");
      }
      catch(const arude::config::exception_t& ex)
      {
        REQUIRE(ex.data() == arude::config::config_error::read_only);
      }
    }

    // load() never writes, so create over a read-only transport is not a
    // contradiction: the defaults reach the cache and the medium is untouched.
    // It is store() that refuses, which is where the write actually is.
    THEN("the create policy fills the cache without touching the transport")
    {
      auto absent = arude::config::config_file<test_helpers::config_test_t, transport_test::transport_memory>{
        transport_test::transport_memory{"absent", store, false}
      };

      constexpr auto policy = arude::config::load_policy::create;

      absent.load(policy);

      REQUIRE(absent.loaded());
      REQUIRE(absent.get().version == arude::config::version_of<test_helpers::config_test_t>());
      REQUIRE(!store.contains("absent"));

      try
      {
        absent.store();
        FAIL("storing through a read-only transport should have thrown");
      }
      catch(const arude::config::exception_t& ex)
      {
        REQUIRE(ex.data() == arude::config::config_error::read_only);
      }

      REQUIRE(!store.contains("absent"));
    }
  }
}
