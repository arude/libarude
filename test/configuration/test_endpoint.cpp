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

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <map>
#include <span>
#include <string>
#include <string_view>
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
/// Parses TOML like any other, and stamps the document when told to.
///
class marker_endpoint final
{
public: // Typedefs / Constants
  static constexpr auto name = std::string_view{"marker"};
  using document_t = rfl::Generic::Object;

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
  /// \return The parsed document.
  ///
  template<arude::config::transport_c Tr>
  [[nodiscard]] auto parse(const Tr& carrier) const -> document_t;

  ///
  /// \see endpoint_toml::read_section
  ///
  template<arude::config::configuration_c T>
  [[nodiscard]] auto
  read_section(const document_t& document, std::string_view section, arude::config::load_policy policy) const -> T;

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
  /// \see endpoint_toml::write_section
  ///
  template<arude::config::configuration_c T>
  auto write_section(document_t& document, std::string_view section, const T& val) const -> void;

private: // Variables
  marker_options options_;
};

///
///
marker_endpoint::marker_endpoint(marker_options options)
  : options_{std::move(options)}
{
}

///
///
template<arude::config::transport_c Tr>
auto marker_endpoint::parse(const Tr& carrier) const -> document_t
{
  const auto bytes = carrier.read();
  const auto* const begin = reinterpret_cast<const char*>(bytes.data());
  const auto text = std::string{begin, bytes.size()};

  auto result = rfl::toml::read<rfl::Generic>(text);

  if(!result.has_value())
  {
    throw arude::exception{"endpoint_test: the bytes are not TOML"};
  }

  auto document = result.value().to_object().value();

  if(options_.stamp)
  {
    document.insert(std::string{"stamped"}, rfl::Generic{true});
  }

  return document;
}

///
///
template<arude::config::configuration_c T>
auto marker_endpoint::read_section(
  const document_t& document, const std::string_view section, const arude::config::load_policy policy) const -> T
{
  return arude::config::detail::parse_section<T>(document, section, policy, false);
}

///
///
template<arude::config::transport_c Tr>
auto marker_endpoint::emit(const Tr& carrier, const document_t& document) const -> void
{
  const auto text = rfl::toml::write(document);
  const auto* const begin = reinterpret_cast<const std::byte*>(text.data());

  carrier.write(std::span<const std::byte>{begin, text.size()});
}

///
///
template<arude::config::configuration_c T>
auto marker_endpoint::write_section(document_t& document, const std::string_view section, const T& val) const -> void
{
  arude::config::detail::write_section_value<T>(document, section, val);
}

} // namespace endpoint_test

static_assert(arude::config::endpoint_c<endpoint_test::marker_endpoint>);

SCENARIO("a stateful endpoint's options reach the read", "[config][endpoint]")
{
  GIVEN("one endpoint type, two instances, two options")
  {
    auto store = endpoint_test::transport_memory::store_t{};
    const auto seed = std::string_view{"version = 1\nname = \"seeded\"\n"};
    const auto seed_bytes = [&]()
    {
      const auto* const begin = reinterpret_cast<const std::byte*>(seed.data());
      return std::vector<std::byte>{begin, begin + seed.size()};
    }();
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
        REQUIRE(!test_helpers::as_text(store.at("plain")).contains("stamped"));
        REQUIRE(test_helpers::as_text(store.at("stamped")).contains("stamped"));
      }
    }
  }
}
