///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// What the TOML endpoint keeps of a file when it goes through a document: the
/// toml++ tree it parsed, whose nodes keep their TOML types wherever no section
/// owns them, and what it gives up, which is the comments.
///

#include "arude/config.hpp"
#include "test/helpers/temp_file.hpp"
#include "test/helpers/write_text.hpp"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <rfl/Flatten.hpp>
#include <rfl/Rename.hpp>
#include <rfl/Skip.hpp>
#include <rfl/Timestamp.hpp>
#include <rfl/toml/read.hpp>
#include <rfl/toml/write.hpp>
#include <toml++/toml.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace toml_tree_test
{
namespace
{

///
/// The root configuration of these tests.
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
/// A table a section declares, so that a key inside it is owned with it.
///
struct tls
{
  ///
  /// Certificate file.
  ///
  std::string cert = "cert.pem";
};

///
/// A named section with a nested table of its own.
///
struct net
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

  ///
  /// How the service is secured.
  ///
  tls security;
};

///
/// A section with a field of each time type reflect-cpp offers.
///
struct schedule
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
  /// A calendar day, in the pattern the type names.
  ///
  rfl::Timestamp<"%Y-%m-%d"> day = "2026-08-17";

  ///
  /// How long to wait.
  ///
  std::chrono::seconds timeout{30};
};

///
/// A section with a field that may be absent.
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
  /// Port to reach it on, where it is not the default one.
  ///
  std::optional<std::uint32_t> port;
};

///
/// One host of a map keyed by host name.
///
struct host
{
  ///
  /// Address the host is reached at.
  ///
  std::string address = "192.0.2.1";

  ///
  /// Port it listens on.
  ///
  std::int64_t port = 443;
};

///
/// A section whose keys are host names, which hold dots.
///
struct web
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
  /// The hosts, by name.
  ///
  std::map<std::string, host> hosts;
};

///
/// A section with an integer field of its own.
///
struct limits
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
  /// Most connections allowed at once.
  ///
  std::int64_t connections = 16;
};

///
/// A colour, written by name. Of the default underlying type: reflect-cpp
/// cannot list the names of an enumeration with a narrower one.
///
enum class colour // NOLINT(performance-enum-size)
{
  red,
  green
};

///
/// Fields flattened into the type that holds them.
///
struct flat_part
{
  ///
  /// A flattened integer.
  ///
  int a = 1;

  ///
  /// A flattened float.
  ///
  double b = 2.0;
};

///
/// One element of an array of tables.
///
struct item
{
  ///
  /// Identifier.
  ///
  std::string id = "x";

  ///
  /// Count.
  ///
  std::uint16_t n = 3;
};

///
/// A configuration using as much of reflect-cpp as a configuration may, for
/// comparing libarude's conversion with reflect-cpp's own.
///
struct rich
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
  /// A renamed field.
  ///
  rfl::Rename<"host-name", std::string> host = "h";

  ///
  /// Neither written nor read.
  ///
  rfl::Skip<std::string> cache;

  ///
  /// Read, never written.
  ///
  rfl::SkipSerialization<std::string> derived;

  ///
  /// Written, never read.
  ///
  rfl::SkipDeserialization<std::string> computed = "c";

  ///
  /// Fields of another type, at this one's level.
  ///
  rfl::Flatten<flat_part> flat;

  ///
  /// A payload.
  ///
  arude::config::binary secret{
    std::array{std::byte{'f'}, std::byte{'o'}, std::byte{'o'}}
  };

  ///
  /// A duration.
  ///
  std::chrono::seconds timeout{30};

  ///
  /// A timestamp.
  ///
  rfl::Timestamp<"%Y-%m-%d"> when = "2026-08-17";

  ///
  /// An optional left disengaged.
  ///
  std::optional<std::uint32_t> port;

  ///
  /// An optional engaged.
  ///
  std::optional<std::string> label = "label";

  ///
  /// An array of tables.
  ///
  std::vector<item> items{
    item{},
    item{.id = "y", .n = 4}
  };

  ///
  /// Nested arrays.
  ///
  std::vector<std::vector<int>> grid{
    {1, 2},
    {3}
  };

  ///
  /// A map.
  ///
  std::map<std::string, int> weights{
    {"a", 1},
    {"b", 2}
  };

  ///
  /// A nested table.
  ///
  flat_part nested;

  ///
  /// An enumeration.
  ///
  colour hue = colour::green;

  ///
  /// A flag.
  ///
  bool flag = true;

  ///
  /// A single-precision float.
  ///
  float ratio = 0.5F;

  ///
  /// A negative 64-bit integer.
  ///
  std::int64_t big = -5;

  ///
  /// A variant.
  ///
  std::variant<int, std::string> either = std::string{"s"};
};

