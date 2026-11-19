///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///

#include "arude/config.hpp"
#include "arude/enum.hpp"
#include "arude/exception.hpp"
#include "test/helpers/temp_file.hpp"
#include "test/helpers/write_text.hpp"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <rfl/Generic.hpp>
#include <rfl/Skip.hpp>
#include <rfl/toml/read.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <future>
#include <iterator>
#include <latch>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

namespace document_test
{
namespace
{

///
/// The root configuration of these tests. One version only: what matters is
/// that the root has its own version, independent of every section's.
///
struct app
{
  ///
  /// The version this type is.
  ///
  static constexpr auto config_version = arude::config::version_t{1};

  ///
  /// The version carried in the file.
  ///
  arude::config::version_t version = config_version;

  ///
  /// Name this configuration is known by.
  ///
  std::string name = "example";
};

///
/// Version 1 of a section: a host, nothing else.
///
struct network_v1
{
  ///
  /// The version this type is.
  ///
  static constexpr auto config_version = arude::config::version_t{1};

  ///
  /// The version carried in the file.
  ///
  arude::config::version_t version = config_version;

  ///
  /// Host the configured service is reached at.
  ///
  std::string host = "localhost";
};

///
/// Version 2 of the same section: a port appears.
///
struct network_v2
{
  ///
  /// The version before this one, which migration walks back through.
  ///
  using previous_t = network_v1;

  ///
  /// The version this type is.
  ///
  static constexpr auto config_version = arude::config::version_t{2};

  ///
  /// The version carried in the file.
  ///
  arude::config::version_t version = config_version;

  ///
  /// \see network_v1::host
  ///
  std::string host = "localhost";

  ///
  /// Port to reach it on. New in version 2, so an upgraded section takes the
  /// default.
  ///
  std::uint32_t port = 8443;
};

///
/// Produces a version 2 section from a version 1 one.
/// \param val Section to upgrade. Not retained.
/// \return The same section as version 2.
///
[[nodiscard]] constexpr auto upgrade(const network_v1& val) -> network_v2;

///
/// Produces a version 1 section from a version 2 one.
/// \param val Section to downgrade. Not retained.
/// \return The same section as version 1, less what version 1 cannot hold.
///
[[nodiscard]] constexpr auto downgrade(const network_v2& val) -> network_v1;

///
/// A second section type, so a document can hold two distinct sections.
///
struct logging
{
  ///
  /// The version this type is.
  ///
  static constexpr auto config_version = arude::config::version_t{1};

  ///
  /// The version carried in the file.
  ///
  arude::config::version_t version = config_version;

  ///
  /// How much to log.
  ///
  std::string level = "info";
};

///
/// Version 1 of a section whose one field the next version renames.
///
struct server_v1
{
  ///
  /// The version this type is.
  ///
  static constexpr auto config_version = arude::config::version_t{1};

  ///
  /// The version carried in the file.
  ///
  arude::config::version_t version = config_version;

  ///
  /// Host the server listens on.
  ///
  std::string host = "localhost";
};

///
/// Version 2 of the same section: host is spelled hostname, so a file written
/// by version 1 holds a key that version 2 does not produce.
///
struct server_v2
{
  ///
  /// The version before this one, which migration walks back through.
  ///
  using previous_t = server_v1;

  ///
  /// The version this type is.
  ///
  static constexpr auto config_version = arude::config::version_t{2};

  ///
  /// The version carried in the file.
  ///
  arude::config::version_t version = config_version;

  ///
  /// What version 1 called host.
  ///
  std::string hostname = "localhost";
};

///
/// Produces a version 2 server section from a version 1 one.
/// \param val Section to upgrade. Not retained.
/// \return The same section as version 2, host carried over as hostname.
///
[[nodiscard]] constexpr auto upgrade(const server_v1& val) -> server_v2;

///
/// Produces a version 1 server section from a version 2 one.
/// \param val Section to downgrade. Not retained.
/// \return The same section as version 1, hostname carried over as host.
///
[[nodiscard]] constexpr auto downgrade(const server_v2& val) -> server_v1;

///
/// A section with a field that may be absent, so a value can stop producing a
/// key it was read with.
///
struct proxy
{
  ///
  /// The version this type is.
  ///
  static constexpr auto config_version = arude::config::version_t{1};

  ///
  /// The version carried in the file.
  ///
  arude::config::version_t version = config_version;

  ///
  /// Host the proxy is reached at.
  ///
  std::string host = "proxy.invalid";

  ///
  /// Port to reach it on, where it is not the default one.
  ///
  std::optional<std::uint32_t> port;
};

///
/// A root configuration with a field that may be absent: what proxy is to a
/// named section, for the top level of the document.
///
struct labelled_app
{
  ///
  /// The version this type is.
  ///
  static constexpr auto config_version = arude::config::version_t{1};

  ///
  /// The version carried in the file.
  ///
  arude::config::version_t version = config_version;

  ///
  /// Name this configuration is known by.
  ///
  std::string name = "example";

  ///
  /// Label shown beside the name, where there is one.
  ///
  std::optional<std::string> label;
};

///
/// A root configuration with members reflect-cpp skips on a write, on a read,
/// or on both, so its top-level keys are not simply its member names.
///
struct skipping_app
{
  ///
  /// The version this type is.
  ///
  static constexpr auto config_version = arude::config::version_t{1};

  ///
  /// The version carried in the file.
  ///
  arude::config::version_t version = config_version;

  ///
  /// Name this configuration is known by.
  ///
  std::string name = "example";

  ///
  /// Neither written nor read.
  ///
  rfl::Skip<std::string> cache;

  ///
  /// Read, but never written.
  ///
  rfl::SkipSerialization<std::string> derived;

