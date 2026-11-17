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
/// Opt-in, and the only part of the configuration support that is: configure
/// with -DARUDE_HTTPS=ON. Everything else here is fetched by CPM, but TLS is
/// not — cpp-httplib needs OpenSSL, mbedTLS or wolfSSL, and the build has to
/// find one of them. A consumer that wants configuration from a file, or from
/// an EEPROM of its own, should not be made to carry that, which is why this is
/// off unless it is asked for.
///
/// The URL is split into an origin and a request target at construction, so
/// the transport carries both halves rather than parsing per call. Only
/// "https://" URLs are accepted: plaintext HTTP for the file that decides what
/// an application trusts is a footgun. A URL carrying userinfo is refused —
/// there is no way to carry a password safely here, and it would be quoted by
/// location() and by every error message; use httplib::Client::set_basic_auth
/// if that is ever needed.
///
/// Redirects are followed here rather than by httplib, which would follow one
/// from https to http with a plaintext client, and which counts hops against a
/// limit of its own. A read follows at most transport_https_options::
/// redirect_limit of them, and only to https; a write follows none.
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

#include <httplib.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <optional>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
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

namespace arude::config::detail
{

///
/// An https URL taken apart into the two halves httplib wants.
///
struct https_url
{
  ///
  /// Scheme and authority, as in "https://example.com:8443": what httplib::Client is constructed from.
  ///
  std::string origin;

  ///
  /// Path and query, without the fragment, and "/" where the URL has no path: what a request asks for.
  ///
  std::string target;
};

///
/// Why a URL could not be taken apart as an https URL.
///
enum class https_url_error : std::uint8_t
{
  ///
  /// The URL does not begin with https://.
  ///
  not_https,

  ///
  /// The authority carries userinfo, which the transport refuses.
  ///
  userinfo,