///
/// A file with every TOML date and time kind in three places: beside the
/// root, at the top of a registered section's table, and in a table nobody
/// registers. Also integers beside floats, and arrays of tables both where
/// nobody owns them and under a registered section.
///
constexpr auto every_kind = std::string_view{"version = 1\n"
                                             "name = \"from the file\"\n"
                                             "odt = 1979-05-27T07:32:00.999-07:00\n"
                                             "ldt = 1979-05-27T07:32:00\n"
                                             "ld = 1979-05-27\n"
                                             "lt = 07:32:00.5\n"
                                             "\n"
                                             "[[plugins]]\n"
                                             "name = \"first\"\n"
                                             "\n"
                                             "[[plugins]]\n"
                                             "name = \"second\"\n"
                                             "\n"
                                             "[foreign]\n"
                                             "odt = 1979-05-27T07:32:00Z\n"
                                             "ldt = 1979-05-27T07:32:00\n"
                                             "ld = 1979-05-27\n"
                                             "lt = 07:32:00\n"
                                             "integer = 1\n"
                                             "float = 1.0\n"
                                             "\n"
                                             "[net]\n"
                                             "version = 1\n"
                                             "host = \"from the file\"\n"
                                             "odt = 2001-02-03T04:05:06+01:00\n"
                                             "ldt = 2001-02-03T04:05:06\n"
                                             "ld = 2001-02-03\n"
                                             "lt = 04:05:06\n"
                                             "integer = 2\n"
                                             "float = 2.0\n"
                                             "\n"
                                             "[net.security]\n"
                                             "cert = \"from the file.pem\"\n"
                                             "expires = 2026-01-01\n"
                                             "\n"
                                             "[[net.items]]\n"
                                             "id = \"undeclared\"\n"};

///
/// Returns the whole file as text.
/// \param path File to read.
/// \return The text.
///
auto text_of(const std::filesystem::path& path) -> std::string
{
  auto stream = std::ifstream{path};
  return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

///
/// Requires a table to hold one node of every date and time kind, each equal
/// to the one the same table holds in another tree.
///
/// \param stored The table as stored.
/// \param original The table as first written.
///
auto require_times_kept(
  const toml::node_view<const toml::node> stored, const toml::node_view<const toml::node> original) -> void
{
  REQUIRE(stored["odt"].is_date_time());
  REQUIRE(stored["odt"].value_or(toml::date_time{}).offset.has_value());
  REQUIRE(stored["odt"].value<toml::date_time>() == original["odt"].value<toml::date_time>());

  REQUIRE(stored["ldt"].is_date_time());
  REQUIRE(!stored["ldt"].value_or(toml::date_time{}).offset.has_value());
  REQUIRE(stored["ldt"].value<toml::date_time>() == original["ldt"].value<toml::date_time>());

  REQUIRE(stored["ld"].is_date());
  REQUIRE(stored["ld"].value<toml::date>() == original["ld"].value<toml::date>());

  REQUIRE(stored["lt"].is_time());
  REQUIRE(stored["lt"].value<toml::time>() == original["lt"].value<toml::time>());
}

///
/// Requires a call to fail with the error given.
///
/// \param action The call. Not retained.
/// \param error The error it must fail with.
///
auto require_error(const std::function<void()>& action, const arude::config::config_error error) -> void
{
  try
  {
    action();
    FAIL("the call should have thrown");
  }
  catch(const arude::config::exception_t& ex)
  {
    INFO(ex.str());
    REQUIRE(ex.data() == error);
  }
}

} // namespace

} // namespace toml_tree_test