  ///
  /// Written, but never read.
  ///
  rfl::SkipDeserialization<std::string> computed;
};

///
///
constexpr auto upgrade(const network_v1& val) -> network_v2
{
  return network_v2{.host = val.host};
}

///
///
constexpr auto downgrade(const network_v2& val) -> network_v1
{
  return network_v1{.host = val.host};
}

///
///
constexpr auto upgrade(const server_v1& val) -> server_v2
{
  return server_v2{.hostname = val.host};
}

///
///
constexpr auto downgrade(const server_v2& val) -> server_v1
{
  return server_v1{.host = val.hostname};
}

///
/// Returns the whole file as text, for asserting on what a store produced.
/// \param path File to read.
/// \return The text.
///
auto text_of(const std::filesystem::path& path) -> std::string
{
  auto stream = std::ifstream{path};
  return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

///
/// Returns the keys one table of a stored file holds, for asserting on a key
/// by name rather than on text another key might happen to contain.
///
/// \param path File to read. It must hold TOML, and the table asked for.
/// \param table Name of the table, or empty for the top level of the file.
/// \return The keys, in a set that looks a string literal up as it is.
///
auto keys_in(const std::filesystem::path& path, const std::string& table) -> std::set<std::string, std::less<>>
{
  const auto document = rfl::toml::read<rfl::Generic>(text_of(path)).value().to_object().value();
  const auto object = table.empty() ? document : document.get(table).value().to_object().value();

  return object | std::views::keys | std::ranges::to<std::set<std::string, std::less<>>>();
}

///
/// What a scripted transport reads and writes, shared by all its copies.
///
struct scripted_source
{
  ///
  /// The source as text, or nothing while there is no source.
  ///
  std::optional<std::string> text;

  ///
  /// A step to run inside the next read that finds a source, after the bytes
  /// that read returns were taken. Cleared before it runs, so it runs once,
  /// and a load it starts reads without it. A read that finds no source
  /// returns std::nullopt and leaves it for the next.
  ///
  std::function<void()> on_read;

  ///
  /// A step to run inside the next write, before the bytes it was handed are
  /// stored. Cleared before it runs, so it runs once, and a write that comes
  /// after it stores its bytes without it.
  ///
  std::function<void()> on_write;
};

///
/// A transport holding its source in memory, whose reads and writes can run a
/// step of the test's, so a test can act in the middle of a load or a store.
///
class transport_scripted final
{
public: // Constants
  static constexpr auto name = std::string_view{"scripted"};

public: // Structors
  ///
  /// \param source What the transport reads and writes. Kept.
  ///
  explicit transport_scripted(std::shared_ptr<scripted_source> source);

public: // Accessors
  [[nodiscard]] auto location() const -> std::string;
  [[nodiscard]] auto writable() const -> bool;
  [[nodiscard]] auto read() const -> std::optional<std::vector<std::byte>>;

public: // Methods
  auto write(std::span<const std::byte> bytes) const -> void;

private: // Variables
  std::shared_ptr<scripted_source> source_;
};

///
///
transport_scripted::transport_scripted(std::shared_ptr<scripted_source> source)
  : source_{std::move(source)}
{
}

///
///
// NOLINTNEXTLINE(readability-convert-member-functions-to-static): the contract is an instance operation.
auto transport_scripted::location() const -> std::string
{
  return std::format("{}:source", name);
}

///
///
// NOLINTNEXTLINE(readability-convert-member-functions-to-static): the contract is an instance operation.
auto transport_scripted::writable() const -> bool
{
  return true;
}

///
///
auto transport_scripted::read() const -> std::optional<std::vector<std::byte>>
{
  // Nothing to read is an answer, not a read: on_read waits for one that
  // returns bytes.
  if(!source_->text.has_value())
  {
    return std::nullopt;
  }

  auto bytes = arude::config::as_bytes(*source_->text);

  if(auto step = std::exchange(source_->on_read, nullptr))
  {
    step();
  }

  return bytes;
}

///
///
auto transport_scripted::write(const std::span<const std::byte> bytes) const -> void
{
  if(auto step = std::exchange(source_->on_write, nullptr))
  {
    step();
  }

  source_->text = arude::config::as_text(bytes);
}

///
/// Requires a call to be refused as a section whose name is taken.
///
/// \param action The call. Not retained.
/// \param mentions Texts the message must contain, such as the section's name
///                 and the location.
///
auto require_refused(const std::function<void()>& action, const std::vector<std::string>& mentions) -> void
{
  try
  {
    action();
    FAIL("the section should have been refused");
  }
  catch(const arude::config::exception_t& ex)
  {
    REQUIRE(ex.data() == arude::config::config_error::already_registered_section);

    for(const auto& mention : mentions)
    {
      INFO(std::format("The message is: {}", ex.str()));
      REQUIRE(ex.str().contains(mention));
    }
  }
}

///
/// Requires a load to be refused with the error given.
///
/// \param document The document to load.
/// \param policy The policy to load with.
/// \param error The error the load must report.
/// \param mentions Texts the message must contain.
///
auto require_refused_load(
  arude::config::config_document_handle& document,
  const arude::config::load_policy policy,
  const arude::config::config_error error,
  const std::vector<std::string>& mentions) -> void
{
  try
  {
    document.load(policy);
    FAIL("the load should have been refused");
  }
  catch(const arude::config::exception_t& ex)
  {
    INFO(std::format("The message is: {}", ex.str()));
    REQUIRE(ex.data() == error);

    for(const auto& mention : mentions)
    {
      REQUIRE(ex.str().contains(mention));
    }
  }
}

///
/// Requires a type to be registered nowhere in a manager's index.
///
/// \tparam T Configuration type.
/// \param manager Manager whose index is looked in.
///
template<arude::config::configuration_c T>
auto require_unregistered(const arude::config::config_manager& manager) -> void
{
  try
  {
    std::ignore = manager.find<T>();
    FAIL("the type should not have been registered");
  }
  catch(const arude::config::exception_t& ex)
  {
    REQUIRE(ex.data() == arude::config::config_error::not_found);
  }
}

} // namespace

} // namespace document_test

SCENARIO("several sections in one file are loaded and stored together", "[config][document]")
{
  GIVEN("a document holding a root and two named sections")
  {
    const auto file = test_helpers::temp_file{"arude_document_sections.toml"};
    auto manager = arude::config::config_manager{};
    auto document =
      manager.register_document(arude::config::endpoint_toml{}, arude::config::transport_file{file.path()});
    auto root = document.add_root<document_test::app>();
    auto network = document.add_section<document_test::network_v2>("network");
    auto logging = document.add_section<document_test::logging>("logging");

    WHEN("the sections are set and the document stored")
    {
      auto app_value = document_test::app{};
      app_value.name = "example";

      auto network_value = document_test::network_v2{};
      network_value.host = "example.invalid";
      network_value.port = 9443;

      auto logging_value = document_test::logging{};
      logging_value.level = "debug";

      root.set(app_value);
      network.set(network_value);
      logging.set(logging_value);
      document.store();

      THEN("one write put all three into the one file")
      {
        const auto text = document_test::text_of(file.path());
        REQUIRE(text.contains("name"));
        REQUIRE(text.contains("example.invalid"));
        REQUIRE(text.contains("debug"));
        REQUIRE(text.contains("[network]"));
        REQUIRE(text.contains("[logging]"));
      }

      THEN("one read brings all three back, through a fresh manager over the same file")
      {
        auto reader = arude::config::config_manager{};
        auto read_document =
          reader.register_document(arude::config::endpoint_toml{}, arude::config::transport_file{file.path()});
        auto read_root = read_document.add_root<document_test::app>();
        auto read_network = read_document.add_section<document_test::network_v2>("network");
        auto read_logging = read_document.add_section<document_test::logging>("logging");

        read_document.load();

        REQUIRE(read_root.get().name == "example");
        REQUIRE(read_network.get().host == "example.invalid");
        REQUIRE(read_network.get().port == 9443);
        REQUIRE(read_logging.get().level == "debug");
      }
    }
  }
}

// A table nobody registered is not anyone's section, but it is someone's data:
// the retained tree is what store() starts from, so it survives untouched.
SCENARIO("an unregistered table survives a store", "[config][document]")
{
  GIVEN("a file holding a table nobody registers")
  {
    const auto file = test_helpers::temp_file{"arude_document_experimental.toml"};
    test_helpers::write_text(
      file.path(),
      "version = 1\n"
      "name = \"example\"\n"
      "\n"
      "[experimental]\n"
      "flag = true\n");

    auto manager = arude::config::config_manager{};
    auto document =
      manager.register_document(arude::config::endpoint_toml{}, arude::config::transport_file{file.path()});
    auto root = document.add_root<document_test::app>();

    WHEN("the document is loaded and stored again")
    {
      document.load();

      auto updated = root.get();
      updated.name = "renamed";
      root.set(updated);
      document.store();

      THEN("the table is still there, exactly as parsed")
      {
        const auto text = document_test::text_of(file.path());
        REQUIRE(text.contains("[experimental]"));
        REQUIRE(text.contains("flag"));
        REQUIRE(text.contains("renamed"));
      }
    }
  }
}

// A section added after a load is disengaged: its subtree is left exactly as
// parsed, so it cannot clobber a value already in the file.
SCENARIO("a section added after a load does not clobber the file", "[config][document]")
{
  GIVEN("a file holding a network table, and a document that did not know it at load time")
  {
    const auto file = test_helpers::temp_file{"arude_document_late_section.toml"};
    test_helpers::write_text(
      file.path(),
      "version = 1\n"
      "name = \"example\"\n"
      "\n"
      "[network]\n"
      "version = 2\n"
      "host = \"original\"\n");

    auto manager = arude::config::config_manager{};
    auto document =
      manager.register_document(arude::config::endpoint_toml{}, arude::config::transport_file{file.path()});
    auto root = document.add_root<document_test::app>();

    document.load();

    WHEN("the network section is added afterwards and the document stored")
    {
      auto network = document.add_section<document_test::network_v2>("network");

      THEN("it is disengaged, not filled with defaults")
      {
        REQUIRE(!network.loaded());
      }

      document.store();

      THEN("the value in the file is the original, not the defaults")
      {
        const auto text = document_test::text_of(file.path());
        REQUIRE(text.contains("original"));
        REQUIRE(!text.contains("8443"));
      }

      THEN("the root it was added next to is untouched")
      {
        REQUIRE(root.get().name == "example");
      }
    }
  }
}

SCENARIO("per-section versions migrate independently", "[config][document]")
{
  GIVEN("a file whose root is current and whose network section is one version behind")
  {
    const auto file = test_helpers::temp_file{"arude_document_section_migration.toml"};
    test_helpers::write_text(
      file.path(),
      "version = 1\n"
      "name = \"example\"\n"
      "\n"
      "[network]\n"
      "version = 1\n"
      "host = \"old-host\"\n");

    auto manager = arude::config::config_manager{};
    auto document =
      manager.register_document(arude::config::endpoint_toml{}, arude::config::transport_file{file.path()});
    auto root = document.add_root<document_test::app>();
    auto network = document.add_section<document_test::network_v2>("network");

    WHEN("the document is loaded")
    {
      document.load();

      THEN("the root stays as it was")
      {
        REQUIRE(root.get().version == 1);
        REQUIRE(root.get().name == "example");
      }

      THEN("the network section was brought forward, value and all")
      {
        REQUIRE(network.get().version == 2);
        REQUIRE(network.get().host == "old-host");
        REQUIRE(network.get().port == 8443);
      }

      THEN(
        "the version decision is per section, so a load without upgrade_to_current refuses the behind section even "
        "though the root is current")
      {
        auto exact_manager = arude::config::config_manager{};
        auto exact_document =
          exact_manager.register_document(arude::config::endpoint_toml{}, arude::config::transport_file{file.path()});
        std::ignore = exact_document.add_root<document_test::app>();
        std::ignore = exact_document.add_section<document_test::network_v2>("network");

        try
        {
          exact_document.load(arude::config::load_policy::none);
          FAIL("a load of a behind section without upgrade_to_current should have thrown");
        }
        catch(const arude::config::exception_t& ex)
        {
          REQUIRE(ex.data() == arude::config::config_error::invalid_version);
        }
      }
    }
  }
}

///
///
SCENARIO("a section written by a newer build is refused and left alone", "[config][document]")
{
  using arude::config::config_error;
  using arude::config::load_policy;

  // Version 3 of the network section, which this build has no type for: the
  // newest it knows is network_v2. mtu stands for a field only version 3 has.
  constexpr auto newer_network = std::string_view{"\n"
                                                  "[network]\n"
                                                  "version = 3\n"
                                                  "host = \"new-host\"\n"
                                                  "port = 1\n"
                                                  "mtu = 1500\n"};

  GIVEN("a file whose root is current and whose network section is version 3")
  {
    const auto file = test_helpers::temp_file{"arude_document_newer_section.toml"};
    const auto written = std::format("version = 1\nname = \"example\"\n{}", newer_network);
    test_helpers::write_text(file.path(), written);

    auto manager = arude::config::config_manager{};
    auto document =
      manager.register_document(arude::config::endpoint_toml{}, arude::config::transport_file{file.path()});
    auto root = document.add_root<document_test::app>();
    auto network = document.add_section<document_test::network_v2>("network");

    WHEN("it is loaded, with upgrade_to_current or without")
    {
      for(const auto policy : {load_policy::upgrade_to_current, load_policy::none})
      {
        document_test::require_refused_load(
          document, policy, config_error::invalid_version, {"network", "version 3", "newer"});
      }

      THEN("nothing was installed, not even the current root")
      {
        REQUIRE(!root.loaded());
        REQUIRE(!network.loaded());
      }
    }

    WHEN("the network section is set and stored with no load before it")
    {
      network.set(document_test::network_v2{.host = "old-build"});

      try
      {
        document.store();
        FAIL("a store over a newer section should have thrown");
      }
      catch(const arude::config::exception_t& ex)
      {
        REQUIRE(ex.data() == config_error::invalid_version);
        REQUIRE(std::string{ex.str()}.contains("newer"));
      }

      THEN("the source is exactly as the newer build wrote it")
      {
        REQUIRE(document_test::text_of(file.path()) == written);
      }
    }

    WHEN("only the root is set and stored with no load before it")
    {
      root.set(document_test::app{.name = "renamed"});
      document.store();

      THEN("the root is written, and the newer section it does not own is kept as it was")
      {
        const auto text = document_test::text_of(file.path());
        REQUIRE(text.contains("renamed"));

        const auto network_keys = document_test::keys_in(file.path(), "network");
        REQUIRE(network_keys.contains("mtu"));

        auto reread = arude::config::config_manager{};
        auto reread_document =
          reread.register_document(arude::config::endpoint_toml{}, arude::config::transport_file{file.path()});
        std::ignore = reread_document.add_section<document_test::network_v2>("network");
        document_test::require_refused_load(
          reread_document, load_policy::upgrade_to_current, config_error::invalid_version, {"newer"});
      }
    }
  }

  GIVEN("a document loaded with only its root, whose source holds the network section in version 3")
  {
    const auto file = test_helpers::temp_file{"arude_document_newer_attached_late.toml"};
    const auto written = std::format("version = 1\nname = \"example\"\n{}", newer_network);
    test_helpers::write_text(file.path(), written);

    auto manager = arude::config::config_manager{};
    auto document =
      manager.register_document(arude::config::endpoint_toml{}, arude::config::transport_file{file.path()});
    std::ignore = document.add_root<document_test::app>();
    document.load();

    WHEN("the network section is added after the load, set and stored")
    {
      auto network = document.add_section<document_test::network_v2>("network");
      network.set(document_test::network_v2{.host = "old-build"});

      try
      {
        document.store();
        FAIL("a store over a newer section should have thrown, though the section came after the load");
      }
      catch(const arude::config::exception_t& ex)
      {
        INFO(std::format("The message is: {}", ex.str()));
        REQUIRE(ex.data() == config_error::invalid_version);
        REQUIRE(ex.str().contains("newer"));
      }

      THEN("the source is exactly as the newer build wrote it")
      {
        REQUIRE(document_test::text_of(file.path()) == written);
      }
    }
  }

  GIVEN("a document loaded from version 2, whose source a newer build then rewrites as version 3")
  {
    const auto file = test_helpers::temp_file{"arude_document_newer_rewrite.toml"};
    test_helpers::write_text(file.path(), "[network]\nversion = 2\nhost = \"current-host\"\nport = 80\n");

    auto manager = arude::config::config_manager{};
    auto document =
      manager.register_document(arude::config::endpoint_toml{}, arude::config::transport_file{file.path()});
    auto network = document.add_section<document_test::network_v2>("network");
    document.load();

    test_helpers::write_text(file.path(), newer_network);

    WHEN("it is loaded again")
    {
      document_test::require_refused_load(
        document, load_policy::upgrade_to_current, config_error::invalid_version, {"newer"});

      THEN("the values of the first load are still the ones held")
      {
        REQUIRE(network.get().version == 2);
        REQUIRE(network.get().host == "current-host");
        REQUIRE(network.get().port == 80);
      }
    }
  }
}

// Regressions for three defects found by review, each of which a passing suite
// had missed because no test exercised the paths where two rules meet.

SCENARIO("the create policy leaves the source alone", "[config][document]")
{
  GIVEN("a document over a path with no file at it")
  {
    const auto file = test_helpers::temp_file{"arude_document_create.toml"};
    auto manager = arude::config::config_manager{};
    auto document =
      manager.register_document(arude::config::endpoint_toml{}, arude::config::transport_file{file.path()});
    auto root = document.add_root<document_test::app>();

    WHEN("it is loaded with the create policy")
    {
      document.load(arude::config::load_policy::create);

      // A load never writes. A store from inside it would skip every
      // section, none being engaged yet, and leave an empty file, which the
      // next start-up would find present and fail to parse until somebody
      // deleted it by hand.
      THEN("nothing was written")
      {
        REQUIRE(!std::filesystem::exists(file.path()));
      }

      THEN("the section is readable, at its defaults")
      {
        REQUIRE(root.loaded());
        REQUIRE(root.get().version == arude::config::version_of<document_test::app>());
      }

      AND_WHEN("it is stored and loaded again by a second manager")
      {
        document.store();

        auto next_manager = arude::config::config_manager{};
        auto next_document =
          next_manager.register_document(arude::config::endpoint_toml{}, arude::config::transport_file{file.path()});
        auto next_root = next_document.add_root<document_test::app>();

        THEN("the second start-up reads what the first wrote")
        {
          next_document.load(arude::config::load_policy::create);
          REQUIRE(next_root.loaded());
          REQUIRE(next_root.get().version == arude::config::version_of<document_test::app>());
        }
      }
    }
  }
}

// Under the create policy a file with no section in it loads as defaults, so
// a load that caught a file part-way through being rewritten would replace the
// application's values with defaults and report nothing, and the next store
// would write them out. transport_file replaces the file in one step, so no
// load sees it empty or half-written.
SCENARIO("a load overlapping a store of the same file reads one whole file", "[config][document]")
{
  GIVEN("a file stored by one document, and a second document over the same file")
  {
    const auto file = test_helpers::temp_file{"arude_document_overlap.toml"};

    auto writer_manager = arude::config::config_manager{};
    auto writer =
      writer_manager.register_document(arude::config::endpoint_toml{}, arude::config::transport_file{file.path()});
    auto writer_root = writer.add_root<document_test::app>();
    writer_root.set(document_test::app{.name = "stored"});
    writer.store();

    auto reader_manager = arude::config::config_manager{};
    auto reader =
      reader_manager.register_document(arude::config::endpoint_toml{}, arude::config::transport_file{file.path()});
    auto reader_root = reader.add_root<document_test::app>();

    WHEN("one thread stores it over and over while another loads it with the create policy")
    {
      static constexpr auto rounds = 300;

      using arude::bitwise_operators::operator|;

      auto loads_at_defaults = 0;
      auto loads_failed = 0;
      auto stores_refused = 0; // Written by the storing thread alone, and read once it has joined.

      {
        // A store that fails is tolerated: on Windows, replacing a file that
        // a reader has open at that moment can be refused. What this test is
        // about is what the reader finds.
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

            if(reader_root.get().name != "stored")
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
          std::filesystem::directory_iterator{file.path().parent_path()} |
          std::views::filter(
            [&file](const std::filesystem::directory_entry& entry) -> bool
            { return entry.path().filename().string().contains(file.path().filename().string()); }));
        REQUIRE(siblings == 1);
      }
    }
  }
}

