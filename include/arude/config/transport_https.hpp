///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// The HTTPS transport, over cpp-httplib.
///
/// Opt-in, and the only part of libarude that is: configure with
/// -DARUDE_HTTPS=ON. Everything else here is fetched by CPM, but TLS is not —
/// cpp-httplib needs OpenSSL, mbedTLS or wolfSSL, and the build has to find one
/// of them. A consumer that wants configuration from a file, or from an EEPROM
/// of its own, should not be made to carry that, which is why this is off
/// unless it is asked for.
///
/// The URL is split into an origin and a request target at construction, so
/// the transport carries both halves rather than parsing per call. Only
/// "https://" URLs are accepted: plaintext HTTP for the file that decides what
/// an application trusts is a footgun. Userinfo is not supported — there is no
/// way to carry a password safely here; use httplib::Client::set_basic_auth if
/// that is ever needed.
///
/// Read-only by default. A configuration served over HTTPS is usually one
/// somebody else publishes, so writing it back is the exception rather than
/// the rule; set transport_https_options::writable to allow the PUT.
///
#pragma once

#if !(defined ARUDE_HTTPS)
  #error "arude/config/transport_https.hpp needs the HTTPS transport. Configure with -DARUDE_HTTPS=ON."
#endif // #if !(defined ARUDE_HTTPS)

#include "arude/config/common.hpp"
#include "arude/config/transport.hpp"
#include "arude/exception.hpp"

#include <httplib.h>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// cpp-httplib speaks TLS only when it was compiled against a backend, and the
// define is what says which. Without one, httplib::Client accepts an https URL
// and fails at run time; catching it here says what to do about it instead.
#if !(defined CPPHTTPLIB_OPENSSL_SUPPORT) &&                                                                           \
  !(defined CPPHTTPLIB_MBEDTLS_SUPPORT) &&                                                                             \
  !(defined CPPHTTPLIB_WOLFSSL_SUPPORT)
  #error "The HTTPS transport needs a cpp-httplib TLS backend: OpenSSL, mbedTLS or wolfSSL."
#endif // #if !(defined CPPHTTPLIB_OPENSSL_SUPPORT) && ...

namespace arude::config
{

///
/// How the HTTPS transport should behave. Passive, so an application sets what
/// it cares about and leaves the rest.
///
struct transport_https_options
{
  ///
  /// How long to wait for a connection, and then for a response.
  ///
  std::chrono::seconds timeout = std::chrono::seconds{30};

  ///
  /// How many redirects to follow before giving up. Zero follows none.
  ///
  std::size_t redirect_limit = 5;

  ///
  /// A file of CA certificates to verify against, or empty for the system store.
  /// Verification is not optional here: a configuration is the thing that says
  /// where an application connects and what it trusts, so accepting an
  /// unverified one defeats the point of fetching it over TLS.
  ///
  std::filesystem::path ca_certificate_path;

  ///
  /// What a write announces the body as.
  /// A transport does not know what format it is carrying, so this is a
  /// setting rather than something derived from the endpoint.
  ///
  std::string content_type = "text/plain; charset=utf-8";

  ///
  /// Whether a write is allowed at all. False means store() is refused.
  ///
  bool writable = false;
};

///
/// Reads, and optionally writes, a configuration over HTTPS.
/// Owns the URL it was constructed with and splits it once, into the origin
/// httplib connects to and the target it requests.
///
class transport_https final
{
public: // Structors
  ///
  /// Constructs a transport for the URL given, with the default options.
  ///
  /// \param url URL the configuration lives at. Must begin with https:// and carry no userinfo.
  /// \throws exception_t With invalid_location if the URL does not begin with https://.
  ///
  explicit transport_https(std::string_view url);

  ///
  /// Constructs a transport for the URL given, with the options given.
  ///
  /// \param url URL the configuration lives at. Must begin with https:// and carry no userinfo.
  /// \param options How it should behave. Copied.
  /// \throws exception_t With invalid_location if the URL does not begin with https://.
  ///
  transport_https(std::string_view url, transport_https_options options);

public: // Accessors
  ///
  /// The name, as a plain label for diagnostics.
  ///
  static constexpr auto name = std::string_view{"https"};

  ///
  /// Returns the URL this transport was constructed with.
  /// \return The URL, exactly as given, which is what the manager keys on and what errors quote.
  ///
  [[nodiscard]] auto location() const -> std::string;

  ///
  /// Reports whether this transport can be written as well as read.
  /// \return true if a write will be attempted rather than refused.
  ///
  [[nodiscard]] auto writable() const -> bool;

  ///
  /// Reports whether anything is at the URL.
  /// Answering false is what lets load_policy::create know there is a
  /// configuration to write out.
  ///
  /// \return true if reading it now would find something.
  /// \throws exception_t With io_error if the URL cannot be reached.
  ///
  [[nodiscard]] auto exists() const -> bool;