SCENARIO("a document keeps the TOML type of everything no section owns", "[config][toml_tree]")
{
  GIVEN("a file holding every date and time kind, beside the root, in a section and in a foreign table")
  {
    const auto file = test_helpers::temp_file{"arude_toml_tree_every_kind.toml"};
    test_helpers::write_text(file.path(), toml_tree_test::every_kind);
    const auto original = toml::parse(toml_tree_test::every_kind);

    auto manager = arude::config::config_manager{};
    auto document =
      manager.register_document(arude::config::endpoint_toml{}, arude::config::transport_file{file.path()});
    auto root = document.add_root<toml_tree_test::app>();
    auto network = document.add_section<toml_tree_test::net>("net");

    WHEN("it is loaded, changed and stored")
    {
      document.load();

      REQUIRE(root.get().name == "from the file");
      REQUIRE(network.get().host == "from the file");
      REQUIRE(network.get().security.cert == "from the file.pem");

      root.set(toml_tree_test::app{.name = "stored"});
      network.set(toml_tree_test::net{.host = "stored", .security = {.cert = "stored.pem"}});
      document.store();

      const auto stored = toml::parse(toml_tree_test::text_of(file.path()));

      THEN("the sections' own fields hold the new values")
      {
        REQUIRE(stored["name"].value<std::string>() == "stored");
        REQUIRE(stored["net"]["host"].value<std::string>() == "stored");
        REQUIRE(stored["net"]["security"]["cert"].value<std::string>() == "stored.pem");
      }

      THEN("the dates and times in a table nobody registers keep their kind and value")
      {
        toml_tree_test::require_times_kept(stored["foreign"], original["foreign"]);
      }

      THEN("the dates and times beside the root keep their kind and value")
      {
        toml_tree_test::require_times_kept(
          toml::node_view<const toml::node>{stored}, toml::node_view<const toml::node>{original});
      }

      THEN("the dates and times a section's type does not declare keep their kind and value")
      {
        toml_tree_test::require_times_kept(stored["net"], original["net"]);
      }

      THEN("an integer stays an integer and a float a float, owned or not")
      {
        REQUIRE(stored["foreign"]["integer"].is_integer());
        REQUIRE(stored["foreign"]["float"].is_floating_point());
        REQUIRE(stored["net"]["integer"].is_integer());
        REQUIRE(stored["net"]["float"].is_floating_point());
      }

      THEN("an array of tables stays one, where nobody owns it and under a registered section")
      {
        REQUIRE(stored["plugins"].is_array_of_tables());
        REQUIRE(stored["plugins"].as_array()->size() == 2);
        REQUIRE(stored["net"]["items"].is_array_of_tables());
        REQUIRE(stored["net"]["items"][0]["id"].value<std::string>() == "undeclared");
      }

      // Owning goes down to the top-level key: a field the type declares is
      // written whole, so what its table held beyond the field's own keys goes.
      THEN("a key inside a table the section declares does not survive")
      {
        REQUIRE(!stored["net"]["security"]["expires"]);
      }
    }
  }
}

SCENARIO("a store before any load keeps the TOML type of what is in the file", "[config][toml_tree]")
{
  GIVEN("a file holding every date and time kind, and a document never loaded")
  {
    const auto file = test_helpers::temp_file{"arude_toml_tree_store_first.toml"};
    test_helpers::write_text(file.path(), toml_tree_test::every_kind);
    const auto original = toml::parse(toml_tree_test::every_kind);

    auto manager = arude::config::config_manager{};
    auto document =
      manager.register_document(arude::config::endpoint_toml{}, arude::config::transport_file{file.path()});
    auto root = document.add_root<toml_tree_test::app>();

    WHEN("the root is set and the document stored")
    {
      root.set(toml_tree_test::app{.name = "stored"});
      document.store();

      const auto stored = toml::parse(toml_tree_test::text_of(file.path()));

      THEN("the root is written, and every date and time keeps its kind and value")
      {
        REQUIRE(stored["name"].value<std::string>() == "stored");
        toml_tree_test::require_times_kept(stored["foreign"], original["foreign"]);
        toml_tree_test::require_times_kept(stored["net"], original["net"]);
        REQUIRE(stored["net"]["security"]["expires"].is_date());
      }
    }
  }
}