SCENARIO("reading a section that was never loaded is a not_found", "[config][document]")
{
  GIVEN("a registered section and no load")
  {
    const auto file = test_helpers::temp_file{"arude_document_unloaded.toml"};
    auto manager = arude::config::config_manager{};
    auto document =
      manager.register_document(arude::config::endpoint_toml{}, arude::config::transport_file{file.path()});
    auto root = document.add_root<document_test::app>();

    THEN("get() throws the subsystem's own error, not std::bad_optional_access")
    {
      REQUIRE(!root.loaded());

      try
      {
        std::ignore = root.get();
        FAIL("get() on a never-loaded section should have thrown");
      }
      catch(const arude::config::exception_t& ex)
      {
        REQUIRE(ex.data() == arude::config::config_error::not_found);
      }
    }

    // std::bad_optional_access derives from std::exception, not from
    // arude::exception_base, so it would walk straight past the catch every
    // other failure here is caught by.
    THEN("it is catchable as an arude::exception_base")
    {
      REQUIRE_THROWS_AS(std::ignore = root.get(), arude::exception_base);
    }
  }
}

// A load keeps what it read to itself and installs it in one step, so it has
// nowhere to leave a half-finished result for the next load to trip over.
SCENARIO("a second load does not disengage what the first loaded", "[config][document]")
{
  GIVEN("a document loaded once")
  {
    const auto file = test_helpers::temp_file{"arude_document_reload.toml"};
    test_helpers::write_text(file.path(), "version = 1\nname = \"kept\"\n");

    auto manager = arude::config::config_manager{};
    auto document =
      manager.register_document(arude::config::endpoint_toml{}, arude::config::transport_file{file.path()});
    auto root = document.add_root<document_test::app>();

    document.load();

    WHEN("it is loaded again from the same source")
    {
      document.load();

      THEN("the value is still loaded")
      {
        REQUIRE(root.loaded());
        REQUIRE(root.get().name == "kept");
      }

      THEN("a store still writes the section")
      {
        std::filesystem::remove(file.path());
        document.store();

        REQUIRE(document_test::text_of(file.path()).contains("kept"));
      }
    }

    WHEN("the source changes and it is loaded again")
    {
      test_helpers::write_text(file.path(), "version = 1\nname = \"changed\"\n");
      document.load();

      THEN("the second load's value is the one cached")
      {
        REQUIRE(root.loaded());
        REQUIRE(root.get().name == "changed");
      }
    }
  }
}

