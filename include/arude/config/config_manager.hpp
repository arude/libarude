///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// The entry point an application reaches for when it wants configuration.
///
/// One manager holds everything an application is configured by, keyed by the
/// URI each configuration came from, so a subsystem that needs a setting asks
/// the manager rather than being handed a copy or reading the file again. The
/// cache is what makes that cheap: load() touches the source, get() does not.
///
/// It is one instance by construction and not by enforcement — there is no
/// singleton here, and where the instance lives and how long it lives are the
/// application's to decide. Inject it through a constructor, as
/// docs/cpp-conventions.md asks, rather than reaching for a global.
///
/// A URI names both the format and where it is kept, as format+transport:
///
/// ```cpp
/// auto manager = arude::config::config_manager{};
/// const auto uri = arude::config::create_uri("/etc/example/app.toml"); // toml+file:///etc/example/app.toml
///
/// auto app = manager.load<arude::config::config_test_t>(uri);          // Reads, migrates, caches.
/// app.retries = 5;
/// manager.set(uri, app);                                               // Cache only.
/// manager.store<arude::config::config_test_t>(uri);                    // Writes the cache back out.
/// ```
///
/// Files are the only transport so far. Every failure is an
/// arude::exception<config_manager::errors> saying which of the five things
/// went wrong, so a caller can tell a missing file from a corrupt one without
/// reading the message.
///
#pragma once

#include "arude/config/config_file.hpp"
#include "arude/config/endpoint.hpp"
#include "arude/config/migration.hpp"
#include "arude/exception.hpp"
#include "arude/exception_report.hpp"
#include "arude/noncopyable.hpp"
#include "arude/type_name.hpp"

#include <boost/url.hpp>