SCENARIO("reject_unknown_fields applies to every section read through a document", "[config][toml_tree]")
{
  const auto strict = arude::config::endpoint_toml{arude::config::endpoint_toml_options{.reject_unknown_fields = true}};

  GIVEN("a strict document whose section holds a key its type does not declare")
  {
    const auto file = test_helpers::temp_file{"arude_toml_tree_strict_section.toml"};
    test_helpers::write_text(
      file.path(),
      "version = 1\n"
      "name = \"app\"\n"
      "\n"
      "[net]\n"
      "version = 1\n"
      "host = \"example.invalid\"\n"
      "undeclared = 1\n"
      "\n"
      "[net.security]\n"
      "cert = \"example.pem\"\n");

    auto manager = arude::config::config_manager{};
    auto document = manager.register_document(strict, arude::config::transport_file{file.path()});
    auto root = document.add_root<toml_tree_test::app>();
    auto network = document.add_section<toml_tree_test::net>("net");

    THEN("the load is an invalid_payload, and no section is loaded")
    {
      toml_tree_test::require_error(
        [&document] -> void { document.load(); }, arude::config::config_error::invalid_payload);

      REQUIRE(!root.loaded());
      REQUIRE(!network.loaded());
    }
  }

  GIVEN("a strict document whose section holds only what its type declares")
  {
    const auto file = test_helpers::temp_file{"arude_toml_tree_strict_clean.toml"};
    test_helpers::write_text(
      file.path(),
      "version = 1\n"
      "name = \"app\"\n"
      "\n"
      "[net]\n"
      "version = 1\n"
      "host = \"example.invalid\"\n"
      "\n"
      "[net.security]\n"
      "cert = \"example.pem\"\n");

    auto manager = arude::config::config_manager{};
    auto document = manager.register_document(strict, arude::config::transport_file{file.path()});
    auto root = document.add_root<toml_tree_test::app>();
    auto network = document.add_section<toml_tree_test::net>("net");

    THEN("it loads")
    {
      document.load();

      REQUIRE(root.get().name == "app");
      REQUIRE(network.get().host == "example.invalid");
    }
  }

  // A table or an array of tables at the top level is somebody else's as far
  // as the root can tell, by the same rule that decides whether the root is
  // in the file at all, so a strict root reads past them.
  GIVEN("a strict root beside a table and an array of tables nobody registers")
  {
    const auto file = test_helpers::temp_file{"arude_toml_tree_strict_root_tables.toml"};
    test_helpers::write_text(
      file.path(),
      "version = 1\n"
      "name = \"app\"\n"
      "\n"
      "[[plugins]]\n"
      "name = \"first\"\n"
      "\n"
      "[foreign]\n"
      "when = 1979-05-27\n");

    auto manager = arude::config::config_manager{};
    auto document = manager.register_document(strict, arude::config::transport_file{file.path()});
    auto root = document.add_root<toml_tree_test::app>();

    THEN("it loads")
    {
      document.load();

      REQUIRE(root.get().name == "app");
    }
  }

  GIVEN("a strict root beside a top-level value its type does not declare")
  {
    const auto file = test_helpers::temp_file{"arude_toml_tree_strict_root_value.toml"};
    test_helpers::write_text(
      file.path(),
      "version = 1\n"
      "name = \"app\"\n"
      "stray = 1\n");

    auto manager = arude::config::config_manager{};
    auto document = manager.register_document(strict, arude::config::transport_file{file.path()});
    auto root = document.add_root<toml_tree_test::app>();

    THEN("the load is an invalid_payload")
    {
      toml_tree_test::require_error(
        [&document] -> void { document.load(); }, arude::config::config_error::invalid_payload);

      REQUIRE(!root.loaded());
    }
  }
}