// An application release that adds a section meets files written before it.
// Whether that is an error is the caller's to say, through load_policy::create;
// either way it is a section that is not there, and not a malformed one.
SCENARIO("a section missing from an existing source is created or reported, as the policy says", "[config][document]")
{
  GIVEN("a file holding a root, a logging table and a table nobody registers, and no network table")
  {
    const auto file = test_helpers::temp_file{"arude_document_missing_section.toml"};
    test_helpers::write_text(
      file.path(),
      "version = 1\n"
      "name = \"from the file\"\n"
      "\n"
      "[logging]\n"
      "version = 1\n"
      "level = \"debug\"\n"
      "\n"
      "[experimental]\n"
      "flag = true\n");

    auto manager = arude::config::config_manager{};
    auto document =
      manager.register_document(arude::config::endpoint_toml{}, arude::config::transport_file{file.path()});
    auto root = document.add_root<document_test::app>();
    auto logging = document.add_section<document_test::logging>("logging");

    WHEN("a document that also registers the network section is loaded without the create policy")
    {
      auto network = document.add_section<document_test::network_v2>("network");

      THEN("the load is a not_found that names the section")
      {
        try
        {
          document.load();
          FAIL("loading a source that lacks a registered section should have thrown");
        }
        catch(const arude::config::exception_t& ex)
        {
          REQUIRE(ex.data() == arude::config::config_error::not_found);
          REQUIRE(ex.str().contains("'network'"));
          REQUIRE(!ex.str().contains("declares no configuration version"));
        }
      }

      THEN("no section was loaded, the ones that are in the file included")
      {
        REQUIRE_THROWS_AS(document.load(), arude::config::exception_t);

        REQUIRE(!root.loaded());
        REQUIRE(!logging.loaded());
        REQUIRE(!network.loaded());
      }
    }

    WHEN("the sections in the file are loaded, and a load then fails for a section added since")
    {
      document.load();

      test_helpers::write_text(
        file.path(),
        "version = 1\n"
        "name = \"rewritten\"\n"
        "\n"
        "[logging]\n"
        "version = 1\n"
        "level = \"trace\"\n");

      auto network = document.add_section<document_test::network_v2>("network");

      REQUIRE_THROWS_AS(document.load(), arude::config::exception_t);

      THEN("the loaded sections keep the values of the load that succeeded")
      {
        REQUIRE(root.get().name == "from the file");
        REQUIRE(logging.get().level == "debug");
      }

      THEN("the section that was never loaded stays unloaded")
      {
        REQUIRE(!network.loaded());
      }

      // The rewritten source dropped the experimental table, so it is in the
      // file after a store only where the tree the failed load read was not
      // retained.
      THEN("a store starts from the tree of the load that succeeded")
      {
        document.store();

        const auto text = document_test::text_of(file.path());
        REQUIRE(text.contains("[experimental]"));
        REQUIRE(text.contains("from the file"));
        REQUIRE(!text.contains("rewritten"));
        REQUIRE(!text.contains("trace"));
      }
    }

    WHEN("it is loaded with the create policy")
    {
      auto network = document.add_section<document_test::network_v2>("network");
      const auto before = document_test::text_of(file.path());

      // The flags combine, so creating what is missing does not give up
      // migrating what is there.
      using arude::bitwise_operators::operator|;

      document.load(arude::config::load_policy::create | arude::config::load_policy::upgrade_to_current);

      THEN("the missing section holds its defaults")
      {
        REQUIRE(network.loaded());
        REQUIRE(network.get().host == "localhost");
        REQUIRE(network.get().port == 8443);
      }

      THEN("the sections that are in the file hold the file's values")
      {
        REQUIRE(root.get().name == "from the file");
        REQUIRE(logging.get().level == "debug");
      }

      THEN("the file is as it was, since a load never writes")
      {
        REQUIRE(document_test::text_of(file.path()) == before);
      }

      AND_WHEN("the document is stored")
      {
        document.store();

        THEN("the section is added to the file")
        {
          const auto keys = document_test::keys_in(file.path(), "network");
          REQUIRE(keys.contains("version"));
          REQUIRE(keys.contains("host"));
          REQUIRE(keys.contains("port"));
        }

        THEN("everything else in the file is kept")
        {
          const auto text = document_test::text_of(file.path());
          REQUIRE(text.contains("from the file"));
          REQUIRE(text.contains("debug"));
          REQUIRE(text.contains("[experimental]"));
          REQUIRE(text.contains("flag"));
        }

        THEN("the next start-up loads it without the create policy")
        {
          auto next_manager = arude::config::config_manager{};
          auto next_document =
            next_manager.register_document(arude::config::endpoint_toml{}, arude::config::transport_file{file.path()});
          auto next_root = next_document.add_root<document_test::app>();
          auto next_logging = next_document.add_section<document_test::logging>("logging");
          auto next_network = next_document.add_section<document_test::network_v2>("network");

          next_document.load();

          REQUIRE(next_root.get().name == "from the file");
          REQUIRE(next_logging.get().level == "debug");
          REQUIRE(next_network.get().port == 8443);
        }
      }
    }
  }
}

// The root has no key of its own to be missing, so what stands for it is a
// value at the top level that is not a table. Tables alone, arrays of tables
// among them, are other owners'.
SCENARIO("a root section missing from an existing source is created or reported", "[config][document]")
{
  GIVEN("a file holding only a registered section's table and a table nobody registers")
  {
    const auto file = test_helpers::temp_file{"arude_document_missing_root.toml"};
    test_helpers::write_text(
      file.path(),
      "[network]\n"
      "version = 2\n"
      "host = \"from the file\"\n"
      "port = 9443\n"
      "\n"
      "[experimental]\n"
      "flag = true\n");

    auto manager = arude::config::config_manager{};
    auto document =
      manager.register_document(arude::config::endpoint_toml{}, arude::config::transport_file{file.path()});
    auto root = document.add_root<document_test::app>();
    auto network = document.add_section<document_test::network_v2>("network");

    WHEN("it is loaded without the create policy")
    {
      THEN("the load is a not_found that names the root section")
      {
        try
        {
          document.load();
          FAIL("loading a source that lacks the root section should have thrown");
        }
        catch(const arude::config::exception_t& ex)
        {
          REQUIRE(ex.data() == arude::config::config_error::not_found);
          REQUIRE(ex.str().contains("the root section"));
        }

        REQUIRE(!network.loaded());
      }
    }

    WHEN("it is loaded with the create policy")
    {
      document.load(arude::config::load_policy::create);

      THEN("the root takes its defaults")
      {
        REQUIRE(root.loaded());
        REQUIRE(root.get().name == "example");
        REQUIRE(root.get().version == arude::config::version_of<document_test::app>());
      }

      THEN("the section that is in the file holds the file's values")
      {
        REQUIRE(network.get().host == "from the file");
        REQUIRE(network.get().port == 9443);
      }

      AND_WHEN("the document is stored")
      {
        document.store();

        THEN("the root is written beside what was there")
        {
          const auto keys = document_test::keys_in(file.path(), "");
          REQUIRE(keys.contains("version"));
          REQUIRE(keys.contains("name"));
          REQUIRE(keys.contains("network"));
          REQUIRE(keys.contains("experimental"));
        }
      }
    }
  }

  GIVEN("a file holding a registered section's table and an array of tables nobody registers")
  {
    const auto file = test_helpers::temp_file{"arude_document_missing_root_array.toml"};
    test_helpers::write_text(
      file.path(),
      "[[plugins]]\n"
      "name = \"first\"\n"
      "\n"
      "[[plugins]]\n"
      "name = \"second\"\n"
      "\n"
      "[network]\n"
      "version = 2\n"
      "host = \"from the file\"\n"
      "port = 9443\n");

    auto manager = arude::config::config_manager{};
    auto document =
      manager.register_document(arude::config::endpoint_toml{}, arude::config::transport_file{file.path()});
    auto root = document.add_root<document_test::app>();
    auto network = document.add_section<document_test::network_v2>("network");

    WHEN("it is loaded without the create policy")
    {
      THEN("the load is a not_found that names the root section, not a malformed root")
      {
        try
        {
          document.load();
          FAIL("loading a source that lacks the root section should have thrown");
        }
        catch(const arude::config::exception_t& ex)
        {
          REQUIRE(ex.data() == arude::config::config_error::not_found);
          REQUIRE(ex.str().contains("the root section"));
        }
      }
    }

    WHEN("it is loaded with the create policy and stored")
    {
      document.load(arude::config::load_policy::create);
      document.store();

      THEN("the root takes its defaults, and the section the file's values")
      {
        REQUIRE(root.get().name == "example");
        REQUIRE(network.get().host == "from the file");
      }

      THEN("the root is written beside the array of tables, which is kept")
      {
        const auto keys = document_test::keys_in(file.path(), "");
        REQUIRE(keys.contains("version"));
        REQUIRE(keys.contains("name"));
        REQUIRE(keys.contains("plugins"));

        const auto text = document_test::text_of(file.path());
        REQUIRE(text.contains("first"));
        REQUIRE(text.contains("second"));
      }
    }
  }

  GIVEN("a file with a value at the top level that is not a table, and no version")
  {
    const auto file = test_helpers::temp_file{"arude_document_versionless_root.toml"};
    test_helpers::write_text(file.path(), "name = \"nameless\"\n");

    auto manager = arude::config::config_manager{};
    auto document =
      manager.register_document(arude::config::endpoint_toml{}, arude::config::transport_file{file.path()});
    auto root = document.add_root<document_test::app>();

    THEN("the root is there and malformed, which the create policy does not paper over")
    {
      try
      {
        document.load(arude::config::load_policy::create);
        FAIL("loading a root without a version should have thrown");
      }
      catch(const arude::config::exception_t& ex)
      {
        REQUIRE(ex.data() == arude::config::config_error::invalid_payload);
      }

      REQUIRE(!root.loaded());
    }
  }
}

