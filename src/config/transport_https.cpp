///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///

#include "arude/config/transport_https.hpp"
#include "arude/config/common.hpp"

#include <httplib.h>

#include <algorithm>
#include <cstddef>
#include <expected>
#include <format>
#include <optional>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace arude::config::detail
{

///
///
auto split_https_url(const std::string_view url) -> std::expected<https_url, https_url_error>
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
auto resolve_redirect(const https_url& base, const std::string_view location)
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
auto without_userinfo(const std::string_view url) -> std::string
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
///
transport_https::transport_https(
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
auto transport_https::location() const -> std::string
{
  return url_;
}

///
///
auto transport_https::writable() const -> bool
{
  return options_.writable;
}

///
///
auto transport_https::read(const std::source_location loc) const -> std::optional<std::vector<std::byte>>
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
auto transport_https::write(const std::span<const std::byte> bytes, const std::source_location loc) const -> void
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
auto transport_https::connect(const std::string& origin, const std::source_location loc) const -> httplib::Client
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
auto transport_https::redirect(
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
