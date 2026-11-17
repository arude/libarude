///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// An endpoint of a test's own, with a tree of its own and its own tree_reader
/// and tree_writer written from scratch, run through a document. What a
/// document promises — presence, owned keys, the create policy, strictness —
/// is libarude's and not the TOML endpoint's, so it has to hold here as well;
/// a rule that had leaked into the TOML implementation would fail here.
///

#include "arude/config.hpp"
#include "arude/non_owning_t.hpp"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <toml++/toml.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace tree_endpoint_test
{
namespace
{

// The tree is recursive, and so is everything that copies, destroys or walks
// it; it is as deep as the text it was parsed from.
// NOLINTBEGIN(misc-no-recursion)
struct map_value;
struct map_entry;

///
/// An array of the tree.
///
using map_array_t = std::vector<map_value>;

///
/// A table of the tree: keys in the order they were added, which is not the
/// order toml++ keeps.
///
using map_table_t = std::vector<map_entry>;

///
/// One node of the tree: a table, a null, a scalar or an array.
///
struct map_value
{
  ///
  /// What the node holds. The table comes first, so that a node is a table
  /// unless set otherwise; a default member initialiser would need the table
  /// type complete before this one is.
  ///
  std::variant<map_table_t, std::monostate, bool, std::int64_t, std::uint64_t, double, std::string, map_array_t> data;
};

///
/// One key of a table and its node. A struct of its own rather than a
/// std::pair, whose traits would need map_value complete while it is not.
///
struct map_entry
{
  ///
  /// The key.
  ///
  std::string key;

  ///
  /// Its node.
  ///
  map_value value;
};
// NOLINTEND(misc-no-recursion)

///
/// How the test endpoint behaves.
///
struct map_options
{
  ///
  /// \see arude::config::tree_read_options::reject_unknown_fields
  ///
  bool reject_unknown_fields = false;

  ///
  /// Whether converting an integer throws, as a reader with a bug in it would.
  ///
  bool faulty_integers = false;

  ///
  /// Whether adding a value throws, as a writer with a bug in it would.
  ///
  bool faulty_writes = false;
};

///
/// The test endpoint's tree_reader.
///
class map_tree_reader final : public arude::config::tree_reader
{
public: // Structors
  ///
  /// \param document The tree. Retained, and must outlive this.
  /// \param options How to behave. Copied.
  ///
  map_tree_reader(const map_value& document, map_options options);

public: // Accessors
  [[nodiscard]] auto root() const -> arude::config::tree_node override;
  [[nodiscard]] auto kind(arude::config::tree_node node) const -> arude::config::node_kind override;
  [[nodiscard]] auto field(std::string_view key, arude::config::tree_node table) const
    -> std::optional<arude::config::tree_node> override;
  [[nodiscard]] auto element(std::size_t index, arude::config::tree_node array) const
    -> std::optional<arude::config::tree_node> override;
  auto for_each_field(arude::config::tree_node table, const field_visitor& visitor) const -> void override;
  auto for_each_element(arude::config::tree_node array, const element_visitor& visitor) const -> void override;
  [[nodiscard]] auto to_string(arude::config::tree_node node) const -> std::expected<std::string, std::string> override;
  [[nodiscard]] auto to_bool(arude::config::tree_node node) const -> std::expected<bool, std::string> override;
  [[nodiscard]] auto to_int64(arude::config::tree_node node) const -> std::expected<std::int64_t, std::string> override;
  [[nodiscard]] auto to_uint64(arude::config::tree_node node) const
    -> std::expected<std::uint64_t, std::string> override;
  [[nodiscard]] auto to_double(arude::config::tree_node node) const -> std::expected<double, std::string> override;
  [[nodiscard]] auto options() const -> arude::config::tree_read_options override;

private: // Helpers
  ///
  /// \param node A handle.
  /// \return The node it names.
  ///
  [[nodiscard]] static auto value_of(arude::config::tree_node node) -> const map_value&;

private: // Variables
  arude::non_owning_t<const map_value> document_;
  map_options options_;
};

///
/// The test endpoint's tree_writer.
///
class map_tree_writer final : public arude::config::tree_writer
{
public: // Structors
  ///
  /// \param document The tree. Retained, and must outlive this.
  /// \param options How to behave. Copied.
  ///
  map_tree_writer(map_value& document, map_options options);

public: // Methods
  [[nodiscard]] auto root() -> arude::config::tree_branch override;
  [[nodiscard]] auto table(arude::config::tree_branch parent, std::string_view key)
    -> arude::config::tree_branch override;
  auto erase(arude::config::tree_branch parent, std::string_view key) -> void override;
  [[nodiscard]] auto add_table(arude::config::tree_branch parent, std::string_view key)
    -> arude::config::tree_branch override;
  [[nodiscard]] auto add_array(arude::config::tree_branch parent, std::string_view key)
    -> arude::config::tree_branch override;
  [[nodiscard]] auto append_table(arude::config::tree_branch parent) -> arude::config::tree_branch override;
  [[nodiscard]] auto append_array(arude::config::tree_branch parent) -> arude::config::tree_branch override;
  auto add_value(arude::config::tree_branch parent, std::string_view key, const arude::config::tree_scalar_t& value)
    -> void override;
  auto append_value(arude::config::tree_branch parent, const arude::config::tree_scalar_t& value) -> void override;
  auto add_null(arude::config::tree_branch parent, std::string_view key) -> void override;
  auto append_null(arude::config::tree_branch parent) -> void override;

private: // Helpers
  ///
  /// \param branch A branch that is a table.
  /// \return The table.
  ///
  [[nodiscard]] static auto table_of(arude::config::tree_branch branch) -> map_table_t&;

  ///
  /// \param branch A branch that is an array.
  /// \return The array.
  ///
  [[nodiscard]] static auto array_of(arude::config::tree_branch branch) -> map_array_t&;

  ///
  /// \param value A scalar.
  /// \return The node holding it.
  ///
  [[nodiscard]] static auto node_of(const arude::config::tree_scalar_t& value) -> map_value;

private: // Variables
  arude::non_owning_t<map_value> document_;
  map_options options_;
};

///
/// The test endpoint. Its bytes are TOML only so that a test can write a seed
/// by hand; the tree they are parsed into is a map_value.
///
class map_endpoint final
{
public: // Typedefs / Constants
  static constexpr auto name = std::string_view{"map"};
  using document_t = map_value;

public: // Structors
  map_endpoint() = default;

  ///
  /// \param options How to behave. Copied.
  ///
  explicit map_endpoint(map_options options);

public: // Accessors
  ///
  /// \see arude::config::endpoint_c
  ///
  template<arude::config::transport_c Tr>
  [[nodiscard]] auto parse(const Tr& carrier) const -> std::optional<document_t>;

  ///
  /// \see arude::config::tree_endpoint_c
  ///
  [[nodiscard]] auto make_reader(const document_t& document) const -> map_tree_reader;

  ///
  /// \see arude::config::tree_endpoint_c
  ///
  auto make_reader(document_t&& document) const -> map_tree_reader = delete;

public: // Methods
  ///
  /// \see arude::config::endpoint_c
  ///
  template<arude::config::transport_c Tr>
  auto emit(const Tr& carrier, const document_t& document) const -> void;

  ///
  /// \see arude::config::tree_endpoint_c
  ///
  [[nodiscard]] auto make_writer(document_t& document) const -> map_tree_writer;

private: // Variables
  map_options options_;
};

///
/// A transport holding one slot's bytes in memory.
///
class transport_memory final
{
public: // Typedefs / Constants
  using store_t = std::map<std::string, std::vector<std::byte>>;

  static constexpr auto name = std::string_view{"memory"};

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
/// The root configuration.
///
struct app
{
  static constexpr auto config_version = arude::config::version_t{1};
  arude::config::version_t version = config_version;
  std::string name = "example";
};

///
/// A named section.
///
struct net
{
  static constexpr auto config_version = arude::config::version_t{1};
  arude::config::version_t version = config_version;
  std::string host = "localhost";
};

///
/// A named section with a field that may be absent.
///
struct proxy
{
  static constexpr auto config_version = arude::config::version_t{1};
  arude::config::version_t version = config_version;
  std::string host = "proxy.invalid";
  std::optional<std::uint32_t> port;
};

///
/// A section holding the largest unsigned 64-bit integer there is.
///
struct counter
{
  static constexpr auto config_version = arude::config::version_t{1};
  arude::config::version_t version = config_version;
  std::uint64_t total = std::numeric_limits<std::uint64_t>::max();
};

///
/// Builds the tree's node for a toml++ node.
/// \param node The toml++ node.
/// \return The tree's node; a date or a time becomes a null.
///
[[nodiscard]] auto from_toml_node(const toml::node& node) -> map_value;

///
/// Adds a node of the tree to a toml++ table, leaving a null out.
///
/// \param table The table.
/// \param key The key.
/// \param value The node.
///
auto insert_into(toml::table& table, std::string_view key, const map_value& value) -> void;

///
/// Appends a node of the tree to a toml++ array, leaving a null out.
///
/// \param array The array.
/// \param value The node.
///
auto append_into(toml::array& array, const map_value& value) -> void;

///
/// Seeds a slot with TOML text.
///
/// \param store The store.
/// \param slot The slot.
/// \param text The text.
///
auto seed(transport_memory::store_t& store, const std::string& slot, std::string_view text) -> void;

///
/// Requires a call to fail with the error given.
///
/// \param action The call. Not retained.
/// \param error The error.
/// \param mention Text the message must contain.
///
auto require_error(const std::function<void()>& action, arude::config::config_error error, std::string_view mention)
  -> void;

///
///
map_tree_reader::map_tree_reader(const map_value& document, const map_options options)
  : document_{&document}
  , options_{options}
{
}

///
///
auto map_tree_reader::root() const -> arude::config::tree_node
{
  return {.address = document_};
}

///
///
auto map_tree_reader::kind(const arude::config::tree_node node) const -> arude::config::node_kind
{
  const auto& value = value_of(node).data;

  if(std::holds_alternative<map_table_t>(value))
  {
    return arude::config::node_kind::table;
  }

  if(std::holds_alternative<map_array_t>(value))
  {
    return arude::config::node_kind::array;
  }

  if(std::holds_alternative<std::monostate>(value))
  {
    return arude::config::node_kind::null;
  }

  return arude::config::node_kind::value;
}

///
///
auto map_tree_reader::field(const std::string_view key, const arude::config::tree_node table) const
  -> std::optional<arude::config::tree_node>
{
  const auto* const fields = std::get_if<map_table_t>(&value_of(table).data);

  if(fields == nullptr)
  {
    return std::nullopt;
  }

  const auto found = std::ranges::find(*fields, key, &map_entry::key);

  if(found == fields->end())
  {
    return std::nullopt;
  }

  return arude::config::tree_node{.address = &found->value};
}

///
///
auto map_tree_reader::element(const std::size_t index, const arude::config::tree_node array) const
  -> std::optional<arude::config::tree_node>
{
  const auto* const elements = std::get_if<map_array_t>(&value_of(array).data);

  if(elements == nullptr || index >= elements->size())
  {
    return std::nullopt;
  }

  return arude::config::tree_node{.address = &elements->at(index)};
}

///
///
auto map_tree_reader::for_each_field(const arude::config::tree_node table, const field_visitor& visitor) const -> void
{
  if(const auto* const fields = std::get_if<map_table_t>(&value_of(table).data); fields != nullptr)
  {
    for(const auto& [key, value] : *fields)
    {
      visitor.visit(key, {.address = &value});
    }
  }
}

///
///
auto map_tree_reader::for_each_element(const arude::config::tree_node array, const element_visitor& visitor) const
  -> void
{
  if(const auto* const elements = std::get_if<map_array_t>(&value_of(array).data); elements != nullptr)
  {
    for(const auto& value : *elements)
    {
      if(!visitor.visit({.address = &value}))
      {
        return;
      }
    }
  }
}

///
///
auto map_tree_reader::to_string(const arude::config::tree_node node) const -> std::expected<std::string, std::string>
{
  if(const auto* const value = std::get_if<std::string>(&value_of(node).data); value != nullptr)
  {
    return *value;
  }

  return std::unexpected{std::string{"map: not a string"}};
}

///
///
auto map_tree_reader::to_bool(const arude::config::tree_node node) const -> std::expected<bool, std::string>
{
  if(const auto* const value = std::get_if<bool>(&value_of(node).data); value != nullptr)
  {
    return *value;
  }

  return std::unexpected{std::string{"map: not a boolean"}};
}

///
///
auto map_tree_reader::to_int64(const arude::config::tree_node node) const -> std::expected<std::int64_t, std::string>
{
  if(options_.faulty_integers)
  {
    throw std::runtime_error{"map: the integer conversion is broken"};
  }

  if(const auto* const value = std::get_if<std::int64_t>(&value_of(node).data); value != nullptr)
  {
    return *value;
  }

  return std::unexpected{std::string{"map: not a signed integer"}};
}

///
///
auto map_tree_reader::to_uint64(const arude::config::tree_node node) const -> std::expected<std::uint64_t, std::string>
{
  if(options_.faulty_integers)
  {
    throw std::runtime_error{"map: the integer conversion is broken"};
  }

  const auto& value = value_of(node).data;

  if(const auto* const unsigned_value = std::get_if<std::uint64_t>(&value); unsigned_value != nullptr)
  {
    return *unsigned_value;
  }

  // What came from TOML is signed, as TOML has nothing else.
  if(const auto* const signed_value = std::get_if<std::int64_t>(&value); signed_value != nullptr && *signed_value >= 0)
  {
    return static_cast<std::uint64_t>(*signed_value);
  }

  return std::unexpected{std::string{"map: not an unsigned integer"}};
}

///
///
auto map_tree_reader::to_double(const arude::config::tree_node node) const -> std::expected<double, std::string>
{
  if(const auto* const value = std::get_if<double>(&value_of(node).data); value != nullptr)
  {
    return *value;
  }

  return std::unexpected{std::string{"map: not a floating-point number"}};
}

///
///
auto map_tree_reader::options() const -> arude::config::tree_read_options
{
  return {.reject_unknown_fields = options_.reject_unknown_fields};
}

///
///
auto map_tree_reader::value_of(const arude::config::tree_node node) -> const map_value&
{
  return *static_cast<const map_value*>(node.address);
}

///
///
map_tree_writer::map_tree_writer(map_value& document, const map_options options)
  : document_{&document}
  , options_{options}
{
}

///
///
auto map_tree_writer::root() -> arude::config::tree_branch
{
  return {.address = document_};
}

///
///
auto map_tree_writer::table(const arude::config::tree_branch parent, const std::string_view key)
  -> arude::config::tree_branch
{
  auto& fields = table_of(parent);
  const auto found = std::ranges::find(fields, key, &map_entry::key);

  if(found == fields.end())
  {
    return add_table(parent, key);
  }

  if(!std::holds_alternative<map_table_t>(found->value.data))
  {
    found->value = map_value{};
  }

  return {.address = &found->value};
}

///
///
auto map_tree_writer::erase(const arude::config::tree_branch parent, const std::string_view key) -> void
{
  std::erase_if(table_of(parent), [key](const map_entry& entry) -> bool { return entry.key == key; });
}

///
///
auto map_tree_writer::add_table(const arude::config::tree_branch parent, const std::string_view key)
  -> arude::config::tree_branch
{
  return {.address = &table_of(parent).emplace_back(std::string{key}, map_value{}).value};
}

///
///
auto map_tree_writer::add_array(const arude::config::tree_branch parent, const std::string_view key)
  -> arude::config::tree_branch
{
  return {.address = &table_of(parent).emplace_back(std::string{key}, map_value{.data = map_array_t{}}).value};
}

///
///
auto map_tree_writer::append_table(const arude::config::tree_branch parent) -> arude::config::tree_branch
{
  return {.address = &array_of(parent).emplace_back()};
}

///
///
auto map_tree_writer::append_array(const arude::config::tree_branch parent) -> arude::config::tree_branch
{
  return {.address = &array_of(parent).emplace_back(map_value{.data = map_array_t{}})};
}

///
///
auto map_tree_writer::add_value(
  const arude::config::tree_branch parent, const std::string_view key, const arude::config::tree_scalar_t& value)
  -> void
{
  if(options_.faulty_writes)
  {
    throw std::runtime_error{"map: adding a value is broken"};
  }

  table_of(parent).emplace_back(std::string{key}, node_of(value));
}

///
///
auto map_tree_writer::append_value(const arude::config::tree_branch parent, const arude::config::tree_scalar_t& value)
  -> void
{
  array_of(parent).push_back(node_of(value));
}

///
///
auto map_tree_writer::add_null(const arude::config::tree_branch parent, const std::string_view key) -> void
{
  table_of(parent).emplace_back(std::string{key}, map_value{.data = std::monostate{}});
}

///
///
auto map_tree_writer::append_null(const arude::config::tree_branch parent) -> void
{
  array_of(parent).push_back(map_value{.data = std::monostate{}});
}

///
///
auto map_tree_writer::table_of(const arude::config::tree_branch branch) -> map_table_t&
{
  return std::get<map_table_t>(static_cast<map_value*>(branch.address)->data);
}

///
///
auto map_tree_writer::array_of(const arude::config::tree_branch branch) -> map_array_t&
{
  return std::get<map_array_t>(static_cast<map_value*>(branch.address)->data);
}

///
///
auto map_tree_writer::node_of(const arude::config::tree_scalar_t& value) -> map_value
{
  return std::visit(
    []<typename T>(const T& scalar) -> map_value
    {
      if constexpr(std::is_same_v<T, std::string_view>)
      {
        return map_value{.data = std::string{scalar}};
      }
      else
      {
        return map_value{.data = scalar};
      }
    },
    value);
}

///
///
map_endpoint::map_endpoint(const map_options options)
  : options_{options}
{
}

///
///
template<arude::config::transport_c Tr>
auto map_endpoint::parse(const Tr& carrier) const -> std::optional<document_t>
{
  const auto bytes = carrier.read();

  if(!bytes.has_value())
  {
    return std::nullopt;
  }

  return from_toml_node(toml::parse(arude::config::as_text(bytes.value())));
}

///
///
auto map_endpoint::make_reader(const document_t& document) const -> map_tree_reader
{
  return map_tree_reader{document, options_};
}

///
///
template<arude::config::transport_c Tr>
auto map_endpoint::emit(const Tr& carrier, const document_t& document) const -> void
{
  auto table = toml::table{};

  for(const auto& [key, value] : std::get<map_table_t>(document.data))
  {
    insert_into(table, key, value);
  }

  auto stream = std::ostringstream{};
  stream << table;
  carrier.write(arude::config::as_bytes(stream.str()));
}

///
///
// NOLINTNEXTLINE(readability-convert-member-functions-to-static): the contract is an instance operation.
auto map_endpoint::make_writer(document_t& document) const -> map_tree_writer
{
  return map_tree_writer{document, options_};
}

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
///
// Recursion follows the depth of the tree, which is the depth of the text.
// NOLINTBEGIN(misc-no-recursion)
auto from_toml_node(const toml::node& node) -> map_value
{
  if(const auto* const table = node.as_table(); table != nullptr)
  {
    auto fields = map_table_t{};

    for(const auto& [key, value] : *table)
    {
      fields.emplace_back(std::string{key.str()}, from_toml_node(value));
    }

    return map_value{.data = std::move(fields)};
  }

  if(const auto* const array = node.as_array(); array != nullptr)
  {
    auto elements = map_array_t{};

    for(const auto& value : *array)
    {
      elements.push_back(from_toml_node(value));
    }

    return map_value{.data = std::move(elements)};
  }

  if(const auto value = node.value_exact<std::int64_t>(); value.has_value())
  {
    return map_value{.data = value.value()};
  }

  if(const auto value = node.value_exact<double>(); value.has_value())
  {
    return map_value{.data = value.value()};
  }

  if(const auto value = node.value_exact<bool>(); value.has_value())
  {
    return map_value{.data = value.value()};
  }

  if(const auto value = node.value_exact<std::string>(); value.has_value())
  {
    return map_value{.data = value.value()};
  }

  // A date or a time, which the test tree has no kind for. No test seeds one.
  return map_value{.data = std::monostate{}};
}

///
///
auto insert_into(toml::table& table, const std::string_view key, const map_value& value) -> void
{
  std::visit(
    [&table, key]<typename T>(const T& held) -> void
    {
      if constexpr(std::is_same_v<T, map_table_t>)
      {
        auto& nested = *table.insert_or_assign(key, toml::table{}).first->second.as_table();

        for(const auto& [nested_key, nested_value] : held)
        {
          insert_into(nested, nested_key, nested_value);
        }
      }
      else if constexpr(std::is_same_v<T, map_array_t>)
      {
        auto& nested = *table.insert_or_assign(key, toml::array{}).first->second.as_array();

        for(const auto& element : held)
        {
          append_into(nested, element);
        }
      }
      else if constexpr(std::is_same_v<T, std::uint64_t>)
      {
        table.insert_or_assign(key, static_cast<std::int64_t>(held));
      }
      else if constexpr(!std::is_same_v<T, std::monostate>)
      {
        table.insert_or_assign(key, held);
      }
    },
    value.data);
}

///
///
auto append_into(toml::array& array, const map_value& value) -> void
{
  std::visit(
    [&array]<typename T>(const T& held) -> void
    {
      if constexpr(std::is_same_v<T, map_table_t>)
      {
        array.push_back(toml::table{});
        auto& nested = *array.back().as_table();

        for(const auto& [nested_key, nested_value] : held)
        {
          insert_into(nested, nested_key, nested_value);
        }
      }
      else if constexpr(std::is_same_v<T, map_array_t>)
      {
        array.push_back(toml::array{});
        auto& nested = *array.back().as_array();

        for(const auto& element : held)
        {
          append_into(nested, element);
        }
      }
      else if constexpr(std::is_same_v<T, std::uint64_t>)
      {
        array.push_back(static_cast<std::int64_t>(held));
      }
      else if constexpr(!std::is_same_v<T, std::monostate>)
      {
        array.push_back(held);
      }
    },
    value.data);
}
// NOLINTEND(misc-no-recursion)

///
///
auto seed(transport_memory::store_t& store, const std::string& slot, const std::string_view text) -> void
{
  store.insert_or_assign(slot, arude::config::as_bytes(text));
}

///
///
auto require_error(
  const std::function<void()>& action, const arude::config::config_error error, const std::string_view mention) -> void
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
    REQUIRE(ex.str().contains(mention));
  }
}

} // namespace

} // namespace tree_endpoint_test