// A section owns the keys it was read from, and a store replaces exactly those.
// So a key the value has stopped producing leaves the file with it.
SCENARIO("a key a section no longer produces leaves the file", "[config][document]")
{
  GIVEN("a named section loaded with its optional field set")
  {
    const auto file = test_helpers::temp_file{"arude_document_optional_reset.toml"};
    test_helpers::write_text(
      file.path(),
      "version = 1\n"
      "name = \"example\"\n"
      "\n"
      "[proxy]\n"
      "version = 1\n"
      "host = \"proxy.invalid\"\n"
      "port = 3128\n");

    auto manager = arude::config::config_manager{};
    auto document =
      manager.register_document(arude::config::endpoint_toml{}, arude::config::transport_file{file.path()});
    auto root = document.add_root<document_test::app>();
    auto proxy = document.add_section<document_test::proxy>("proxy");

    document.load();
    REQUIRE(proxy.get().port == 3128);

    WHEN("the field is reset and the document stored")
    {
      auto updated = proxy.get();
      updated.port.reset();
      proxy.set(updated);
      document.store();

      THEN("the key is gone from the section's table, and the others are not")
      {
        const auto keys = document_test::keys_in(file.path(), "proxy");
        REQUIRE(!keys.contains("port"));
        REQUIRE(keys.contains("host"));
        REQUIRE(keys.contains("version"));
        REQUIRE(!document_test::text_of(file.path()).contains("3128"));
      }

      THEN("the field stays disengaged across a load")
      {
        document.load();
        REQUIRE(!proxy.get().port.has_value());
        REQUIRE(proxy.get().host == "proxy.invalid");
      }

      THEN("a second store does not bring the key back")
      {
        document.store();
        REQUIRE(!document_test::keys_in(file.path(), "proxy").contains("port"));
      }
    }
  }

  GIVEN("a root section loaded with its optional field set, beside a table nobody registers")
  {
    const auto file = test_helpers::temp_file{"arude_document_optional_reset_root.toml"};
    test_helpers::write_text(
      file.path(),
      "version = 1\n"
      "name = \"example\"\n"
      "label = \"temporary\"\n"
      "\n"
      "[experimental]\n"
      "flag = true\n");

    auto manager = arude::config::config_manager{};
    auto document =
      manager.register_document(arude::config::endpoint_toml{}, arude::config::transport_file{file.path()});
    auto root = document.add_root<document_test::labelled_app>();

    document.load();
    REQUIRE(root.get().label == "temporary");

    WHEN("the field is reset and the document stored")
    {
      auto updated = root.get();
      updated.label.reset();
      root.set(updated);
      document.store();

      THEN("the key is gone from the top level, and what the root does not own is not")
      {
        const auto keys = document_test::keys_in(file.path(), "");
        REQUIRE(!keys.contains("label"));
        REQUIRE(keys.contains("name"));
        REQUIRE(keys.contains("version"));
        REQUIRE(keys.contains("experimental"));
      }

      THEN("the field stays disengaged across a load")
      {
        document.load();
        REQUIRE(!root.get().label.has_value());
      }
    }
  }

  GIVEN("a version 1 section whose field the current version renames")
  {
    const auto file = test_helpers::temp_file{"arude_document_renamed_key.toml"};
    test_helpers::write_text(
      file.path(),
      "version = 1\n"
      "name = \"example\"\n"
      "\n"
      "[server]\n"
      "version = 1\n"
      "host = \"old-host\"\n");

    auto manager = arude::config::config_manager{};
    auto document =
      manager.register_document(arude::config::endpoint_toml{}, arude::config::transport_file{file.path()});
    auto root = document.add_root<document_test::app>();
    auto server = document.add_section<document_test::server_v2>("server");

    WHEN("it is loaded as the current version and stored")
    {
      document.load();
      REQUIRE(server.get().hostname == "old-host");

      document.store();

      THEN("the table holds the new key and not the old one")
      {
        const auto keys = document_test::keys_in(file.path(), "server");
        REQUIRE(keys.contains("hostname"));
        REQUIRE(!keys.contains("host"));
        REQUIRE(keys.contains("version"));
      }

      // A load without upgrade_to_current alone passes whether or not the old
      // key is still beside the new one, as a document ignores keys its types
      // do not declare. What it cannot pass with is host surviving the store
      // that follows it.
      THEN("it reads back as the current version, and a store from that has no old key either")
      {
        document.load(arude::config::load_policy::none);
        REQUIRE(server.get().hostname == "old-host");

        document.store();
        REQUIRE(!document_test::keys_in(file.path(), "server").contains("host"));
      }
    }
  }
}

// The other half of owning a key: one the section's type does not declare was
// never read by it, so it is somebody else's and a store leaves it be.
SCENARIO("a key a section's type does not declare survives a store", "[config][document]")
{
  GIVEN("a file whose network table holds a key the type knows nothing of")
  {
    const auto file = test_helpers::temp_file{"arude_document_undeclared_key.toml"};
    test_helpers::write_text(
      file.path(),
      "version = 1\n"
      "name = \"example\"\n"
      "\n"
      "[network]\n"
      "version = 2\n"
      "host = \"original\"\n"
      "port = 9443\n"
      "comment = \"kept\"\n");

    auto manager = arude::config::config_manager{};
    auto document =
      manager.register_document(arude::config::endpoint_toml{}, arude::config::transport_file{file.path()});
    auto root = document.add_root<document_test::app>();
    auto network = document.add_section<document_test::network_v2>("network");

    WHEN("the section is loaded, changed and stored")
    {
      document.load();

      auto updated = network.get();
      updated.host = "renamed";
      network.set(updated);
      document.store();

      THEN("the key is still in the table, beside the value that changed")
      {
        const auto keys = document_test::keys_in(file.path(), "network");
        REQUIRE(keys.contains("comment"));

        const auto text = document_test::text_of(file.path());
        REQUIRE(text.contains("kept"));
        REQUIRE(text.contains("renamed"));
        REQUIRE(!text.contains("original"));
      }
    }
  }
}

