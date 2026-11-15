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
#include "test/helpers/temp_file.hpp"
#include "test/helpers/write_text.hpp"

#include <catch2/catch_test_macros.hpp>
#include <rfl/Generic.hpp>
#include <rfl/toml/read.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
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
/// What a scripted transport reads and writes, shared by all its copies.
///
struct scripted_source
{
  ///
  /// The source as text, or nothing while there is no source.
  ///
  std::optional<std::string> text;

  ///
  /// A step to run inside the next read, after the bytes that read returns
  /// were taken. Cleared before it runs, so it runs once, and a load it
  /// starts reads without it.
  ///
  std::function<void()> on_read;
};

///
/// A transport holding its source in memory, whose reads can run a step of
/// the test's, so a test can act in the middle of a load without a thread.
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
  [[nodiscard]] auto exists() const -> bool;
  [[nodiscard]] auto read() const -> std::vector<std::byte>;

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
auto transport_scripted::exists() const -> bool
{
  return source_->text.has_value();
}

///
///
auto transport_scripted::read() const -> std::vector<std::byte>
{
  if(!source_->text.has_value())
  {
    throw arude::config::exception_t{"scripted: there is no source to read.", arude::config::config_error::io_error};
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
  source_->text = arude::config::as_text(bytes);
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
        "the version decision is per section, so strict_version refuses the behind section even though the root is "
        "current")
      {
        auto strict_manager = arude::config::config_manager{};
        auto strict_document =
          strict_manager.register_document(arude::config::endpoint_toml{}, arude::config::transport_file{file.path()});
        std::ignore = strict_document.add_root<document_test::app>();
        std::ignore = strict_document.add_section<document_test::network_v2>("network");

        try
        {
          strict_document.load(arude::config::load_policy::strict_version);
          FAIL("a strict load of a behind section should have thrown");
        }
        catch(const arude::config::exception_t& ex)
        {
          REQUIRE(ex.data() == arude::config::config_error::invalid_version);
        }
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

      // The defect: load() used to call store(), which skips disengaged
      // sections, so it wrote an empty file. The next start-up then found that
      // file present, failed to parse it, and kept failing until somebody
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

// A load that caught a file part-way through being rewritten would find it
// empty or cut short and fail, although the file was whole both before the
// write and after it. transport_file replaces the file in one step, so no load
// sees it empty or half-written.
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
    // arude::exception_base, so it used to walk straight past the catch every
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
// tree a store starts from and the value every section holds come from one
// read. Run on one thread: the transport starts the second load from inside
// the first one's read, after taking the bytes that read returns.
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