// reflect-cpp maps no type onto a TOML date or time: its TOML reader and
// writer know strings, booleans, integers, floats, tables and arrays and
// nothing else. A native date survives only where no section owns it.
SCENARIO("a time-typed field is stored as reflect-cpp writes it", "[config][toml_tree]")
{
  GIVEN("a document with a section holding a timestamp and a duration")
  {
    const auto file = test_helpers::temp_file{"arude_toml_tree_times.toml"};

    auto manager = arude::config::config_manager{};
    auto document =
      manager.register_document(arude::config::endpoint_toml{}, arude::config::transport_file{file.path()});
    auto times = document.add_section<toml_tree_test::schedule>("schedule");

    WHEN("it is set and stored")
    {
      times.set(toml_tree_test::schedule{.day = "2001-02-03", .timeout = std::chrono::seconds{90}});
      document.store();

      const auto stored = toml::parse(toml_tree_test::text_of(file.path()));

      THEN("the timestamp is a string in its pattern, not a TOML date")
      {
        REQUIRE(stored["schedule"]["day"].is_string());
        REQUIRE(stored["schedule"]["day"].value<std::string>() == "2001-02-03");
      }

      THEN("the duration is a table of a count and a unit")
      {
        REQUIRE(stored["schedule"]["timeout"].is_table());
        REQUIRE(stored["schedule"]["timeout"]["count"].value<std::int64_t>() == 90);
        REQUIRE(stored["schedule"]["timeout"]["unit"].value<std::string>() == "seconds");
      }

      THEN("both read back as they were")
      {
        document.load();

        REQUIRE(times.get().day.reflection() == "2001-02-03");
        REQUIRE(times.get().timeout == std::chrono::seconds{90});
      }
    }
  }

  GIVEN("a file with a native TOML date where the section has a timestamp")
  {
    const auto file = test_helpers::temp_file{"arude_toml_tree_native_date.toml"};
    test_helpers::write_text(
      file.path(),
      "[schedule]\n"
      "version = 1\n"
      "day = 2001-02-03\n"
      "timeout = { count = 30, unit = \"seconds\" }\n");

    auto manager = arude::config::config_manager{};
    auto document =
      manager.register_document(arude::config::endpoint_toml{}, arude::config::transport_file{file.path()});
    auto times = document.add_section<toml_tree_test::schedule>("schedule");

    THEN("the load is an invalid_payload")
    {
      toml_tree_test::require_error(
        [&document] -> void { document.load(); }, arude::config::config_error::invalid_payload);

      REQUIRE(!times.loaded());
    }
  }
}

SCENARIO("a disengaged optional is left out of the file and reads back disengaged", "[config][toml_tree]")
{
  GIVEN("a document with a section whose optional field is disengaged")
  {
    const auto file = test_helpers::temp_file{"arude_toml_tree_optional.toml"};

    auto manager = arude::config::config_manager{};
    auto document =
      manager.register_document(arude::config::endpoint_toml{}, arude::config::transport_file{file.path()});
    auto section = document.add_section<toml_tree_test::proxy>("proxy");

    WHEN("it is stored and loaded again")
    {
      section.set(toml_tree_test::proxy{});
      document.store();

      const auto stored = toml::parse(toml_tree_test::text_of(file.path()));
      document.load();

      THEN("the field is not in the file, and the section has it disengaged")
      {
        REQUIRE(stored["proxy"].is_table());
        REQUIRE(!stored["proxy"]["port"]);
        REQUIRE(!section.get().port.has_value());
      }
    }
  }
}

SCENARIO("libarude's conversion of a configuration matches reflect-cpp's own", "[config][toml_tree]")
{
  GIVEN("a configuration using as much of reflect-cpp as a configuration may")
  {
    const auto value = toml_tree_test::rich{};

    THEN("to_toml renders it exactly as rfl::toml::write does")
    {
      REQUIRE(arude::config::to_toml(value) == rfl::toml::write(value));
    }

    THEN("from_toml reads back exactly what rfl::toml::read does")
    {
      const auto text = rfl::toml::write(value);
      const auto ours = arude::config::from_toml<toml_tree_test::rich>(text);
      const auto theirs = rfl::toml::read<toml_tree_test::rich>(text);

      REQUIRE(theirs.has_value());
      REQUIRE(rfl::toml::write(ours) == rfl::toml::write(theirs.value()));
    }
  }
}