// A document that was never loaded has not looked at its source, and what it
// has not looked at is not its to wipe: a store reads the source first.
SCENARIO("a store with no load before it keeps what is in the source", "[config][document]")
{
  GIVEN("a file with a root, a logging table and a table nobody registers, and a document never loaded")
  {
    const auto file = test_helpers::temp_file{"arude_document_store_unloaded.toml"};
    test_helpers::write_text(
      file.path(),
      "version = 1\n"
      "name = \"from the file\"\n"
      "comment = \"kept\"\n"
      "\n"
      "[logging]\n"
      "version = 1\n"
      "level = \"debug\"\n"
      "\n"
      "[experimental]\n"
      "flag = true\n");

    auto manager = arude::config::config_manager{};
    auto document =
      manager.register_document(arude::config::endpoint_toml{}, arude::config::transport_file{file.path()});
    auto root = document.add_root<document_test::app>();
    auto network = document.add_section<document_test::network_v2>("network");
    auto logging = document.add_section<document_test::logging>("logging");

    WHEN("one section is set and the document stored")
    {
      auto network_value = document_test::network_v2{};
      network_value.host = "example.invalid";

      network.set(network_value);
      document.store();

      THEN("the set section is written")
      {
        const auto text = document_test::text_of(file.path());
        REQUIRE(text.contains("[network]"));
        REQUIRE(text.contains("example.invalid"));
      }

      THEN("the source's other keys and tables are kept")
      {
        const auto text = document_test::text_of(file.path());
        REQUIRE(text.contains("from the file"));
        REQUIRE(text.contains("comment"));
        REQUIRE(text.contains("[logging]"));
        REQUIRE(text.contains("debug"));
        REQUIRE(text.contains("[experimental]"));
        REQUIRE(text.contains("flag"));
      }

      THEN("reading the source for that engaged no section")
      {
        REQUIRE(!root.loaded());
        REQUIRE(!logging.loaded());
      }
    }

    WHEN("the root is set and the document stored")
    {
      root.set(document_test::app{.name = "renamed"});
      document.store();

      THEN("the root's fields are merged over the top level, which loses no key it did not write")
      {
        const auto keys = document_test::keys_in(file.path(), "");
        REQUIRE(keys.contains("comment"));
        REQUIRE(keys.contains("logging"));
        REQUIRE(keys.contains("experimental"));

        const auto text = document_test::text_of(file.path());
        REQUIRE(text.contains("renamed"));
        REQUIRE(!text.contains("from the file"));
      }
    }

    // A store reads the source, but that is not a load: the next store has
    // still not looked at what was added to the source since, and reads again.
    WHEN("a section is stored, a table is added to the source, and the document is stored again")
    {
      network.set(document_test::network_v2{});
      document.store();

      test_helpers::write_text(file.path(), document_test::text_of(file.path()) + "\n[added]\nflag = true\n");
      document.store();

      THEN("the table added in between is kept, beside what was there first")
      {
        const auto keys = document_test::keys_in(file.path(), "");
        REQUIRE(keys.contains("added"));
        REQUIRE(keys.contains("experimental"));
        REQUIRE(keys.contains("network"));
      }
    }

    WHEN("a section is stored, and the source then stops parsing")
    {
      static constexpr auto garbage = std::string_view{"{ this is not toml"};

      network.set(document_test::network_v2{});
      document.store();

      test_helpers::write_text(file.path(), garbage);

      THEN("the next store is an invalid_payload as well, and leaves the source as it is")
      {
        try
        {
          document.store();
          FAIL("storing over a source that does not parse should have thrown");
        }
        catch(const arude::config::exception_t& ex)
        {
          REQUIRE(ex.data() == arude::config::config_error::invalid_payload);
        }

        REQUIRE(document_test::text_of(file.path()) == garbage);
      }
    }
  }

  GIVEN("a file that is not valid TOML, and a document never loaded")
  {
    static constexpr auto garbage = std::string_view{"{ this is not toml"};

    const auto file = test_helpers::temp_file{"arude_document_store_unparseable.toml"};
    test_helpers::write_text(file.path(), garbage);

    auto manager = arude::config::config_manager{};
    auto document =
      manager.register_document(arude::config::endpoint_toml{}, arude::config::transport_file{file.path()});
    auto root = document.add_root<document_test::app>();

    WHEN("a section is set and the document stored")
    {
      root.set(document_test::app{.name = "would overwrite"});

      THEN("the store is an invalid_payload rather than an overwrite")
      {
        try
        {
          document.store();
          FAIL("storing over a source that does not parse should have thrown");
        }
        catch(const arude::config::exception_t& ex)
        {
          REQUIRE(ex.data() == arude::config::config_error::invalid_payload);
        }

        REQUIRE(document_test::text_of(file.path()) == garbage);
      }
    }

    // A load cannot get past the source either, on any policy, so this is the
    // one way to replace it through the library.
    WHEN("the source is discarded, the root set and the document stored")
    {
      auto logging = document.add_section<document_test::logging>("logging");

      document.discard_source();
      root.set(document_test::app{.name = "replacement"});
      document.store();

      THEN("the source holds the root and nothing else, the section never set included")
      {
        const auto keys = document_test::keys_in(file.path(), "");
        REQUIRE(keys == std::set<std::string, std::less<>>{"name", "version"});
        REQUIRE(!logging.loaded());
      }

      THEN("the next start-up loads it")
      {
        auto next_manager = arude::config::config_manager{};
        auto next_document =
          next_manager.register_document(arude::config::endpoint_toml{}, arude::config::transport_file{file.path()});
        auto next_root = next_document.add_root<document_test::app>();

        next_document.load();

        REQUIRE(next_root.get().name == "replacement");
      }
    }
  }
}

// What a store starts from once the document has been loaded is the tree that
// load read, so it writes over the source exactly what the sections own in it,
// and a store does not go back to the source on its own.
SCENARIO("a store after a load starts from the source as that load read it", "[config][document]")
{
  GIVEN("a document loaded from a file holding a root and a table nobody registers")
  {
    const auto file = test_helpers::temp_file{"arude_document_store_loaded.toml"};
    test_helpers::write_text(
      file.path(),
      "version = 1\n"
      "name = \"from the file\"\n"
      "\n"
      "[experimental]\n"
      "flag = true\n");

    auto manager = arude::config::config_manager{};
    auto document =
      manager.register_document(arude::config::endpoint_toml{}, arude::config::transport_file{file.path()});
    auto root = document.add_root<document_test::app>();

    document.load();

    WHEN("a table is added to the source after the load, and the document is stored")
    {
      test_helpers::write_text(file.path(), document_test::text_of(file.path()) + "\n[added]\nflag = true\n");
      document.store();

      THEN("what the load read is written back")
      {
        const auto keys = document_test::keys_in(file.path(), "");
        REQUIRE(keys.contains("experimental"));
        REQUIRE(keys.contains("name"));
      }

      THEN("the table added since is not, as the document never read it")
      {
        REQUIRE(!document_test::keys_in(file.path(), "").contains("added"));
      }
    }
  }
}

// The sections a load works through are the ones attached when it started.
// One attached while the source is being read is not part of that load: it
// stays disengaged, rather than failing the load for not being in a source
// that was written before it existed.
SCENARIO("a section attached while a load is under way is not part of that load", "[config][document]")
{
  GIVEN("a document whose transport attaches sections in the middle of its read")
  {
    auto source = std::make_shared<document_test::scripted_source>();
    source->text = "version = 1\n"
                   "name = \"from the source\"\n";

    auto manager = arude::config::config_manager{};
    auto document =
      manager.register_document(arude::config::endpoint_toml{}, document_test::transport_scripted{source});
    auto root = document.add_root<document_test::app>();

    // Enough of them that the list they are attached to has to grow, so the
    // load cannot be working through that list itself.
    static constexpr auto attached_during_read = std::size_t{64};
    auto late = std::vector<arude::config::config_handle<document_test::network_v2>>{};

    source->on_read = [&document, &late] -> void
    {
      for(auto i = std::size_t{0}; i < attached_during_read; ++i)
      {
        late.push_back(document.add_section<document_test::network_v2>(std::format("late_{}", i)));
      }
    };

    WHEN("it is loaded without the create policy")
    {
      document.load();

      THEN("the load succeeds, and fills the section attached before it")
      {
        REQUIRE(root.loaded());
        REQUIRE(root.get().name == "from the source");
      }

      THEN("every section attached during it stays disengaged")
      {
        REQUIRE(late.size() == attached_during_read);
        REQUIRE(std::ranges::none_of(late, [](const auto& section) -> bool { return section.loaded(); }));
      }
    }
  }
}

// Of two loads under way at once, the one that started later wins, so the
// tree a store starts from and the keys every section owns in it come from
// one read. Run on one thread: the transport starts the second load from
// inside the first one's read, after taking the bytes that read returns.
SCENARIO("of two loads under way at once, the later one's read is the one that stands", "[config][document]")
{
  GIVEN("a source whose renamed section is still at version 1, read by a load that a second load overtakes")
  {
    auto source = std::make_shared<document_test::scripted_source>();
    source->text = "version = 1\n"
                   "name = \"first read\"\n"
                   "\n"
                   "[server]\n"
                   "version = 1\n"
                   "host = \"old-host\"\n";

    auto manager = arude::config::config_manager{};
    auto document =
      manager.register_document(arude::config::endpoint_toml{}, document_test::transport_scripted{source});
    auto root = document.add_root<document_test::app>();
    auto attached = std::vector<arude::config::config_handle<document_test::server_v2>>{};

    // Between the two loads a section is attached, so the first load does not
    // know it and the second one does, and the source moves on to version 2.
    source->on_read = [&source, &document, &attached] -> void
    {
      attached.push_back(document.add_section<document_test::server_v2>("server"));
      source->text = "version = 1\n"
                     "name = \"second read\"\n"
                     "\n"
                     "[server]\n"
                     "version = 2\n"
                     "hostname = \"new-host\"\n";

      document.load();
    };

    WHEN("the first load finishes after the second")
    {
      document.load();

      THEN("every section holds the second read")
      {
        REQUIRE(root.get().name == "second read");
        REQUIRE(attached.size() == 1);
        REQUIRE(attached.at(0).get().hostname == "new-host");
      }

      THEN("a store writes the second read, not one read's table under the other's keys")
      {
        document.store();

        const auto stored = rfl::toml::read<rfl::Generic>(source->text.value()).value().to_object().value();
        const auto table = stored.get("server").value().to_object().value();

        REQUIRE(stored.get("name").value().to_string().value() == "second read");
        REQUIRE(table.get("hostname").has_value());
        REQUIRE(!table.get("host").has_value());
      }
    }
  }
}