static_assert(arude::config::tree_endpoint_c<tree_endpoint_test::map_endpoint>);
static_assert(arude::config::endpoint_c<tree_endpoint_test::map_endpoint, tree_endpoint_test::transport_memory>);

SCENARIO("a document over an endpoint of its own finds its sections by libarude's rules", "[config][tree_endpoint]")
{
  auto store = tree_endpoint_test::transport_memory::store_t{};
  auto manager = arude::config::config_manager{};
  auto document =
    manager.register_document(tree_endpoint_test::map_endpoint{}, tree_endpoint_test::transport_memory{"slot", store});
  auto root = document.add_root<tree_endpoint_test::app>();
  auto network = document.add_section<tree_endpoint_test::net>("net");

  GIVEN("a source holding a named section and an array of tables, and no root")
  {
    tree_endpoint_test::seed(
      store,
      "slot",
      "[[plugins]]\n"
      "name = \"first\"\n"
      "\n"
      "[net]\n"
      "version = 1\n"
      "host = \"from the source\"\n");

    THEN("the root is missing, and the create policy gives it its defaults")
    {
      tree_endpoint_test::require_error(
        [&document] -> void { document.load(); }, arude::config::config_error::not_found, "the root section");

      document.load(arude::config::load_policy::create);

      REQUIRE(root.get().name == "example");
      REQUIRE(network.get().host == "from the source");
    }
  }

  GIVEN("a source holding an empty array at the top level, and no version")
  {
    tree_endpoint_test::seed(
      store,
      "slot",
      "empty = []\n"
      "\n"
      "[net]\n"
      "version = 1\n"
      "host = \"from the source\"\n");

    THEN("the root is there and malformed")
    {
      tree_endpoint_test::require_error(
        [&document] -> void { document.load(arude::config::load_policy::create); },
        arude::config::config_error::invalid_payload,
        "declares no configuration version");

      REQUIRE(!root.loaded());
    }
  }

  GIVEN("a source holding an array of a table and a value at the top level, and no version")
  {
    tree_endpoint_test::seed(
      store,
      "slot",
      "mixed = [{ name = \"first\" }, 2]\n"
      "\n"
      "[net]\n"
      "version = 1\n"
      "host = \"from the source\"\n");

    THEN("the root is there and malformed")
    {
      tree_endpoint_test::require_error(
        [&document] -> void { document.load(arude::config::load_policy::create); },
        arude::config::config_error::invalid_payload,
        "declares no configuration version");
    }
  }

  GIVEN("a source holding a root and no named section")
  {
    tree_endpoint_test::seed(store, "slot", "version = 1\nname = \"from the source\"\n");

    THEN("the named section is missing, and the create policy gives it its defaults")
    {
      tree_endpoint_test::require_error(
        [&document] -> void { document.load(); }, arude::config::config_error::not_found, "section 'net'");

      document.load(arude::config::load_policy::create);

      REQUIRE(root.get().name == "from the source");
      REQUIRE(network.get().host == "localhost");
    }
  }

  GIVEN("no source at all")
  {
    THEN("the create policy gives every section its defaults, and a store writes them")
    {
      document.load(arude::config::load_policy::create);
      document.store();

      document.load();

      REQUIRE(root.get().name == "example");
      REQUIRE(network.get().host == "localhost");
    }
  }
}