  ///
  /// Reads the bytes at the URL.
  ///
  /// \return The bytes, exactly as served.
  /// \throws exception_t With io_error if the URL cannot be reached or read.
  ///
  [[nodiscard]] auto read() const -> std::vector<std::byte>;

public: // Methods
  ///
  /// Writes bytes to the URL, replacing whatever was there.
  ///
  /// \param bytes Bytes to write. Not retained.
  /// \throws exception_t With read_only if the transport is read-only, or io_error if the write fails.
  ///
  auto write(std::span<const std::byte> bytes) const -> void;

private: // Helpers
  ///
  /// Applies the options to a client.
  /// \param client Client to configure.
  ///
  auto configure(httplib::Client& client) const -> void;

private: // Variables
  std::string url_;
  std::string origin_;
  std::string target_;
  transport_https_options options_;
};

///
///
inline transport_https::transport_https(const std::string_view url)
  : transport_https{url, transport_https_options{}}
{
}

///
///
inline transport_https::transport_https(const std::string_view url, transport_https_options options)
  : url_{url}
  , options_{std::move(options)}
{
  constexpr auto scheme = std::string_view{"https://"};

  if(!url_.starts_with(scheme))
  {
    throw exception_t{std::format("arude::config: '{}' is not an https URL.", url_), config_error::invalid_location};
  }

  // The split is safe without a URL parser: neither an IPv6 literal nor
  // userinfo contains a '/', so the first '/' after the scheme always begins
  // the path. httplib::Client does the host/port splitting itself.
  const auto path_start = url_.find('/', scheme.size());

  origin_ = path_start == std::string::npos ? url_ : url_.substr(0, path_start);
  target_ = path_start == std::string::npos ? "/" : url_.substr(path_start);

  // HTTP requests do not send a fragment, and httplib would put it on the wire
  // as part of the target.
  if(const auto fragment = target_.find('#'); fragment != std::string::npos)
  {
    target_.erase(fragment);
  }
}

///
///
inline auto transport_https::location() const -> std::string
{
  return url_;
}

///
///
inline auto transport_https::writable() const -> bool
{
  return options_.writable;
}

///
///
inline auto transport_https::configure(httplib::Client& client) const -> void
{
  client.set_connection_timeout(options_.timeout);
  client.set_read_timeout(options_.timeout);
  client.set_follow_location(options_.redirect_limit != 0);

  if(!options_.ca_certificate_path.empty())
  {
    client.set_ca_cert_path(options_.ca_certificate_path.string());
  }
}

///
///
inline auto transport_https::exists() const -> bool
{
  auto client = httplib::Client{origin_};
  configure(client);

  const auto result = client.Head(target_);

  if(!result)
  {
    throw exception_t{
      std::format("arude::config: '{}' could not be reached: {}", url_, httplib::to_string(result.error())),
      config_error::io_error};
  }

  // A 404 is an answer; a 403 or a 500 is not, and reporting either as "there
  // is nothing there" would have load_policy::create overwrite a configuration
  // that is present but momentarily unreadable.
  if(result->status == 404 || result->status == 410)
  {
    return false;
  }

  if(result->status < 200 || result->status >= 300)
  {
    throw exception_t{std::format("arude::config: '{}' answered {}.", url_, result->status), config_error::io_error};
  }

  return true;
}

///
///
inline auto transport_https::read() const -> std::vector<std::byte>
{
  auto client = httplib::Client{origin_};
  configure(client);

  const auto result = client.Get(target_);

  if(!result)
  {
    throw exception_t{
      std::format("arude::config: '{}' could not be read: {}", url_, httplib::to_string(result.error())),
      config_error::io_error};
  }

  if(result->status < 200 || result->status >= 300)
  {
    throw exception_t{std::format("arude::config: '{}' answered {}.", url_, result->status), config_error::io_error};
  }

  return as_bytes(result->body);
}

///
///
inline auto transport_https::write(const std::span<const std::byte> bytes) const -> void
{
  if(!writable())
  {
    throw exception_t{std::format("arude::config: the '{}' transport is read-only.", name), config_error::read_only};
  }

  auto client = httplib::Client{origin_};
  configure(client);

  const auto result = client.Put(target_, as_text(bytes), options_.content_type);

  if(!result)
  {
    throw exception_t{
      std::format("arude::config: '{}' could not be written: {}", url_, httplib::to_string(result.error())),
      config_error::io_error};
  }

  if(result->status < 200 || result->status >= 300)
  {
    throw exception_t{
      std::format("arude::config: '{}' answered {} to the write.", url_, result->status), config_error::io_error};
  }
}

} // namespace arude::config

static_assert(arude::config::transport_c<arude::config::transport_https>);