// Two stores under way at once are written in the order their snapshots were
// taken, or the older snapshot could land last and lose the newer values
// without a word. The transport holds the first store inside its write, with
// its snapshot taken, until the test lets it go.
SCENARIO("of two stores under way at once, the newer snapshot is written last", "[config][document]")
{
  GIVEN("a loaded document whose transport holds its next write until the test lets it go")
  {
    auto source = std::make_shared<document_test::scripted_source>();
    source->text = "version = 1\n"
                   "name = \"from the source\"\n";

    auto manager = arude::config::config_manager{};
    auto document =
      manager.register_document(arude::config::endpoint_toml{}, document_test::transport_scripted{source});
    auto root = document.add_root<document_test::app>();

    document.load();

    // Declared in this order so that a held write is let go on every way out
    // of the scope, a failed REQUIRE included: release is destroyed before the
    // futures, which wait for their threads, and a promise destroyed unset
    // still wakes whoever waits on it.
    auto held = std::promise<void>{}; // Set by the write once it is being held.
    auto first = std::future<void>{};
    auto second = std::future<void>{};
    auto release = std::promise<void>{};

    source->on_write = [&held, released = release.get_future().share()] -> void
    {
      held.set_value();
      released.wait();
    };

    WHEN("one store is held in its write, and a second starts once a newer value is set")
    {
      root.set(document_test::app{.name = "older"});
      first = std::async(std::launch::async, [&document] -> void { document.store(); });
      held.get_future().wait();

      // The held store holds no lock on the cache, so this does not wait.
      root.set(document_test::app{.name = "newer"});
      second = std::async(std::launch::async, [&document] -> void { document.store(); });

      // Bounded, as a second store that waits for the first would otherwise
      // never return here. Where stores are ordered the result does not depend
      // on the bound: the second cannot finish before the first is let go,
      // however long it is given.
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
        const auto stored = rfl::toml::read<rfl::Generic>(source->text.value()).value().to_object().value();
        REQUIRE(stored.get("name").value().to_string().value() == "newer");
      }
    }

    WHEN("a store is held in its write, and the document is loaded meanwhile")
    {
      root.set(document_test::app{.name = "stored"});
      first = std::async(std::launch::async, [&document] -> void { document.store(); });
      held.get_future().wait();

      second = std::async(std::launch::async, [&document] -> void { document.load(); });

      // Generous, as the bound is how long a failure takes to show and not
      // what a pass depends on: a load that does not wait for the store is
      // done long before it runs out.
      const auto loaded = second.wait_for(std::chrono::seconds{10}) == std::future_status::ready;

      release.set_value();
      first.get();
      second.get();

      THEN("the load finished while the store was still held")
      {
        REQUIRE(loaded);
      }

      THEN("it installed the source as it was before the held write, as one whole read")
      {
        REQUIRE(root.get().name == "from the source");
      }
    }
  }
}

SCENARIO("a store that fails below the transport is an io_error", "[config][document]")
{
  GIVEN("a file transport whose parent path is a regular file")
  {
    const auto blocker = test_helpers::temp_file{"arude_document_blocked_parent"};
    test_helpers::write_text(blocker.path(), "in the way");

    auto manager = arude::config::config_manager{};
    auto document = manager.register_document(
      arude::config::endpoint_toml{}, arude::config::transport_file{blocker.path() / "app.toml"});
    auto root = document.add_root<document_test::app>();

    WHEN("a section is set and the document stored")
    {
      root.set(document_test::app{});

      // The directories leading to the file cannot be created, which the
      // filesystem library reports as a std::filesystem::filesystem_error.
      THEN("the failure is the subsystem's own error, naming the location")
      {
        try
        {
          document.store();
          FAIL("storing below a regular file should have thrown");
        }
        catch(const arude::config::exception_t& ex)
        {
          REQUIRE(ex.data() == arude::config::config_error::io_error);
          REQUIRE(ex.str().contains(document.location()));
        }
      }

      THEN("it is catchable as an arude::exception_base")
      {
        REQUIRE_THROWS_AS(document.store(), arude::exception_base);
      }
    }
  }
}

// A section's name is its key at the top level of the document, so two
// sections under one name would both write the same table, and a second root
// would write the same keys as the first.
SCENARIO("a section under a name the document holds already is refused", "[config][document]")
{
  GIVEN("a document holding a root and a named section")
  {
    auto source = std::make_shared<document_test::scripted_source>();
    auto manager = arude::config::config_manager{};
    auto document =
      manager.register_document(arude::config::endpoint_toml{}, document_test::transport_scripted{source});
    auto root = document.add_root<document_test::app>();
    auto network = document.add_section<document_test::network_v2>("network");

    WHEN("a second section is added under the same name")
    {
      THEN("it is refused, naming the section and the location, whatever its type")
      {
        document_test::require_refused(
          [&document] -> void { std::ignore = document.add_section<document_test::logging>("network"); },
          {"'network'", document.location()});

        document_test::require_refused(
          [&document] -> void { std::ignore = document.add_section<document_test::network_v2>("network"); },
          {"'network'", document.location()});
      }

      // Registered twice, network_v2 would be an ambiguous_type here.
      THEN("the refused section is not registered, and the first one is found")
      {
        document_test::require_refused(
          [&document] -> void { std::ignore = document.add_section<document_test::logging>("network"); }, {});
        document_test::require_refused(
          [&document] -> void { std::ignore = document.add_section<document_test::network_v2>("network"); }, {});

        document_test::require_unregistered<document_test::logging>(manager);

        network.set(document_test::network_v2{.host = "the first section"});
        REQUIRE(manager.find<document_test::network_v2>().get().host == "the first section");
      }
    }

    WHEN("a second root is added")
    {
      THEN("it is refused, naming the root and the location, whatever its type")
      {
        document_test::require_refused(
          [&document] -> void { std::ignore = document.add_root<document_test::labelled_app>(); },
          {"the root section", document.location()});

        document_test::require_refused(
          [&document] -> void { std::ignore = document.add_section<document_test::labelled_app>(""); },
          {"the root section", document.location()});
      }

      // Registered twice, the root's type would be an ambiguous_type here.
      THEN("its type is registered once, for the first root, and found")
      {
        document_test::require_refused(
          [&document] -> void { std::ignore = document.add_root<document_test::app>(); }, {document.location()});
        document_test::require_refused(
          [&document] -> void { std::ignore = document.add_root<document_test::labelled_app>(); },
          {document.location()});
        document_test::require_refused(
          [&document] -> void { std::ignore = document.add_section<document_test::labelled_app>(""); },
          {document.location()});

        root.set(document_test::app{.name = "the first root"});
        REQUIRE(manager.find<document_test::app>().get().name == "the first root");
        document_test::require_unregistered<document_test::labelled_app>(manager);
      }
    }
  }
}