SCENARIO("a store through an endpoint of its own replaces exactly what a section owns", "[config][tree_endpoint]")
{
  auto store = tree_endpoint_test::transport_memory::store_t{};
  auto manager = arude::config::config_manager{};
  auto document =
    manager.register_document(tree_endpoint_test::map_endpoint{}, tree_endpoint_test::transport_memory{"slot", store});
  auto section = document.add_section<tree_endpoint_test::proxy>("proxy");

  GIVEN("a section loaded with its optional field set, beside a key its type does not declare")
  {
    tree_endpoint_test::seed(
      store,
      "slot",
      "[proxy]\n"
      "version = 1\n"
      "host = \"from the source\"\n"
      "port = 3128\n"
      "extra = \"kept\"\n");

    document.load();

    REQUIRE(section.get().port == 3128U);

    WHEN("the field is reset and the document stored")
    {
      auto changed = section.get();
      changed.port.reset();
      changed.host = "stored";
      section.set(changed);
      document.store();

      const auto stored = toml::parse(arude::config::as_text(store.at("slot")));

      THEN("the field left the source, and the undeclared key stayed")
      {
        REQUIRE(stored["proxy"]["host"].value<std::string>() == "stored");
        REQUIRE(!stored["proxy"]["port"]);
        REQUIRE(stored["proxy"]["extra"].value<std::string>() == "kept");
      }
    }
  }
}