  ///
  /// The authority has no host, a port outside 1 to 65535, or an IPv6 literal of other than hex digits and colons.
  ///
  malformed_authority
};

///
/// Describes why a URL was refused, as the end of a sentence about it.
/// \param error What was wrong with it.
/// \return A clause such as "is not an https URL".
///
[[nodiscard]] constexpr auto describe(https_url_error error) -> std::string_view;

///
/// Reports whether an HTTP status is a redirect a GET follows.
/// 300 and 304 are left out: neither names somewhere else to go.
///
/// \param status HTTP status code.
/// \return true for 301, 302, 303, 307 and 308.
///
[[nodiscard]] constexpr auto is_redirect(int status) -> bool;

///
/// Returns the scheme a URI reference begins with, as RFC 3986 section 3.1 spells one.
/// \param reference URI reference, absolute or relative. Not retained.
/// \return The scheme, without its colon, or empty for a relative reference. Views into reference.
///
[[nodiscard]] constexpr auto scheme_of(std::string_view reference) -> std::string_view;

///
/// Reports whether an authority is a host with an optional port, in a form httplib can connect to.
/// \param authority Authority without userinfo, as in "example.com:8443" or "[::1]". Not retained.
/// \return true if the host is not empty, an IPv6 literal holds only hex digits and colons, and the port, if any,
///         is a number from 1 to 65535.
///
[[nodiscard]] constexpr auto is_authority(std::string_view authority) -> bool;

///
/// Takes an https URL apart into its origin and its request target.
///
/// \param url URL to take apart. Not retained.
/// \return The two halves, or why the URL is not one the transport takes.
///
[[nodiscard]] auto split_https_url(std::string_view url) -> std::expected<https_url, https_url_error>;

///
/// Resolves the Location of a redirect against the URL that answered with it.
/// An absolute reference must be https; a network-path reference ("//host/x")
/// keeps the https scheme; anything else keeps the origin and is resolved
/// against the target by RFC 3986 section 5.2, without removing dot segments,
/// which the server is left to do.
///
/// \param base URL that answered with the redirect.
/// \param location Location header of the answer. Not retained.
/// \return Where the redirect leads, or why it is not somewhere the transport goes.
///
[[nodiscard]] auto resolve_redirect(const https_url& base, std::string_view location)
  -> std::expected<https_url, https_url_error>;

///
/// Returns a refused URL with anything that might be userinfo removed, so an error message can quote it without
/// quoting a password.
/// A URL that was refused cannot be trusted to mark where its authority ends:
/// a leading space hides the scheme, and an unencoded '/', '?' or '#' in a
/// password ends the authority early. So this errs on removing too much, and
/// everything before the last '@' goes but a scheme and the "//" after it.
///
/// \param url URL or URI reference. Not retained.
/// \return The URL, unchanged if it has no '@'.
///
[[nodiscard]] auto without_userinfo(std::string_view url) -> std::string;

///
///
constexpr auto describe(const https_url_error error) -> std::string_view
{
  switch(error)
  {
  case https_url_error::not_https: return "is not an https URL";
  case https_url_error::userinfo: return "carries userinfo, which is refused and left out of this message";
  case https_url_error::malformed_authority:
    return "has no host, a port outside 1 to 65535, or an IPv6 literal of other than hex digits and colons";
  }

  return "is not a URL the https transport can use";
}

///
///
constexpr auto is_redirect(const int status) -> bool
{
  return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

///
///
constexpr auto scheme_of(const std::string_view reference) -> std::string_view
{
  // A relative reference cannot have a ':' in its first segment, so a colon
  // before any '/', '?' or '#' ends a scheme or nothing at all.
  const auto colon = reference.find_first_of(":/?#");

  if(colon == std::string_view::npos || colon == 0 || reference.at(colon) != ':')
  {
    return {};
  }

  const auto scheme = reference.substr(0, colon);
  const auto is_alpha = [](const char c) -> bool { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); };
  const auto is_scheme_char = [&is_alpha](const char c) -> bool
  { return is_alpha(c) || (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.'; };

  if(!is_alpha(scheme.front()) || !std::ranges::all_of(scheme, is_scheme_char))
  {
    return {};
  }

  return scheme;
}

///
///
constexpr auto is_authority(const std::string_view authority) -> bool
{
  auto host = authority;
  auto port = std::string_view{};

  if(authority.starts_with('['))
  {
    const auto close = authority.find(']');

    if(close == std::string_view::npos)
    {
      return false;
    }

    host = authority.substr(1, close - 1);
    const auto rest = authority.substr(close + 1);

    if(!rest.empty() && !rest.starts_with(':'))
    {
      return false;
    }

    port = rest.substr(std::min<std::size_t>(1, rest.size()));

    // httplib takes an IPv6 literal of hex digits and colons only, and makes
    // a plaintext client for anything it cannot parse.
    const auto is_literal_char = [](const char c) -> bool
    { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') || c == ':'; };

    if(!std::ranges::all_of(host, is_literal_char))
    {
      return false;
    }
  }
  else if(const auto colon = authority.find(':'); colon != std::string_view::npos)
  {
    host = authority.substr(0, colon);
    port = authority.substr(colon + 1);
  }

  if(host.empty())
  {
    return false;
  }

  // An empty port, as in "example.com:", is the default one.
  if(port.empty())
  {
    return true;
  }

  auto number = 0;
  const auto* const end = port.data() + port.size();
  // NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage): the end goes with it, so nothing past the view is read.
  const auto [last, error] = std::from_chars(port.data(), end, number);

  return error == std::errc{} && last == end && number >= 1 && number <= 65535;
}

///
///
inline auto split_https_url(const std::string_view url) -> std::expected<https_url, https_url_error>
{
  static constexpr auto scheme = std::string_view{"https://"};

  if(!url.starts_with(scheme))
  {
    return std::unexpected{https_url_error::not_https};
  }

  // The authority ends at the first '/', '?' or '#' (RFC 3986 section 3.2),
  // none of which can appear in a host, a port or an IPv6 literal, so no
  // fuller parser is needed. httplib::Client splits host from port itself.
  const auto end = std::min(url.find_first_of("/?#", scheme.size()), url.size());
  const auto authority = url.substr(scheme.size(), end - scheme.size());

  if(authority.contains('@'))
  {
    return std::unexpected{https_url_error::userinfo};
  }

  if(!is_authority(authority))
  {
    return std::unexpected{https_url_error::malformed_authority};
  }

  auto target = std::string{url.substr(end)};

  // HTTP requests do not send a fragment, and httplib would put it on the wire
  // as part of the target.
  target.erase(std::min(target.find('#'), target.size()));

  if(!target.starts_with('/'))
  {
    target.insert(0, 1, '/');
  }

  return https_url{.origin = std::string{url.substr(0, end)}, .target = std::move(target)};
}

///
///
inline auto resolve_redirect(const https_url& base, const std::string_view location)
  -> std::expected<https_url, https_url_error>
{
  if(const auto scheme = scheme_of(location); !scheme.empty())
  {
    // A scheme is case-insensitive, and split_https_url wants it lower case.
    const auto lower = [](const char c) -> char { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };

    if(!std::ranges::equal(scheme, std::string_view{"https"}, {}, lower))
    {
      return std::unexpected{https_url_error::not_https};
    }

    return split_https_url(std::format("https{}", location.substr(scheme.size())));
  }

  if(location.starts_with("//"))
  {
    return split_https_url(std::format("https:{}", location));
  }

  const auto path = std::string_view{base.target}.substr(0, base.target.find('?'));
  auto target = std::string{};

  if(location.starts_with('/'))
  {
    target = location;
  }
  else if(location.empty() || location.starts_with('#'))
  {
    target = base.target;
  }
  else if(location.starts_with('?'))
  {
    target = std::format("{}{}", path, location);
  }
  else
  {
    // A target always begins with '/', so there is a directory to resolve against.
    target = std::format("{}{}", path.substr(0, path.rfind('/') + 1), location);
  }

  target.erase(std::min(target.find('#'), target.size()));

  return https_url{.origin = base.origin, .target = std::move(target)};
}

///
///
inline auto without_userinfo(const std::string_view url) -> std::string
{
  const auto at = url.rfind('@');

  if(at == std::string_view::npos)
  {
    return std::string{url};
  }

  // A scheme holds no '@' and no ':', so keeping it cannot keep a password.
  const auto scheme = scheme_of(url);
  const auto has_authority = !scheme.empty() && url.substr(scheme.size() + 1).starts_with("//");
  const auto kept = has_authority ? scheme.size() + 3 : std::size_t{0};

  return std::format("{}{}", url.substr(0, kept), url.substr(at + 1));
}

} // namespace arude::config::detail

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
  /// Applies to each request, so a read that follows redirects may take longer in all.
  ///
  std::chrono::seconds timeout = std::chrono::seconds{30};

  ///
  /// How many redirects a read follows before giving up. Zero follows none.
  /// A redirect beyond the limit is an io_error, as is one to anywhere but an
  /// https URL: the refusal of plaintext would mean nothing if the server could
  /// send a read elsewhere over http. A write follows no redirect whatever the
  /// limit, since sending the bytes on to an address the server names is a
  /// write the caller did not ask for.
  ///
  std::size_t redirect_limit = 5;

  ///
  /// A file of CA certificates to verify against, or empty for the system store.
  /// Verification is not optional here: a configuration is the thing that says
  /// where an application connects and what it trusts, so accepting an
  /// unverified one defeats the point of fetching it over TLS. The same
  /// certificates verify every host a redirect leads to.
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
  /// Constructs a transport for the URL given, with the options given.
  /// One constructor rather than a URL-only overload beside it, so that
  /// transport_https{url, {}} names the default options rather than being
  /// ambiguous with the location.
  ///
  /// \param url URL the configuration lives at. Must begin with https:// and carry no userinfo.
  /// \param options How it should behave. Copied. Defaults to transport_https_options{}.
  /// \param loc Where the call was made, which is what a failure reports. Left to its default.
  /// \throws exception_t With invalid_location if the URL does not begin with
  ///                     https://, carries userinfo, or has no host, a port
  ///                     outside 1 to 65535, or a malformed IPv6 literal. The
  ///                     message leaves anything that might be userinfo out.
  ///
  explicit transport_https(
    std::string_view url,
    transport_https_options options = {},
    std::source_location loc = std::source_location::current());

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
  /// Reads the bytes at the URL, with one GET, and one more per redirect followed.
  /// A redirect (301, 302, 303, 307 or 308) is followed to the https URL its
  /// Location names, resolved against the URL that answered, up to
  /// transport_https_options::redirect_limit of them.
  ///
  /// \param loc Where the call was made, which is what a failure reports. Left to its default.
  /// \return The bytes, exactly as served, or std::nullopt where the server
  ///         answers 404 or 410, which is the source that load_policy::create
  ///         gives defaults for.
  /// \throws exception_t With io_error if the URL cannot be reached, if the
  ///                     server answers with any other status outside 2xx, or
  ///                     if a redirect has no Location, leads anywhere but an
  ///                     https URL, or goes beyond the limit.
  ///
  [[nodiscard]] auto read(std::source_location loc = std::source_location::current()) const
    -> std::optional<std::vector<std::byte>>;

public: // Methods
  ///
  /// Writes bytes to the URL, replacing whatever was there, with one PUT.
  /// A redirect is not followed.
  ///
  /// \param bytes Bytes to write. Not retained.
  /// \param loc Where the call was made, which is what a failure reports. Left to its default.
  /// \throws exception_t With read_only if the transport is read-only, or
  ///                     io_error if the write fails or the server answers
  ///                     with any status outside 2xx, a redirect included.
  ///
  auto write(std::span<const std::byte> bytes, std::source_location loc = std::source_location::current()) const
    -> void;

private: // Helpers
  ///
  /// Makes a client for an origin, with the options applied.
  ///
  /// \param origin Origin to connect to, as detail::https_url carries it.
  /// \param loc The call site a failure reports.
  /// \return The client, not yet connected.
  /// \throws exception_t With io_error if httplib cannot make a client for the origin.
  ///
  [[nodiscard]] auto connect(const std::string& origin, std::source_location loc) const -> httplib::Client;

  ///
  /// Works out where a redirect leads, or refuses to follow it.
  ///
  /// \param from URL that answered with the redirect.
  /// \param response The answer, carrying the Location.
  /// \param followed How many redirects the read has followed before this one.
  /// \param loc The call site a failure reports.
  /// \return Where to send the next request.
  /// \throws exception_t With io_error if the answer has no Location, if it
  ///                     leads anywhere but an https URL, or if following it
  ///                     would exceed the redirect limit.
  ///
  [[nodiscard]] auto redirect(
    const detail::https_url& from,
    const httplib::Response& response,
    std::size_t followed,
    std::source_location loc) const -> detail::https_url;

private: // Variables
  std::string url_;
  detail::https_url parts_;
  transport_https_options options_;
};

///
///
inline transport_https::transport_https(
  const std::string_view url, transport_https_options options, const std::source_location loc)
  : url_{url}
  , options_{std::move(options)}
{
  auto parts = detail::split_https_url(url_);

  if(!parts)
  {
    throw exception_t{
      std::format("arude::config: '{}' {}.", detail::without_userinfo(url_), detail::describe(parts.error())),
      config_error::invalid_location,
      loc};
  }

  parts_ = std::move(*parts);
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
inline auto transport_https::read(const std::source_location loc) const -> std::optional<std::vector<std::byte>>
{
  auto at = parts_;

  for(auto followed = std::size_t{0};; ++followed)
  {
    // Names the URL actually requested, once a redirect has made it differ
    // from the one the transport was constructed with.
    const auto where = [&at, followed, this] -> std::string
    {
      return followed == 0 ? std::format("'{}'", url_)
                           : std::format("'{}', redirected to '{}{}',", url_, at.origin, at.target);
    };

    auto client = connect(at.origin, loc);
    const auto result = client.Get(at.target);

    if(!result)
    {
      throw exception_t{
        std::format("arude::config: {} could not be reached: {}", where(), httplib::to_string(result.error())),
        config_error::io_error,
        loc};
    }

    if(detail::is_redirect(result->status))
    {
      at = redirect(at, *result, followed, loc);
      continue;
    }

    // A 404 is an answer; a 403 or a 500 is not, and reporting either as "there
    // is nothing there" would have load_policy::create hand back defaults for a
    // configuration that is present but momentarily unreadable — and a later
    // store() would then overwrite it with them.
    if(result->status == 404 || result->status == 410)
    {
      return std::nullopt;
    }

    if(result->status < 200 || result->status >= 300)
    {
      throw exception_t{
        std::format("arude::config: {} answered {}.", where(), result->status), config_error::io_error, loc};
    }

    return as_bytes(result->body);
  }
}

///
///
inline auto transport_https::write(const std::span<const std::byte> bytes, const std::source_location loc) const -> void
{
  if(!writable())
  {
    throw exception_t{
      std::format("arude::config: the '{}' transport is read-only.", name), config_error::read_only, loc};
  }

  auto client = connect(parts_.origin, loc);
  const auto result = client.Put(parts_.target, as_text(bytes), options_.content_type);

  if(!result)
  {
    throw exception_t{
      std::format("arude::config: '{}' could not be written: {}", url_, httplib::to_string(result.error())),
      config_error::io_error,
      loc};
  }

  // Not followed: a PUT replayed at an address the server names is a write the
  // caller never asked for, to a place it never named.
  if(result->status >= 300 && result->status < 400)
  {
    throw exception_t{
      std::format(
        "arude::config: '{}' answered {} to the write, and a write does not follow redirects.", url_, result->status),
      config_error::io_error,
      loc};
  }

  if(result->status < 200 || result->status >= 300)
  {
    throw exception_t{
      std::format("arude::config: '{}' answered {} to the write.", url_, result->status), config_error::io_error, loc};
  }
}

///
///
inline auto transport_https::connect(const std::string& origin, const std::source_location loc) const -> httplib::Client
{
  auto client = httplib::Client{origin};

  // split_https_url has vetted the origin already, but httplib makes no client
  // at all for one it cannot parse, and calling into that would crash.
  if(!client.is_valid())
  {
    throw exception_t{
      std::format("arude::config: '{}' could not be reached: httplib cannot connect to '{}'.", url_, origin),
      config_error::io_error,
      loc};
  }

  client.set_connection_timeout(options_.timeout);
  client.set_read_timeout(options_.timeout);

  // read() follows redirects itself, so that it can hold them to https and to
  // redirect_limit; httplib would do neither.
  client.set_follow_location(false);

  if(!options_.ca_certificate_path.empty())
  {
    client.set_ca_cert_path(options_.ca_certificate_path.string());
  }

  return client;
}

///
///
inline auto transport_https::redirect(
  const detail::https_url& from,
  const httplib::Response& response,
  const std::size_t followed,
  const std::source_location loc) const -> detail::https_url
{
  const auto location = response.get_header_value("Location");

  if(location.empty())
  {
    throw exception_t{
      std::format(
        "arude::config: '{}' answered {} from '{}{}' without a Location to redirect to.",
        url_,
        response.status,
        from.origin,
        from.target),
      config_error::io_error,
      loc};
  }

  auto next = detail::resolve_redirect(from, location);

  if(!next)
  {
    throw exception_t{
      std::format(
        "arude::config: '{}' redirected to '{}', which {}.",
        url_,
        detail::without_userinfo(location),
        detail::describe(next.error())),
      config_error::io_error,
      loc};
  }

  // Resolved first, so that what is quoted here has been through
  // split_https_url and carries no userinfo.
  if(followed >= options_.redirect_limit)
  {
    throw exception_t{
      std::format(
        "arude::config: '{}' redirected to '{}{}', beyond the limit of {} redirects.",
        url_,
        next->origin,
        next->target,
        options_.redirect_limit),
      config_error::io_error,
      loc};
  }

  return std::move(*next);
}

} // namespace arude::config

static_assert(arude::config::transport_c<arude::config::transport_https>);