// The root's keys are the document's top level, where every named section's
// name is a key too. A section named like a field of the root would be read
// by the root as that field, and stripped from it as a subsection on a load.
SCENARIO("a section named like a top-level key of the root is refused", "[config][document]")
{
  GIVEN("a document with a root whose type has a field that may be absent")
  {
    auto source = std::make_shared<document_test::scripted_source>();
    auto manager = arude::config::config_manager{};
    auto document =
      manager.register_document(arude::config::endpoint_toml{}, document_test::transport_scripted{source});
    std::ignore = document.add_root<document_test::labelled_app>();

    WHEN("a section is added under the name of one of the root's fields")
    {
      THEN("it is refused, naming the section, the key, the root and the location")
      {
        document_test::require_refused(
          [&document] -> void { std::ignore = document.add_section<document_test::logging>("name"); },
          {"section 'name'", "the root section", document.location()});

        document_test::require_unregistered<document_test::logging>(manager);
      }
    }

    WHEN("a section is added under the root's version key")
    {
      THEN("it is refused")
      {
        document_test::require_refused(
          [&document] -> void { std::ignore = document.add_section<document_test::logging>("version"); },
          {"section 'version'", document.location()});
      }
    }

    // The field is disengaged in the default value, so the root has not
    // written it yet. It writes it once it is set, so it is the root's all
    // the same.
    WHEN("a section is added under the name of a field the root leaves out while it is disengaged")
    {
      THEN("it is refused")
      {
        document_test::require_refused(
          [&document] -> void { std::ignore = document.add_section<document_test::logging>("label"); },
          {"section 'label'", document.location()});
      }
    }
  }

  GIVEN("a source holding a section named like a field of the root, and a document holding that section alone")
  {
    auto source = std::make_shared<document_test::scripted_source>();
    source->text = "[name]\n"
                   "version = 1\n"
                   "level = \"debug\"\n";

    auto manager = arude::config::config_manager{};
    auto document =
      manager.register_document(arude::config::endpoint_toml{}, document_test::transport_scripted{source});
    auto name = document.add_section<document_test::logging>("name");

    WHEN("the root is added after it")
    {
      THEN("the root is refused, naming the root, the key, the section and the location")
      {
        document_test::require_refused(
          [&document] -> void { std::ignore = document.add_root<document_test::app>(); },
          {"the root section", "'name'", "section 'name'", document.location()});

        document_test::require_unregistered<document_test::app>(manager);
      }

      // Attached, the root would make the load a not_found: the source has
      // no root, and the load does not create one.
      THEN("the document loads as one without a root")
      {
        document_test::require_refused(
          [&document] -> void { std::ignore = document.add_root<document_test::app>(); }, {});

        document.load();
        REQUIRE(name.get().level == "debug");
      }
    }
  }

  // Only what the root writes is its key. A member reflect-cpp skips on a
  // write never reaches the document, so a section is free to take its name.
  GIVEN("a document with a root whose type has members reflect-cpp skips")
  {
    auto source = std::make_shared<document_test::scripted_source>();
    auto manager = arude::config::config_manager{};
    auto document =
      manager.register_document(arude::config::endpoint_toml{}, document_test::transport_scripted{source});
    auto root = document.add_root<document_test::skipping_app>();

    WHEN("sections are added under the names of the members that are never written")
    {
      auto cache = document.add_section<document_test::logging>("cache");
      auto derived = document.add_section<document_test::network_v2>("derived");

      THEN("they are accepted, and stored and loaded beside the root")
      {
        root.set(document_test::skipping_app{.name = "stored"});
        cache.set(document_test::logging{.level = "debug"});
        derived.set(document_test::network_v2{.host = "derived.invalid"});
        document.store();

        document.load();
        REQUIRE(root.get().name == "stored");
        REQUIRE(cache.get().level == "debug");
        REQUIRE(derived.get().host == "derived.invalid");
      }
    }

    WHEN("a section is added under the name of the member that is written, although never read")
    {
      THEN("it is refused")
      {
        document_test::require_refused(
          [&document] -> void { std::ignore = document.add_section<document_test::logging>("computed"); },
          {"section 'computed'", "the root section", document.location()});

        document_test::require_unregistered<document_test::logging>(manager);
      }
    }
  }
}

SCENARIO("a refused section leaves the document as it was", "[config][document]")
{
  GIVEN("two documents over the same text, one of them asked for three sections it refuses")
  {
    static constexpr auto text = std::string_view{"version = 1\n"
                                                  "name = \"from the source\"\n"
                                                  "\n"
                                                  "[network]\n"
                                                  "version = 2\n"
                                                  "host = \"original\"\n"
                                                  "port = 9443\n"};

    auto refused_source = std::make_shared<document_test::scripted_source>();
    refused_source->text = std::string{text};
    auto refused_manager = arude::config::config_manager{};
    auto refused_document = refused_manager.register_document(
      arude::config::endpoint_toml{}, document_test::transport_scripted{refused_source});
    auto refused_root = refused_document.add_root<document_test::app>();
    auto refused_network = refused_document.add_section<document_test::network_v2>("network");

    document_test::require_refused(
      [&refused_document] -> void { std::ignore = refused_document.add_section<document_test::logging>("network"); },
      {});
    document_test::require_refused(
      [&refused_document] -> void { std::ignore = refused_document.add_section<document_test::logging>("name"); }, {});
    document_test::require_refused(
      [&refused_document] -> void { std::ignore = refused_document.add_root<document_test::labelled_app>(); }, {});

    auto plain_source = std::make_shared<document_test::scripted_source>();
    plain_source->text = std::string{text};
    auto plain_manager = arude::config::config_manager{};
    auto plain_document =
      plain_manager.register_document(arude::config::endpoint_toml{}, document_test::transport_scripted{plain_source});
    auto plain_root = plain_document.add_root<document_test::app>();
    auto plain_network = plain_document.add_section<document_test::network_v2>("network");

    WHEN("both are loaded")
    {
      refused_document.load();
      plain_document.load();

      THEN("the root keeps the field a refused section was named like")
      {
        REQUIRE(refused_root.get().name == "from the source");
        REQUIRE(refused_network.get().host == "original");
      }

      THEN("a store of each, after the same change, writes the same text")
      {
        refused_root.set(document_test::app{.name = "renamed"});
        plain_root.set(document_test::app{.name = "renamed"});

        auto network_value = refused_network.get();
        network_value.host = "changed";
        refused_network.set(network_value);
        plain_network.set(network_value);

        refused_document.store();
        plain_document.store();

        REQUIRE(refused_source->text == plain_source->text);
        REQUIRE(!refused_source->text.value().contains("level"));
      }

      THEN("the refused types are registered nowhere, and the accepted ones once")
      {
        document_test::require_unregistered<document_test::logging>(refused_manager);
        document_test::require_unregistered<document_test::labelled_app>(refused_manager);
        REQUIRE(refused_manager.find<document_test::app>().get().name == "from the source");
        REQUIRE(refused_manager.find<document_test::network_v2>().get().host == "original");
      }
    }
  }
}

SCENARIO("sections under distinct names are still accepted", "[config][document]")
{
  GIVEN("a document holding a root and a named section, loaded")
  {
    auto source = std::make_shared<document_test::scripted_source>();
    source->text = "version = 1\n"
                   "name = \"from the source\"\n"
                   "\n"
                   "[network]\n"
                   "version = 2\n"
                   "host = \"original\"\n"
                   "port = 9443\n"
                   "\n"
                   "[logging]\n"
                   "version = 1\n"
                   "level = \"debug\"\n";

    auto manager = arude::config::config_manager{};
    auto document =
      manager.register_document(arude::config::endpoint_toml{}, document_test::transport_scripted{source});
    auto root = document.add_root<document_test::app>();
    std::ignore = document.add_section<document_test::network_v2>("network");

    document.load();

    WHEN("a section under a name of its own is added after the load")
    {
      auto logging = document.add_section<document_test::logging>("logging");

      THEN("it is accepted, and the next load fills it")
      {
        REQUIRE(!logging.loaded());

        document.load();
        REQUIRE(logging.get().level == "debug");
      }

      THEN("a name the root holds is refused after a load as well")
      {
        document_test::require_refused(
          [&document] -> void { std::ignore = document.add_section<document_test::proxy>("name"); },
          {"section 'name'"});
      }
    }

    // Only the top level is shared: a named section's fields are keys of its
    // own table, where no other section writes.
    WHEN("a section is named like a field of a named section")
    {
      auto host = document.add_section<document_test::logging>("host");

      THEN("it is accepted")
      {
        REQUIRE(!host.loaded());
      }
    }
  }

  GIVEN("two documents")
  {
    const auto file = test_helpers::temp_file{"arude_document_distinct.toml"};
    auto manager = arude::config::config_manager{};
    auto first = manager.register_document(
      arude::config::endpoint_toml{},
      document_test::transport_scripted{std::make_shared<document_test::scripted_source>()});
    auto second = manager.register_document(arude::config::endpoint_toml{}, arude::config::transport_file{file.path()});

    WHEN("each is given a root and a section under the same name")
    {
      std::ignore = first.add_root<document_test::app>();
      std::ignore = first.add_section<document_test::logging>("logging");

      THEN("the other document accepts them, as a name is taken within one document only")
      {
        std::ignore = second.add_root<document_test::labelled_app>();
        std::ignore = second.add_section<document_test::network_v2>("logging");
      }
    }
  }
}

// The name is checked and the section attached in one step, so a name that is
// free when two threads ask for it is given to one of them.
SCENARIO("of sections added under one name at once, one is accepted", "[config][document]")
{
  GIVEN("a document, and threads that all add a section under the same name")
  {
    static constexpr auto threads = std::size_t{8};

    auto manager = arude::config::config_manager{};
    auto document = manager.register_document(
      arude::config::endpoint_toml{},
      document_test::transport_scripted{std::make_shared<document_test::scripted_source>()});

    auto start = std::latch{static_cast<std::ptrdiff_t>(threads)};
    auto accepted = std::atomic<std::size_t>{0};
    auto refused = std::atomic<std::size_t>{0};

    WHEN("they run")
    {
      {
        auto workers = std::vector<std::jthread>{};
        workers.reserve(threads);

        for(auto i = std::size_t{0}; i < threads; ++i)
        {
          workers.emplace_back(
            [&document, &start, &accepted, &refused] -> void
            {
              start.arrive_and_wait();

              try
              {
                std::ignore = document.add_section<document_test::logging>("shared");
                ++accepted;
              }
              catch(const arude::config::exception_t& ex)
              {
                if(ex.data() == arude::config::config_error::already_registered_section)
                {
                  ++refused;
                }
              }
            });
        }
      }

      THEN("one is accepted, every other one refused, and the type is registered once")
      {
        REQUIRE(accepted.load() == 1);
        REQUIRE(refused.load() == threads - 1);
        REQUIRE(!manager.find<document_test::logging>().loaded());
      }
    }
  }
}