#include <any>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace arude::config
{

///
/// Holds an application's configurations, and reads and writes them.
/// One instance, owned by the application; see the file comment for the shape
/// of a typical use.
///
/// Move-only, through arude::noncopyable: two managers over one set of sources
/// would each cache half the truth, while moving one into place after building
/// it is exactly how an application is meant to get hold of one. Declaring a
/// destructor here would take the move members away again, so there is none.
///
class config_manager final : public noncopyable
{
public: // Typedefs / Constants
  using uri_t = boost::urls::url;
  using uri_view_t = boost::urls::url_view_base;
  using key_t = std::string;
  using cache_t = std::unordered_map<key_t, std::any>;

  ///
  /// What went wrong, carried as the payload of every exception thrown here.
  /// Distinguishing these is the point: a caller can create a configuration on
  /// not_found, report io_error to the user, and refuse to start on
  /// invalid_payload, which one message string would not let it do.
  ///
  /// Formats as its own name, through arude/enum.hpp, so an exception report
  /// names the error rather than calling the payload unformattable.
  ///
  enum class errors : std::uint8_t
  {
    ///
    /// The URI names an endpoint or a transport this manager cannot act on, or no file at all.
    ///
    invalid_uri,

    ///
    /// The source holds a version that cannot be brought to the one asked for.
    ///
    invalid_version,

    ///
    /// The source is not a configuration of that type, or the cache holds another type under the URI.
    ///
    invalid_payload,

    ///
    /// The source could not be read or written.
    ///
    io_error,

    ///
    /// There is nothing at the URI, or nothing in the cache for it.
    ///
    not_found
  };

  ///
  /// What load() should do about a source that is missing or of another version.
  /// A bitmask, so the flags combine; is_set() is what reads one back out.
  ///
  enum class load_policy : std::uint8_t
  {
    ///
    /// A source must be there, and must be the version asked for.
    ///
    none = 0x00,

    ///
    /// A missing source is written out from the defaults rather than being an error.
    ///
    create = 0x01,

    ///
    /// An older version is migrated forward; without this, an older source is an error.
    ///
    upgrade_to_current = 0x02,

    ///
    /// The version must match exactly, whatever upgrade_to_current says.
    ///
    strict_version = 0x04
  };

  using exception_t = exception<errors>;

public: // Accessors
  ///
  /// Parses text as a URI.
  /// Static, because a URI has to be built before there is anything to do with
  /// it. Prefer create_uri(), which cannot produce one this manager will
  /// refuse; this is for a URI that arrived from a command line or a file.
  ///
  /// \param text URI text. Not retained.
  /// \return The parsed URI.
  /// \throws arude::exception<errors> With errors::invalid_uri if the text is not a URI.
  ///
  [[nodiscard]] static auto parse_uri(std::string_view text) -> uri_t;

  ///
  /// Returns what the cache holds for a URI.
  /// Never touches the source: a URI that has not been loaded is not_found
  /// rather than an implied load.
  ///
  /// \tparam T Configuration type, which must be the type that was cached.
  /// \param uri Source the configuration came from.
  /// \return A copy of the cached configuration.
  /// \throws arude::exception<errors> With errors::not_found if nothing is cached for the URI,
  ///         or errors::invalid_payload if what is cached is another type.
  ///
  template<configuration_c T>
  [[nodiscard]] auto get(const uri_view_t& uri) const -> T;

  ///
  /// Reports whether anything is cached for a URI.
  ///
  /// \param uri Source to look for.
  /// \return true if the cache holds an entry for it.
  ///
  [[nodiscard]] auto contains(const uri_view_t& uri) const -> bool;

  ///
  /// Returns the number of configurations cached.
  ///
  /// \return The number of entries.
  ///
  [[nodiscard]] auto size() const -> std::size_t;

public: // Methods
  ///
  /// Reads a configuration from its source, caches it, and returns it.
  /// The source is read every time, and what is read replaces whatever the
  /// cache held for the URI. get() is the one that does not touch the source.
  ///
  /// With load_policy::create, a source that is not there is written out from
  /// T's defaults, so that the file the user is meant to edit exists after the
  /// first run; the directories leading to it are created too.
  ///
  /// \tparam T Configuration type, usually the current version alias.
  /// \tparam E Endpoint, which must match the URI's scheme.
  /// \param uri Source to read. Not retained beyond the call, but its text becomes the cache key.
  /// \param policy What to do about a missing source or an older version.
  /// \return The configuration, migrated to T where the policy allows it.
  /// \throws arude::exception<errors> With errors::invalid_uri, not_found, io_error, invalid_payload
  ///         or invalid_version, according to what stopped it.
  ///
  template<configuration_c T, endpoint_c E = endpoint_toml>
  [[nodiscard]] auto load(const uri_view_t& uri, load_policy policy = load_policy::upgrade_to_current) -> T;

  ///
  /// Writes a configuration to its source and caches it.
  /// The directories leading to the source are created if they are missing.
  /// The version written is T's, whatever the value's version member holds.
  ///
  /// \tparam T Configuration type.
  /// \tparam E Endpoint, which must match the URI's scheme.
  /// \param uri Source to write.
  /// \param value Configuration to write. Not retained; a copy is cached.
  /// \throws arude::exception<errors> With errors::invalid_uri or errors::io_error.
  ///
  template<configuration_c T, endpoint_c E = endpoint_toml>
  auto store(const uri_view_t& uri, const T& value) -> void;

  ///
  /// Writes what the cache holds for a URI back to its source.
  /// The counterpart to set(): change the cached configuration, then commit it.
  ///
  /// \tparam T Configuration type, which must be the type that was cached.
  /// \tparam E Endpoint, which must match the URI's scheme.
  /// \param uri Source to write.
  /// \throws arude::exception<errors> With errors::not_found if nothing is cached for the URI,
  ///         errors::invalid_payload if what is cached is another type, or errors::invalid_uri
  ///         or errors::io_error from the writing itself.
  ///
  template<configuration_c T, endpoint_c E = endpoint_toml>
  auto store(const uri_view_t& uri) -> void;

  ///
  /// Puts a configuration in the cache without writing it anywhere.
  /// Replaces whatever was cached for the URI, including a value of another
  /// type. Nothing reaches the source until store() is called.
  ///
  /// \tparam T Configuration type.
  /// \param uri Source the configuration belongs to. Its text becomes the cache key.
  /// \param value Configuration to cache. Not retained; a copy is cached.
  ///
  template<configuration_c T>
  auto set(const uri_view_t& uri, const T& value) -> void;

  ///
  /// Drops the cache entry for a URI, leaving the source alone.
  /// Anything set() put there and store() has not written out is lost.
  ///
  /// \param uri Source to drop.
  /// \return true if there was an entry to drop.
  ///
  auto evict(const uri_view_t& uri) -> bool;

  ///
  /// Drops every cache entry, leaving the sources alone.
  ///
  auto evict_all() -> void;

private: // Accessors
  ///
  /// Returns the cache key a URI is held under, which is its text as given.
  /// Two spellings of one source are two entries, which is why create_uri()
  /// makes the path absolute and normalised before it builds a URI.
  ///
  /// \param uri Source to key.
  /// \return The key.
  ///
  [[nodiscard]] static auto key(const uri_view_t& uri) -> key_t;

  ///
  /// Returns the file a URI names, checking that the URI is one E can act on.
  ///
  /// \tparam E Endpoint the URI's scheme must name.
  /// \param uri Source to resolve.
  /// \return The path the URI names.
  /// \throws arude::exception<errors> With errors::invalid_uri if the scheme is not E's over a
  ///         file, or if the URI names no path.
  ///
  template<endpoint_c E>
  [[nodiscard]] static auto source_path(const uri_view_t& uri) -> std::filesystem::path;

  ///
  /// Turns the text read from a source into a configuration.
  /// Split out of load() because this is where the version is decided, and
  /// where all three of the payload and version failures are told apart.
  ///
  /// \tparam T Configuration type.
  /// \tparam E Endpoint to parse with.
  /// \param text Text read from the source. Not retained.
  /// \param source What to call the source in an error message.
  /// \param policy What to do about a version other than T's.
  /// \return The configuration, migrated to T where the policy allows it.
  /// \throws arude::exception<errors> With errors::invalid_payload or errors::invalid_version.
  ///
  template<configuration_c T, endpoint_c E>
  [[nodiscard]] static auto parse(const std::string& text, const std::string& source, load_policy policy) -> T;

private: // Variables
  cache_t cache_;
};

///
/// Combines two load policies.
///
/// \param lhs First policy.
/// \param rhs Second policy.
/// \return The policy holding the flags of both.
///
[[nodiscard]] constexpr auto operator|(config_manager::load_policy lhs, config_manager::load_policy rhs)
  -> config_manager::load_policy;

///
/// Intersects two load policies.
///
/// \param lhs First policy.
/// \param rhs Second policy.
/// \return The policy holding the flags both have.
///
[[nodiscard]] constexpr auto operator&(config_manager::load_policy lhs, config_manager::load_policy rhs)
  -> config_manager::load_policy;

///
/// Reports whether a policy carries a flag.
///
/// \param value Policy to test.
/// \param flag Flag to look for.
/// \return true if the policy carries it.
///
[[nodiscard]] constexpr auto is_set(config_manager::load_policy value, config_manager::load_policy flag) -> bool;

///
///
constexpr auto operator|(const config_manager::load_policy lhs, const config_manager::load_policy rhs)
  -> config_manager::load_policy
{
  return static_cast<config_manager::load_policy>(std::to_underlying(lhs) | std::to_underlying(rhs));
}

///
///
constexpr auto operator&(const config_manager::load_policy lhs, const config_manager::load_policy rhs)
  -> config_manager::load_policy
{
  return static_cast<config_manager::load_policy>(std::to_underlying(lhs) & std::to_underlying(rhs));
}

///
///
constexpr auto is_set(const config_manager::load_policy value, const config_manager::load_policy flag) -> bool
{
  return (value & flag) == flag;
}

///
///
inline auto config_manager::parse_uri(const std::string_view text) -> uri_t
{
  const auto result = boost::urls::parse_uri(text);

  if(!result.has_value())
  {
    throw exception_t{std::format("arude::config: '{}' is not a URI.", text), errors::invalid_uri};
  }

  return uri_t{result.value()};
}

///
///
template<configuration_c T>
auto config_manager::get(const uri_view_t& uri) const -> T
{
  const auto entry = cache_.find(key(uri));

  if(entry == cache_.end())
  {
    throw exception_t{std::format("arude::config: nothing is cached for '{}'.", key(uri)), errors::not_found};
  }

  // The pointer form rather than the throwing one: a type mismatch is reported
  // as this manager's own error, not as std::bad_any_cast from three layers
  // down.
  const auto* const value = std::any_cast<T>(&entry->second);

  if(value == nullptr)
  {
    throw exception_t{
      std::format("arude::config: what is cached for '{}' is not a {}.", key(uri), type_name<T>()),
      errors::invalid_payload};
  }

  return *value;
}

///
///
inline auto config_manager::contains(const uri_view_t& uri) const -> bool
{
  return cache_.contains(key(uri));
}

///
///
inline auto config_manager::size() const -> std::size_t
{
  return cache_.size();
}

///
///
template<configuration_c T, endpoint_c E>
auto config_manager::load(const uri_view_t& uri, const load_policy policy) -> T
{
  const auto path = source_path<E>(uri);
  const auto source = path.string();

  if(!std::filesystem::exists(path))
  {
    if(!is_set(policy, load_policy::create))
    {
      throw exception_t{std::format("arude::config: there is no configuration at '{}'.", source), errors::not_found};
    }

    // Written out rather than only returned, so that the file the user is
    // meant to edit is there after the first run.
    const auto created = T{};
    store<T, E>(uri, created);

    return created;
  }

  auto text = std::string{};

  try
  {
    text = detail::read_file(path);
  }
  catch(...)
  {
    throw exception_t{
      std::format("arude::config: '{}' could not be read: {}", source, exception_report()), errors::io_error};
  }

  auto value = parse<T, E>(text, source, policy);
  cache_.insert_or_assign(key(uri), std::any{value});

  return value;
}

///
///
template<configuration_c T, endpoint_c E>
auto config_manager::store(const uri_view_t& uri, const T& value) -> void
{
  const auto path = source_path<E>(uri);

  try
  {
    if(const auto parent = path.parent_path(); !parent.empty())
    {
      std::filesystem::create_directories(parent);
    }

    detail::write_file(path, E::write(value));
  }
  catch(...)
  {
    throw exception_t{
      std::format("arude::config: '{}' could not be written: {}", path.string(), exception_report()), errors::io_error};
  }

  cache_.insert_or_assign(key(uri), std::any{value});
}

///
///
template<configuration_c T, endpoint_c E>
auto config_manager::store(const uri_view_t& uri) -> void
{
  store<T, E>(uri, get<T>(uri));
}

///
///
template<configuration_c T>
auto config_manager::set(const uri_view_t& uri, const T& value) -> void
{
  cache_.insert_or_assign(key(uri), std::any{value});
}

///
///
inline auto config_manager::evict(const uri_view_t& uri) -> bool
{
  return cache_.erase(key(uri)) != 0;
}

///
///
inline auto config_manager::evict_all() -> void
{
  cache_.clear();
}

///
///
inline auto config_manager::key(const uri_view_t& uri) -> key_t
{
  return key_t{uri.buffer()};
}

///
///
template<endpoint_c E>
auto config_manager::source_path(const uri_view_t& uri) -> std::filesystem::path
{
  const auto expected = uri_scheme<E>();
  const auto scheme = std::string{uri.scheme()};

  if(scheme != expected)
  {
    throw exception_t{
      std::format("arude::config: '{}' is a '{}' URI, and a '{}' one was expected.", key(uri), scheme, expected),
      errors::invalid_uri};
  }

  auto path = std::filesystem::path{std::string{uri.path()}};

  if(path.empty())
  {
    throw exception_t{std::format("arude::config: '{}' names no file.", key(uri)), errors::invalid_uri};
  }

  return path;
}

///
///
template<configuration_c T, endpoint_c E>
auto config_manager::parse(const std::string& text, const std::string& source, const load_policy policy) -> T
{
  auto version = version_t{0};

  try
  {
    version = E::probe_version(text);
  }
  catch(...)
  {
    throw exception_t{
      std::format("arude::config: '{}' declares no configuration version: {}", source, exception_report()),
      errors::invalid_payload};
  }

  if(version == version_of<T>())
  {
    try
    {
      return E::template read<T>(text);
    }
    catch(...)
    {
      throw exception_t{
        std::format("arude::config: '{}' is not a {}: {}", source, type_name<T>(), exception_report()),
        errors::invalid_payload};
    }
  }

  if(is_set(policy, load_policy::strict_version) || !is_set(policy, load_policy::upgrade_to_current))
  {
    throw exception_t{
      std::format("arude::config: '{}' is version {}, and version {} was asked for.", source, version, version_of<T>()),
      errors::invalid_version};
  }

  try
  {
    return E::template read_migrated<T>(text);
  }
  catch(...)
  {
    throw exception_t{
      std::format(
        "arude::config: '{}' cannot be migrated to version {}: {}", source, version_of<T>(), exception_report()),
      errors::invalid_version};
  }
}

} // namespace arude::config