SCENARIO("an endpoint of its own carries strictness to the sections it is read by", "[config][tree_endpoint]")
{
  auto store = tree_endpoint_test::transport_memory::store_t{};
  tree_endpoint_test::seed(
    store,
    "slot",
    "[net]\n"
    "version = 1\n"
    "host = \"from the source\"\n"
    "extra = 1\n");

  GIVEN("a strict reader")
  {
    auto manager = arude::config::config_manager{};
    auto document = manager.register_document(
      tree_endpoint_test::map_endpoint{tree_endpoint_test::map_options{.reject_unknown_fields = true}},
      tree_endpoint_test::transport_memory{"slot", store});
    auto network = document.add_section<tree_endpoint_test::net>("net");

    THEN("the undeclared key is an invalid_payload")
    {
      tree_endpoint_test::require_error(
        [&document] -> void { document.load(); }, arude::config::config_error::invalid_payload, "extra");

      REQUIRE(!network.loaded());
    }
  }

  GIVEN("a lenient reader")
  {
    auto manager = arude::config::config_manager{};
    auto document = manager.register_document(
      tree_endpoint_test::map_endpoint{}, tree_endpoint_test::transport_memory{"slot", store});
    auto network = document.add_section<tree_endpoint_test::net>("net");

    THEN("the undeclared key is passed over")
    {
      document.load();

      REQUIRE(network.get().host == "from the source");
    }
  }
}

