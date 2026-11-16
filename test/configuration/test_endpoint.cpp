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
#include "test/helpers/temp_file.hpp"
#include "test/helpers/write_text.hpp"

#include <catch2/catch_test_macros.hpp>
#include <toml++/toml.hpp>

#include <cstddef>
#include <format>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace endpoint_test
{

///
/// A transport holding one slot's bytes in memory, so the endpoint's options
/// are the only thing being tested.
///
class transport_memory final
{
public: // Typedefs / Constants
  using store_t = std::map<std::string, std::vector<std::byte>>;

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
  [[nodiscard]] auto read() const -> std::optional<std::vector<std::byte>>;

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
/// A configuration the endpoint parses, with no version chain of its own.
///
struct marker_config
{
  static constexpr auto config_version = arude::config::version_t{1};
  arude::config::version_t version = config_version;
  std::string name = "example";
};

///
/// How marker_endpoint behaves. The option is the whole point: two instances
/// of the same endpoint type parse differently, which is what proves the
/// instance — and its options — is what a document reads with.
///
struct marker_options
{
  ///
  /// Whether parse() stamps the parsed document with a marker field.
  ///
  bool stamp = false;
};

///
/// A test endpoint whose option visibly changes what it parses.
/// Wraps the TOML endpoint, and stamps the tree it parsed when told to; the
/// reader and writer are the TOML endpoint's own, named as a wrapper names them.
///
class marker_endpoint final
{
public: // Typedefs / Constants
  static constexpr auto name = std::string_view{"marker"};
  using document_t = arude::config::endpoint_toml::document_t;

public: // Structors
  ///
  /// Constructs an endpoint with the options given.
  /// \param options How it should behave. Copied.
  ///
  explicit marker_endpoint(marker_options options);

public: // Accessors
  ///
  /// Reads and parses the document, stamping it when the option says so.
  ///
  /// \tparam Tr Transport type.
  /// \param carrier Where the bytes come from.
  /// \return The parsed document, or std::nullopt where there is none.
  ///
  template<arude::config::transport_c Tr>
  [[nodiscard]] auto parse(const Tr& carrier) const -> std::optional<document_t>;

  ///
  /// \see arude::config::endpoint_toml::make_reader
  ///
  [[nodiscard]] auto make_reader(const document_t& document) const -> arude::config::toml_tree_reader;

  ///
  /// \see arude::config::endpoint_toml::make_reader
  ///
  auto make_reader(document_t&& document) const -> arude::config::toml_tree_reader = delete;

public: // Methods
  ///
  /// Writes the document through the transport.
  ///
  /// \tparam Tr Transport type.
  /// \param carrier Where the bytes go.
  /// \param document The document to write.
  ///
  template<arude::config::transport_c Tr>
  auto emit(const Tr& carrier, const document_t& document) const -> void;

  ///
  /// \see arude::config::endpoint_toml::make_writer
  ///
  [[nodiscard]] auto make_writer(document_t& document) const -> arude::config::toml_tree_writer;

private: // Variables
  arude::config::endpoint_toml format_;
  marker_options options_;
};

///
/// A test endpoint that only works with a transport that has a path, as an
/// endpoint integrated with its medium might. Otherwise the TOML endpoint.
///
class path_endpoint final
{
public: // Typedefs / Constants
  static constexpr auto name = std::string_view{"path"};
  using document_t = arude::config::endpoint_toml::document_t;

public: // Accessors
  ///
  /// Reads and parses the file at the transport's path.
  ///
  /// \tparam Tr Transport type, which has to have a path.
  /// \param carrier Where the bytes come from.
  /// \return The parsed document, or std::nullopt where there is none.
  ///
  template<arude::config::path_transport_c Tr>
  [[nodiscard]] auto parse(const Tr& carrier) const -> std::optional<document_t>;

  ///
  /// \see arude::config::endpoint_toml::make_reader
  ///
  [[nodiscard]] auto make_reader(const document_t& document) const -> arude::config::toml_tree_reader;

  ///
  /// \see arude::config::endpoint_toml::make_reader
  ///
  auto make_reader(document_t&& document) const -> arude::config::toml_tree_reader = delete;

public: // Methods
  ///
  /// Writes the document to the transport's path.
  ///
  /// \tparam Tr Transport type, which has to have a path.
  /// \param carrier Where the bytes go.
  /// \param document The document to write.
  ///
  template<arude::config::path_transport_c Tr>
  auto emit(const Tr& carrier, const document_t& document) const -> void;

  ///
  /// \see arude::config::endpoint_toml::make_writer
  ///
  [[nodiscard]] auto make_writer(document_t& document) const -> arude::config::toml_tree_writer;

private: // Variables
  arude::config::endpoint_toml format_;
};

///
/// Whether a manager takes an endpoint and a transport as a pair, asked of the
/// call itself rather than of the concept it is constrained with.
///
/// \tparam E Endpoint type.
/// \tparam Tr Transport type.
///
template<typename E, typename Tr>
concept registrable_c = requires(arude::config::config_manager& manager, E format, Tr carrier) {
  manager.register_document(std::move(format), std::move(carrier));
};

///
///
marker_endpoint::marker_endpoint(marker_options options)
  : options_{options}
{
}

///
///
template<arude::config::transport_c Tr>
auto marker_endpoint::parse(const Tr& carrier) const -> std::optional<document_t>
{
  auto document = format_.parse(carrier);

  if(document.has_value() && options_.stamp)
  {
    document->insert_or_assign("stamped", true);
  }

  return document;
}

///
///
auto marker_endpoint::make_reader(const document_t& document) const -> arude::config::toml_tree_reader
{
  return format_.make_reader(document);
}

///
///
template<arude::config::transport_c Tr>
auto marker_endpoint::emit(const Tr& carrier, const document_t& document) const -> void
{
  format_.emit(carrier, document);
}

///
///
auto marker_endpoint::make_writer(document_t& document) const -> arude::config::toml_tree_writer
{
  return format_.make_writer(document);
}

///
///
template<arude::config::path_transport_c Tr>
auto path_endpoint::parse(const Tr& carrier) const -> std::optional<document_t>
{
  auto stream = std::ifstream{carrier.path()};

  if(!stream.is_open())
  {
    return std::nullopt;
  }

  return toml::parse(stream, carrier.path().string());
}

///
///
auto path_endpoint::make_reader(const document_t& document) const -> arude::config::toml_tree_reader
{
  return format_.make_reader(document);
}

///
///
template<arude::config::path_transport_c Tr>
auto path_endpoint::emit(const Tr& carrier, const document_t& document) const -> void
{
  format_.emit(carrier, document);
}

///
///
auto path_endpoint::make_writer(document_t& document) const -> arude::config::toml_tree_writer
{
  return format_.make_writer(document);
}

} // namespace endpoint_test

static_assert(arude::config::endpoint_c<endpoint_test::marker_endpoint, endpoint_test::transport_memory>);

// The constraint is checked against the transport the endpoint is used with,
// so an endpoint that needs a path is fine with a file and refused, at the
// interface, with anything else.
static_assert(arude::config::tree_endpoint_c<endpoint_test::path_endpoint>);
static_assert(arude::config::endpoint_c<endpoint_test::path_endpoint, arude::config::transport_file>);
static_assert(!arude::config::endpoint_c<endpoint_test::path_endpoint, endpoint_test::transport_memory>);
static_assert(endpoint_test::registrable_c<endpoint_test::path_endpoint, arude::config::transport_file>);
static_assert(!endpoint_test::registrable_c<endpoint_test::path_endpoint, endpoint_test::transport_memory>);

SCENARIO("a stateful endpoint's options reach the read", "[config][endpoint]")
{
  GIVEN("one endpoint type, two instances, two options")
  {
    auto store = endpoint_test::transport_memory::store_t{};
    const auto seed = std::string_view{"version = 1\nname = \"seeded\"\n"};
    const auto seed_bytes = [&seed]() -> std::vector<std::byte> { return arude::config::as_bytes(seed); }();
    store["plain"] = seed_bytes;
    store["stamped"] = seed_bytes;

    // Each slot holds the same seed; a load-and-store round trip puts back
    // exactly what each endpoint instance parsed, which is where the options
    // show.
    auto manager = arude::config::config_manager{};
    auto plain_document = manager.register_document(
      endpoint_test::marker_endpoint{endpoint_test::marker_options{.stamp = false}},
      endpoint_test::transport_memory{"plain", store});
    auto stamped_document = manager.register_document(
      endpoint_test::marker_endpoint{endpoint_test::marker_options{.stamp = true}},
      endpoint_test::transport_memory{"stamped", store});

    std::ignore = plain_document.add_root<endpoint_test::marker_config>();
    std::ignore = stamped_document.add_root<endpoint_test::marker_config>();

    WHEN("both documents are loaded and stored back")
    {
      plain_document.load();
      stamped_document.load();
      plain_document.store();
      stamped_document.store();

      THEN("only the instance whose option says so parsed the marker")
      {
        REQUIRE(!arude::config::as_text(store.at("plain")).contains("stamped"));
        REQUIRE(arude::config::as_text(store.at("stamped")).contains("stamped"));
      }
    }
  }
}

SCENARIO("an endpoint constrained to path transports works through a document over a file", "[config][endpoint]")
{
  GIVEN("a file, and a document over it whose endpoint reads by path")
  {
    const auto file = test_helpers::temp_file{"arude_endpoint_path.toml"};
    test_helpers::write_text(file.path(), "version = 1\nname = \"from the file\"\n");

    auto manager = arude::config::config_manager{};
    auto document =
      manager.register_document(endpoint_test::path_endpoint{}, arude::config::transport_file{file.path()});
    auto root = document.add_root<endpoint_test::marker_config>();

    WHEN("it is loaded, changed and stored")
    {
      document.load();
      const auto loaded = root.get().name;

      root.set(endpoint_test::marker_config{.name = "stored"});
      document.store();

      THEN("the load read the file, and the store wrote it")
      {
        REQUIRE(loaded == "from the file");

        auto stream = std::ifstream{file.path()};
        const auto text = std::string{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};

        REQUIRE(text.contains("stored"));
        REQUIRE(!text.contains("from the file"));
      }
    }
  }
}