// toml++ reads a dot in a path as a separator, so a key holding one is looked
// up by key and never by path here.
SCENARIO("keys holding dots survive a load and a store", "[config][toml_tree]")
{
  GIVEN("a section keyed by host names, and a foreign table whose name holds a dot")
  {
    const auto file = test_helpers::temp_file{"arude_toml_tree_dotted.toml"};
    test_helpers::write_text(
      file.path(),
      "[\"a.b\"]\n"
      "flag = true\n"
      "\n"
      "[web]\n"
      "version = 1\n"
      "\n"
      "[web.hosts.\"example.com\"]\n"
      "address = \"192.0.2.7\"\n"
      "port = 443\n");

    auto manager = arude::config::config_manager{};
    auto document =
      manager.register_document(arude::config::endpoint_toml{}, arude::config::transport_file{file.path()});
    auto site = document.add_section<toml_tree_test::web>("web");

    WHEN("it is loaded, changed and stored")
    {
      document.load();

      REQUIRE(site.get().hosts.at("example.com").address == "192.0.2.7");

      auto changed = site.get();
      changed.hosts.at("example.com").port = 8443;
      changed.hosts.insert_or_assign("www.example.com", toml_tree_test::host{});
      site.set(changed);
      document.store();

      const auto stored = toml::parse(toml_tree_test::text_of(file.path()));

      THEN("the hosts are tables under their own names")
      {
        const auto& hosts = *stored["web"]["hosts"].as_table();

        REQUIRE(hosts.get("example.com") != nullptr);
        REQUIRE(hosts.get("example.com")->as_table()->get("port")->value<std::int64_t>() == 8443);
        REQUIRE(hosts.get("www.example.com") != nullptr);
      }

      THEN("the foreign table is still there under its name")
      {
        REQUIRE(stored.get("a.b") != nullptr);
        REQUIRE(stored.get("a.b")->as_table()->get("flag")->value<bool>() == true);
      }
    }
  }
}

// toml++ does not keep comments, and nothing else here does either. The loss
// is accepted, and pinned so that a change to it is noticed.
SCENARIO("a comment in the file does not survive a store", "[config][toml_tree]")
{
  GIVEN("a file with a comment in it")
  {
    const auto file = test_helpers::temp_file{"arude_toml_tree_comment.toml"};
    test_helpers::write_text(
      file.path(),
      "# Written by hand.\n"
      "version = 1\n"
      "name = \"commented\"\n");

    auto manager = arude::config::config_manager{};
    auto document =
      manager.register_document(arude::config::endpoint_toml{}, arude::config::transport_file{file.path()});
    auto root = document.add_root<toml_tree_test::app>();

    WHEN("it is loaded and stored")
    {
      document.load();
      document.store();

      THEN("the values are kept and the comment is gone")
      {
        const auto text = toml_tree_test::text_of(file.path());

        REQUIRE(text.contains("commented"));
        REQUIRE(!text.contains("Written by hand"));
      }
    }
  }
}

// An integer's base is kept by toml++ on the node it parsed, so it survives
// where that node does, and only there.
SCENARIO("an integer keeps its base where no section owns it", "[config][toml_tree]")
{
  GIVEN("a file with integers in hexadecimal, octal and binary, owned and not")
  {
    const auto file = test_helpers::temp_file{"arude_toml_tree_integer_base.toml"};
    test_helpers::write_text(
      file.path(),
      "version = 1\n"
      "name = \"based\"\n"
      "mask = 0xFF\n"
      "[limits]\n"
      "version = 1\n"
      "connections = 0x20\n"
      "mode = 0o755\n"
      "[foreign]\n"
      "flags = 0b1010\n");

    auto manager = arude::config::config_manager{};
    auto document =
      manager.register_document(arude::config::endpoint_toml{}, arude::config::transport_file{file.path()});
    auto root = document.add_root<toml_tree_test::app>();
    auto limit = document.add_section<toml_tree_test::limits>("limits");

    WHEN("it is loaded and stored")
    {
      document.load();

      REQUIRE(limit.get().connections == 32);

      document.store();

      const auto stored = toml::parse(toml_tree_test::text_of(file.path()));

      THEN("an integer no section owns keeps its value and its base")
      {
        REQUIRE(stored["mask"].value<std::int64_t>() == 255);
        REQUIRE(stored["mask"].as_integer()->flags() == toml::value_flags::format_as_hexadecimal);
        REQUIRE(stored["limits"]["mode"].value<std::int64_t>() == 493);
        REQUIRE(stored["limits"]["mode"].as_integer()->flags() == toml::value_flags::format_as_octal);
        REQUIRE(stored["foreign"]["flags"].value<std::int64_t>() == 10);
        REQUIRE(stored["foreign"]["flags"].as_integer()->flags() == toml::value_flags::format_as_binary);
      }

      THEN("an integer field the section declares keeps its value and is written in decimal")
      {
        REQUIRE(stored["limits"]["connections"].value<std::int64_t>() == 32);
        REQUIRE(stored["limits"]["connections"].as_integer()->flags() == toml::value_flags::none);
      }
    }
  }
}
