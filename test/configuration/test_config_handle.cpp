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

#include <catch2/catch_test_macros.hpp>

#include <condition_variable>
#include <cstddef>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace config_handle_test
{

///
/// The gate a slow transport's read waits on.
/// Shared with the test, and owned through a shared_ptr so the transport stays
/// movable — a mutex member would delete the move constructor the transport
/// contract requires.
///
struct gate final
{
  ///
  /// Protects released.
  ///
  std::mutex mutex;

  ///
  /// Signalled when the test lets the read finish.
  ///
  std::condition_variable ready;

  ///
  /// Whether the read may finish.
  ///
  bool released = false;
};

///
/// A transport whose read blocks until the test releases it.
/// The test is about locking, not timing: a blocked read is how it is
/// observed that one document's slow load holds nothing another document's
/// get() needs.
///
class slow_transport final
{
public: // Typedefs / Constants
  using store_t = std::map<std::string, std::vector<std::byte>>;

  static constexpr auto name = std::string_view{"slow"};

public: // Structors
  ///
  /// \param slot Which slot this transport reads and writes.
  /// \param store Where the bytes live. Held by reference and must outlive this.
  /// \param gate The gate read() blocks on, shared with the test.
  ///
  slow_transport(std::string_view slot, store_t& store, std::shared_ptr<gate> gate);

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
  std::shared_ptr<gate> gate_;
};

///
/// Releases a gate and joins the loading thread when the scope ends, so a
/// failed assertion cannot leave a blocked thread behind to hang the binary.
///
class scoped_release final
{
public: // Structors / Operators
  ///
  /// \param gate The gate to release.
  /// \param loading The thread to join.
  ///
  scoped_release(std::shared_ptr<gate> gate, std::thread& loading);

  ///
  /// Releases and joins.
  ///
  ~scoped_release();

  scoped_release(const scoped_release&) = delete;
  scoped_release(scoped_release&&) = delete;
  auto operator=(const scoped_release&) -> scoped_release& = delete;
  auto operator=(scoped_release&&) -> scoped_release& = delete;

public: // Methods
  ///
  /// Releases the gate and joins the thread now, once.
  ///
  auto fire() -> void;

private: // Variables
  std::shared_ptr<gate> gate_;
  arude::non_owning_t<std::thread> loading_;
  bool fired_ = false;
};

///
///
scoped_release::scoped_release(std::shared_ptr<gate> gate, std::thread& loading)
  : gate_{std::move(gate)}
  , loading_{&loading}
{
}

///
///
scoped_release::~scoped_release()
{
  fire();
}

///
///
auto scoped_release::fire() -> void
{
  if(fired_)
  {
    return;
  }

  fired_ = true;

  {
    const auto lock = std::lock_guard{gate_->mutex};
    gate_->released = true;
  }

  gate_->ready.notify_all();
  loading_->join();
}

///
///
slow_transport::slow_transport(const std::string_view slot, store_t& store, std::shared_ptr<gate> gate)
  : slot_{slot}
  , store_{&store}
  , gate_{std::move(gate)}
{
}

///
///
auto slow_transport::location() const -> std::string
{
  return std::format("{}:{}", name, slot_);
}

///
///
auto slow_transport::writable() const -> bool
{
  return true;
}

///
///
auto slow_transport::exists() const -> bool
{
  return store_->contains(slot_);
}

///
///
auto slow_transport::read() const -> std::vector<std::byte>
{
  auto lock = std::unique_lock{gate_->mutex};
  gate_->ready.wait(lock, [this] { return gate_->released; });

  return store_->at(slot_);
}

///
///
auto slow_transport::write(const std::span<const std::byte> bytes) const -> void
{
  store_->insert_or_assign(slot_, std::vector<std::byte>{bytes.begin(), bytes.end()});
}

///
/// The configuration the test loads, one version only.
///
struct app
{
  static constexpr auto config_version = arude::config::version_t{1};
  arude::config::version_t version = config_version;
  std::string name = "example";
};

///
/// Seeds a slot with a minimal document, as bytes.
/// \param name The name the document will load as.
/// \return The bytes.
///
[[nodiscard]] auto seed(std::string_view name) -> std::vector<std::byte>;

///
///
auto seed(const std::string_view name) -> std::vector<std::byte>
{
  const auto text = std::format("version = 1\nname = \"{}\"\n", name);
  const auto* const begin = reinterpret_cast<const std::byte*>(text.data());

  return {begin, begin + text.size()};
}

} // namespace config_handle_test

static_assert(arude::config::transport_c<config_handle_test::slow_transport>);

SCENARIO("a slow load on one document does not block a get on another", "[config][config_handle]")
{
  GIVEN("two documents, one of them slow, and a load of the slow one in flight")
  {
    auto store = config_handle_test::slow_transport::store_t{};
    store["slow"] = config_handle_test::seed("loaded");
    store["fast"] = config_handle_test::seed("fast");

    auto gate = std::make_shared<config_handle_test::gate>();

    auto manager = arude::config::config_manager{};
    auto slow_document = manager.register_document(
      arude::config::endpoint_toml{}, config_handle_test::slow_transport{"slow", store, gate});
    auto slow_root = slow_document.add_root<config_handle_test::app>();

    auto fast_document = manager.register_document(
      arude::config::endpoint_toml{}, config_handle_test::slow_transport{"fast", store, gate});
    auto fast_root = fast_document.add_root<config_handle_test::app>();
    fast_root.set(config_handle_test::app{.name = "fast"});

    auto loading = std::thread{[&slow_document] { slow_document.load(); }};

    WHEN("get() is called on the other document while the slow load is blocked")
    {
      const auto release = config_handle_test::scoped_release{gate, loading};

      THEN("it returns without waiting for the slow load")
      {
        // A value before the release is the whole proof: had the slow load
        // held anything get() needs, this call would still be blocked.
        REQUIRE(fast_root.get().name == "fast");
      }
    }

    WHEN("the slow load is released")
    {
      THEN("it finishes with the document's value")
      {
        auto release = config_handle_test::scoped_release{gate, loading};
        release.fire();
        REQUIRE(slow_root.get().name == "loaded");
      }
    }
  }
}

SCENARIO("a get on a document is serialized against a load of its own", "[config][config_handle]")
{
  GIVEN("a document with a cached value and a slow load of it in flight")
  {
    auto store = config_handle_test::slow_transport::store_t{};
    store["slow"] = config_handle_test::seed("loaded");

    auto gate = std::make_shared<config_handle_test::gate>();

    auto manager = arude::config::config_manager{};
    auto document = manager.register_document(
      arude::config::endpoint_toml{}, config_handle_test::slow_transport{"slow", store, gate});
    auto root = document.add_root<config_handle_test::app>();
    root.set(config_handle_test::app{.name = "before"});

    auto loading = std::thread{[&document] { document.load(); }};

    WHEN("get() is called while the load has not finished parsing")
    {
      const auto release = config_handle_test::scoped_release{gate, loading};

      THEN("it sees the cached value, not a half-loaded one")
      {
        // The slow read runs outside the document's lock, so this returns
        // promptly with the value from before the load started.
        REQUIRE(root.get().name == "before");
      }
    }

    WHEN("the load is released")
    {
      THEN("once it has committed, the new value is what get() sees")
      {
        auto release = config_handle_test::scoped_release{gate, loading};
        release.fire();
        REQUIRE(root.get().name == "loaded");
      }
    }
  }
}