// reflect-cpp's parsers are noexcept, and the reader is called from inside
// them, so a throw has to be caught before it gets there or it terminates.
SCENARIO("a reader that throws fails the load rather than the program", "[config][tree_endpoint]")
{
  GIVEN("a document over an endpoint whose integer conversion throws")
  {
    auto store = tree_endpoint_test::transport_memory::store_t{};
    tree_endpoint_test::seed(store, "slot", "version = 1\nname = \"from the source\"\n");

    auto manager = arude::config::config_manager{};
    auto document = manager.register_document(
      tree_endpoint_test::map_endpoint{tree_endpoint_test::map_options{.faulty_integers = true}},
      tree_endpoint_test::transport_memory{"slot", store});
    auto root = document.add_root<tree_endpoint_test::app>();

    THEN("the load is an invalid_payload that reports what was thrown, and loads nothing")
    {
      tree_endpoint_test::require_error(
        [&document] -> void { document.load(); },
        arude::config::config_error::invalid_payload,
        "the integer conversion is broken");

      REQUIRE(!root.loaded());
    }
  }
}

// The public write_section() has no document around it to translate what the
// writer throws, so it does that itself, as read_section() does.
SCENARIO("a writer that throws fails the write with an io_error", "[config][tree_endpoint]")
{
  GIVEN("an endpoint whose writer throws when it adds a value")
  {
    const auto format = tree_endpoint_test::map_endpoint{tree_endpoint_test::map_options{.faulty_writes = true}};

    WHEN("a section is written into a tree of its own through write_section()")
    {
      auto document = tree_endpoint_test::map_endpoint::document_t{};

      THEN("the write is an io_error that reports what was thrown")
      {
        tree_endpoint_test::require_error(
          [&format, &document] -> void
          { arude::config::write_section(format, document, "counter", tree_endpoint_test::counter{}); },
          arude::config::config_error::io_error,
          "adding a value is broken");
      }
    }

    WHEN("a document over it is stored with a section set")
    {
      auto store = tree_endpoint_test::transport_memory::store_t{};

      auto manager = arude::config::config_manager{};
      auto document = manager.register_document(format, tree_endpoint_test::transport_memory{"slot", store});
      auto root = document.add_root<tree_endpoint_test::app>();
      root.set(tree_endpoint_test::app{});

      THEN("the store is an io_error that reports what was thrown, and writes nothing")
      {
        tree_endpoint_test::require_error(
          [&document] -> void { document.store(); }, arude::config::config_error::io_error, "adding a value is broken");

        REQUIRE(!store.contains("slot"));
      }
    }
  }
}

SCENARIO("a section round-trips through an endpoint of its own without a transport", "[config][tree_endpoint]")
{
  GIVEN("a tree of the test endpoint's own and a value holding the largest unsigned 64-bit integer")
  {
    const auto format = tree_endpoint_test::map_endpoint{};
    auto document = tree_endpoint_test::map_endpoint::document_t{};

    WHEN("it is written into the tree and read back")
    {
      arude::config::write_section(format, document, "counter", tree_endpoint_test::counter{});

      const auto read = arude::config::read_section<tree_endpoint_test::counter>(
        format, document, "counter", arude::config::load_policy::none);

      THEN("the tree holds it as an unsigned integer, and it reads back whole")
      {
        const auto& fields = std::get<tree_endpoint_test::map_table_t>(document.data);
        const auto& section = std::get<tree_endpoint_test::map_table_t>(fields.at(0).value.data);
        const auto total = std::ranges::find(section, std::string_view{"total"}, &tree_endpoint_test::map_entry::key);

        REQUIRE(total != section.end());
        REQUIRE(std::holds_alternative<std::uint64_t>(total->value.data));
        REQUIRE(read.total == std::numeric_limits<std::uint64_t>::max());
      }
    }
  }
}
