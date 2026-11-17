///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// Built only with -DARUDE_HTTPS=ON and cpp-httplib on its OpenSSL backend,
/// whose API makes the certificate. The servers run on 127.0.0.1, and the
/// certificate they present is made here at test time, so nothing secret and
/// nothing that expires is checked in.
///

#include "arude/config.hpp"
#include "arude/exception.hpp"
#include "arude/exception_report.hpp"
#include "test/helpers/temp_directory.hpp"
#include "test/helpers/write_text.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <httplib.h>
#include <openssl/asn1.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <chrono>
#include <concepts>
#include <cstdio>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

namespace transport_https_test
{
namespace
{

///
/// Frees an OpenSSL object with the function OpenSSL provides for it, so a std::unique_ptr can own one.
/// \tparam Free Function that frees one.
///
template<auto Free>
struct openssl_free
{
  ///
  /// Frees the object.
  /// \param object Object to free.
  ///
  auto operator()(auto* object) const -> void;
};

///
/// An OpenSSL object, owned.
/// \tparam T Object type.
/// \tparam Free Function that frees one.
///
template<typename T, auto Free>
using openssl_ptr_t = std::unique_ptr<T, openssl_free<Free>>;

///
/// A self-signed certificate and its private key, in PEM.
///
struct certificate
{
  ///
  /// The certificate, which is also the CA a client verifies it against.
  ///
  std::string certificate_pem;

  ///
  /// The private key, unencrypted.
  ///
  std::string key_pem;
};

///
/// What a test server saw of one request.
///
struct seen_request
{
  ///
  /// The method, as in "GET".
  ///
  std::string method;

  ///
  /// The request target exactly as it came on the wire, query included.
  ///
  std::string target;

  ///
  /// The body.
  ///
  std::string body;

  ///
  /// The Content-Type header, or empty.
  ///
  std::string content_type;
};

///
/// An httplib server on 127.0.0.1, on a port of its own, run on a thread of its own.
/// Routes are added through server() before start(). Every request it answers
/// is recorded, body included, before the answer goes out: once a client has
/// its response, requests() has the request.
///
class test_server final
{
public: // Structors
  ///
  /// Takes the server to run.
  ///
  /// \param server Server, with or without TLS, not yet listening.
  /// \param scheme What origin() begins with: "https" or "http".
  ///
  test_server(std::unique_ptr<httplib::Server> server, std::string scheme);

  ///
  /// Stops the server and waits for its thread.
  ///
  ~test_server();

  test_server(const test_server&) = delete;
  test_server(test_server&&) = delete;
  auto operator=(const test_server&) -> test_server& = delete;
  auto operator=(test_server&&) -> test_server& = delete;

public: // Accessors
  ///
  /// Returns the server, to add routes to before start().
  /// \return The server.
  ///
  [[nodiscard]] auto server() -> httplib::Server&;

  ///
  /// Returns the scheme, host and port the server listens on.
  /// \return The origin, as in "https://127.0.0.1:40123". Only meaningful after start().
  ///
  [[nodiscard]] auto origin() const -> std::string;

  ///
  /// Returns a URL on this server.
  /// \param target Path, beginning with '/'. Not retained.
  /// \return The URL.
  ///
  [[nodiscard]] auto url(std::string_view target) const -> std::string;

  ///
  /// Returns every request the server has been sent, in order.
  /// \return A copy, taken under the lock the server thread records under.
  ///
  [[nodiscard]] auto requests() const -> std::vector<seen_request>;

public: // Methods
  ///
  /// Binds to a free port on 127.0.0.1 and serves on a thread, returning once the server is ready.
  /// \throws arude::exception If no port can be bound.
  ///
  auto start() -> void;

private: // Variables
  std::unique_ptr<httplib::Server> server_;
  std::string scheme_;
  int port_ = 0;
  mutable std::mutex mutex_;
  std::vector<seen_request> requests_;
  std::thread thread_;
};

///
/// Creates a self-signed certificate for 127.0.0.1, valid from an hour ago until a day from now.
/// \return The certificate and its key.
/// \throws arude::exception If OpenSSL refuses any step.
///
auto make_certificate() -> certificate;

///
/// Creates a TLS server presenting a certificate.
/// \param identity Certificate and key to present. Not retained.
/// \return The server, not yet listening.
/// \throws arude::exception If httplib cannot set up TLS with them.
///
auto make_https_server(const certificate& identity) -> std::unique_ptr<httplib::Server>;

///
/// Writes a certificate where a transport can be pointed at it, and returns options that trust it.
///
/// \param directory Directory to write the file in.
/// \param name File name to use.
/// \param authority Certificate to trust.
/// \return Options with a short timeout and the CA set to that file.
///
auto options_trusting(
  const test_helpers::temp_directory& directory, std::string_view name, const certificate& authority)
  -> arude::config::transport_https_options;

///
/// Adds the routes the scenarios below request.
///
/// \param server Server to add them to, not yet started.
/// \param plain Origin of a plaintext server, for a redirect that must not be followed. Not retained.
/// \param other Origin of a second TLS server, for a redirect that must be. Not retained.
///
auto add_routes(httplib::Server& server, std::string_view plain, std::string_view other) -> void;

///
/// Runs an action and returns what it threw.
/// \param action Action to run.
/// \return The exception, or std::nullopt if it threw none.
///
auto failure_of(const std::invocable auto& action) -> std::optional<arude::config::exception_t>;

///
/// Returns the targets of the requests a server saw, in order.
/// \param server Server to ask.
/// \return The targets.
///
auto targets_seen_by(const test_server& server) -> std::vector<std::string>;

///
///
template<auto Free>
auto openssl_free<Free>::operator()(auto* const object) const -> void
{
  Free(object);
}

///
///
test_server::test_server(std::unique_ptr<httplib::Server> server, std::string scheme)
  : server_{std::move(server)}
  , scheme_{std::move(scheme)}
{
  // After routing rather than before it, which is the earliest point httplib
  // has read the body by.
  server_->set_post_routing_handler(
    [this](const httplib::Request& request, [[maybe_unused]] httplib::Response& response) -> void
    {
      const auto lock = std::scoped_lock{mutex_};
      requests_.push_back(
        {.method = request.method,
         .target = request.target,
         .body = request.body,
         .content_type = request.get_header_value("Content-Type")});
    });
}

///
///
test_server::~test_server()
{
  // A destructor throws nothing, and a server thread that will not join is
  // not worth ending the whole test run over. exception_report() allocates and
  // so could throw in turn, which leaves a fixed message as the last resort.
  try
  {
    if(thread_.joinable())
    {
      server_->stop();
      thread_.join();
    }
  }
  catch(...)
  {
    try
    {
      const auto report = arude::exception_report();
      std::fputs(report.c_str(), stderr);
    }
    catch(...)
    {
      std::fputs("The test server did not stop cleanly.\n", stderr);
    }
  }
}

///
///
auto test_server::server() -> httplib::Server&
{
  return *server_;
}

///
///
auto test_server::origin() const -> std::string
{
  return std::format("{}://127.0.0.1:{}", scheme_, port_);
}

///
///
auto test_server::url(const std::string_view target) const -> std::string
{
  return std::format("{}{}", origin(), target);
}

///
///
auto test_server::requests() const -> std::vector<seen_request>
{
  const auto lock = std::scoped_lock{mutex_};
  return requests_;
}

///
///
auto test_server::start() -> void
{
  port_ = server_->bind_to_any_port("127.0.0.1");

  if(port_ < 0)
  {
    throw arude::exception{"The test server could not bind to a port on 127.0.0.1."};
  }

  thread_ = std::thread{[this] -> void { server_->listen_after_bind(); }};
  server_->wait_until_ready();
}

///
///
auto make_certificate() -> certificate
{
  const auto require = [](const bool done, const std::string_view step) -> void
  {
    if(!done)
    {
      throw arude::exception{std::format("OpenSSL could not {}.", step)};
    }
  };

  // Read whole by the size it reports, so no PEM is ever cut short.
  const auto drain = [&require](BIO* const bio) -> std::string
  {
    auto text = std::string(BIO_ctrl_pending(bio), '\0');
    require(BIO_read(bio, text.data(), static_cast<int>(text.size())) == static_cast<int>(text.size()), "read a PEM");
    return text;
  };

  const auto context =
    openssl_ptr_t<EVP_PKEY_CTX, EVP_PKEY_CTX_free>{EVP_PKEY_CTX_new_from_name(nullptr, "EC", nullptr)};
  require(context != nullptr, "make a key context");
  require(EVP_PKEY_keygen_init(context.get()) == 1, "start making a key");
  require(EVP_PKEY_CTX_set_group_name(context.get(), "P-256") == 1, "choose the curve");

  EVP_PKEY* raw_key = nullptr;
  require(EVP_PKEY_generate(context.get(), &raw_key) == 1, "make a key");
  const auto key = openssl_ptr_t<EVP_PKEY, EVP_PKEY_free>{raw_key};

  const auto cert = openssl_ptr_t<X509, X509_free>{X509_new()};
  require(cert != nullptr, "make a certificate");
  require(X509_set_version(cert.get(), X509_VERSION_3) == 1, "set the version");

  // Two certificates with one subject must not share a serial too, or the one
  // that is not trusted could pass for the one that is in anything but its key.
  auto random = std::random_device{};
  require(ASN1_INTEGER_set(X509_get_serialNumber(cert.get()), static_cast<long>(random() >> 1U)) == 1, "set a serial");

  require(X509_gmtime_adj(X509_getm_notBefore(cert.get()), -3600) != nullptr, "set the start of validity");
  require(X509_gmtime_adj(X509_getm_notAfter(cert.get()), 86400) != nullptr, "set the end of validity");
  require(X509_set_pubkey(cert.get(), key.get()) == 1, "set the public key");

  static constexpr auto common_name = std::string_view{"127.0.0.1"};
  auto* const subject = X509_get_subject_name(cert.get());
  require(
    X509_NAME_add_entry_by_txt(
      subject,
      "CN",
      MBSTRING_ASC,
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): char and unsigned char may alias each other.
      reinterpret_cast<const unsigned char*>(common_name.data()),
      static_cast<int>(common_name.size()),
      -1,
      0) == 1,
    "set the subject");
  require(X509_set_issuer_name(cert.get(), subject) == 1, "set the issuer");

  // Hostname verification matches an IP address only against an IP entry in
  // the subject alternative name, never against the common name.
  auto extension_context = X509V3_CTX{};
  X509V3_set_ctx(&extension_context, cert.get(), cert.get(), nullptr, nullptr, 0);
  const auto alternative_name = openssl_ptr_t<X509_EXTENSION, X509_EXTENSION_free>{
    X509V3_EXT_conf_nid(nullptr, &extension_context, NID_subject_alt_name, "IP:127.0.0.1")};
  require(alternative_name != nullptr, "make the subject alternative name");
  require(X509_add_ext(cert.get(), alternative_name.get(), -1) == 1, "add the subject alternative name");

  require(X509_sign(cert.get(), key.get(), EVP_sha256()) > 0, "sign the certificate");

  const auto certificate_bio = openssl_ptr_t<BIO, BIO_free_all>{BIO_new(BIO_s_mem())};
  const auto key_bio = openssl_ptr_t<BIO, BIO_free_all>{BIO_new(BIO_s_mem())};
  require(certificate_bio != nullptr && key_bio != nullptr, "make a memory buffer");
  require(PEM_write_bio_X509(certificate_bio.get(), cert.get()) == 1, "write the certificate");
  require(
    PEM_write_bio_PrivateKey(key_bio.get(), key.get(), nullptr, nullptr, 0, nullptr, nullptr) == 1, "write the key");

  return {.certificate_pem = drain(certificate_bio.get()), .key_pem = drain(key_bio.get())};
}

///
///
auto make_https_server(const certificate& identity) -> std::unique_ptr<httplib::Server>
{
  auto server = std::make_unique<httplib::SSLServer>(httplib::SSLServer::PemMemory{
    .cert_pem = identity.certificate_pem.c_str(),
    .cert_pem_len = identity.certificate_pem.size(),
    .key_pem = identity.key_pem.c_str(),
    .key_pem_len = identity.key_pem.size(),
    .client_ca_pem = nullptr,
    .client_ca_pem_len = 0,
    .private_key_password = nullptr});

  if(!server->is_valid())
  {
    throw arude::exception{"httplib could not set up TLS with the test certificate."};
  }

  return server;
}

///
///
auto options_trusting(
  const test_helpers::temp_directory& directory, const std::string_view name, const certificate& authority)
  -> arude::config::transport_https_options
{
  const auto path = directory.file(name);
  test_helpers::write_text(path, authority.certificate_pem);

  // Short, so a test that goes wrong fails rather than hangs.
  return {.timeout = std::chrono::seconds{5}, .ca_certificate_path = path};
}

///
///
auto add_routes(httplib::Server& server, const std::string_view plain, const std::string_view other) -> void
{
  const auto answer = [](const int status) -> httplib::Server::Handler
  {
    return [status]([[maybe_unused]] const httplib::Request& request, httplib::Response& response) -> void
    { response.status = status; };
  };

  const auto redirect = [](const int status, std::string location) -> httplib::Server::Handler
  {
    return [status, location = std::move(location)](
             [[maybe_unused]] const httplib::Request& request, httplib::Response& response) -> void
    {
      response.status = status;
      response.set_header("Location", location);
    };
  };

  server.Get(
    "/app.toml",
    []([[maybe_unused]] const httplib::Request& request, httplib::Response& response) -> void
    { response.set_content("contents\n", "text/plain"); });
  server.Get(
    "/dir/here.toml",
    []([[maybe_unused]] const httplib::Request& request, httplib::Response& response) -> void
    { response.set_content("relative\n", "text/plain"); });
  server.Get("/missing", answer(404));
  server.Get("/gone", answer(410));
  server.Get("/forbidden", answer(403));
  server.Get("/broken", answer(500));

  server.Get("/moved", redirect(302, "/app.toml"));
  server.Get("/dir/moved", redirect(301, "here.toml"));
  server.Get("/elsewhere", redirect(307, std::format("{}/app.toml", other)));
  server.Get("/to-plain", redirect(302, std::format("{}/app.toml", plain)));
  // The other origin, with userinfo spliced in after its "https://".
  server.Get("/to-userinfo", redirect(302, std::format("https://user:secret@{}/app.toml", other.substr(8))));
  // The same, with an unencoded '/' in the password ending the authority early.
  server.Get("/to-slashed-userinfo", redirect(302, std::format("https://user:secret/x@{}/app.toml", other.substr(8))));
  server.Get("/loop", redirect(302, "/loop"));
  server.Get("/nowhere", answer(302));

  server.Put("/app.toml", answer(204));
  server.Put("/moved", redirect(307, "/app.toml"));
}

///
///
auto failure_of(const std::invocable auto& action) -> std::optional<arude::config::exception_t>
{
  try
  {
    action();
  }
  catch(const arude::config::exception_t& ex)
  {
    return ex;
  }

  return std::nullopt;
}

///
///
auto targets_seen_by(const test_server& server) -> std::vector<std::string>
{
  auto targets = std::vector<std::string>{};

  for(const auto& request : server.requests())
  {
    targets.push_back(request.target);
  }

  return targets;
}

} // namespace

} // namespace transport_https_test

// The status codes are the contract: only 404 and 410 say that nothing is
// there, which the create policy acts on, and everything else outside 2xx is
// a failure that must never be mistaken for one.
SCENARIO("an https transport reads what the server answers", "[config][transport][transport_https]")
{
  using arude::config::config_error;
  using arude::config::transport_https;

  const auto identity = transport_https_test::make_certificate();
  const auto directory = test_helpers::temp_directory{"arude_transport_https_read"};
  const auto options = transport_https_test::options_trusting(directory, "ca.pem", identity);

  auto plain = transport_https_test::test_server{std::make_unique<httplib::Server>(), "http"};
  plain.start();
  auto other = transport_https_test::test_server{transport_https_test::make_https_server(identity), "https"};
  other.start();
  auto server = transport_https_test::test_server{transport_https_test::make_https_server(identity), "https"};
  transport_https_test::add_routes(server.server(), plain.origin(), other.origin());
  server.start();

  GIVEN("a URL the server answers with 200")
  {
    THEN("its bytes are read")
    {
      const auto bytes = transport_https{server.url("/app.toml"), options}.read();

      REQUIRE(bytes.has_value());
      // NOLINTNEXTLINE(bugprone-unchecked-optional-access): the REQUIRE above checks it.
      REQUIRE(arude::config::as_text(bytes.value()) == "contents\n");
    }
  }

  GIVEN("URLs the server answers with 404 and with 410")
  {
    THEN("both read as nothing there")
    {
      REQUIRE(!transport_https{server.url("/missing"), options}.read().has_value());
      REQUIRE(!transport_https{server.url("/gone"), options}.read().has_value());
    }
  }

  GIVEN("URLs the server answers with 403 and with 500")
  {
    THEN("both are an io_error, not nothing there")
    {
      for(const auto* const target : {"/forbidden", "/broken"})
      {
        const auto failure = transport_https_test::failure_of(
          [&server, target, &options] -> void { std::ignore = transport_https{server.url(target), options}.read(); });

        REQUIRE(failure.has_value());
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access): the REQUIRE above checks it.
        REQUIRE(failure->data() == config_error::io_error);
      }
    }
  }

  GIVEN("a URL with a fragment")
  {
    WHEN("the transport reads it")
    {
      const auto bytes = transport_https{server.url("/app.toml?v=1#section"), options}.read();

      THEN("the request leaves the fragment out and keeps the query")
      {
        REQUIRE(bytes.has_value());
        REQUIRE(transport_https_test::targets_seen_by(server) == std::vector<std::string>{"/app.toml?v=1"});
      }
    }
  }

  GIVEN("a CA file holding some other certificate than the one the server presents")
  {
    const auto stranger = transport_https_test::make_certificate();
    const auto untrusting = transport_https_test::options_trusting(directory, "stranger.pem", stranger);

    THEN("the read is an io_error, and no request reaches the server")
    {
      const auto failure = transport_https_test::failure_of(
        [&server, &untrusting] -> void { std::ignore = transport_https{server.url("/app.toml"), untrusting}.read(); });

      REQUIRE(failure.has_value());
      // NOLINTNEXTLINE(bugprone-unchecked-optional-access): the REQUIRE above checks it.
      REQUIRE(failure->data() == config_error::io_error);
      REQUIRE(server.requests().empty());
    }
  }
}

// A redirect is followed by the transport itself, so that it can hold every
// hop to https and the number of hops to redirect_limit. httplib, left to it,
// would follow to http with a plaintext client and count against its own limit.
SCENARIO("an https transport follows redirects only to https and only so far", "[config][transport][transport_https]")
{
  using arude::config::config_error;
  using arude::config::transport_https;
  using Catch::Matchers::ContainsSubstring;

  const auto identity = transport_https_test::make_certificate();
  const auto directory = test_helpers::temp_directory{"arude_transport_https_redirect"};
  auto options = transport_https_test::options_trusting(directory, "ca.pem", identity);

  auto plain = transport_https_test::test_server{std::make_unique<httplib::Server>(), "http"};
  plain.start();
  auto other = transport_https_test::test_server{transport_https_test::make_https_server(identity), "https"};
  other.server().Get(
    "/app.toml",
    []([[maybe_unused]] const httplib::Request& request, httplib::Response& response) -> void
    { response.set_content("elsewhere\n", "text/plain"); });
  other.start();
  auto server = transport_https_test::test_server{transport_https_test::make_https_server(identity), "https"};
  transport_https_test::add_routes(server.server(), plain.origin(), other.origin());
  server.start();

  GIVEN("a redirect to an absolute path on the same origin")
  {
    WHEN("the transport reads it")
    {
      const auto bytes = transport_https{server.url("/moved"), options}.read();

      THEN("it reads what the redirect leads to")
      {
        REQUIRE(bytes.has_value());
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access): the REQUIRE above checks it.
        REQUIRE(arude::config::as_text(bytes.value()) == "contents\n");
        REQUIRE(transport_https_test::targets_seen_by(server) == std::vector<std::string>{"/moved", "/app.toml"});
      }
    }
  }

  GIVEN("a redirect to a relative path")
  {
    THEN("it is resolved against the directory of the URL that answered")
    {
      const auto bytes = transport_https{server.url("/dir/moved"), options}.read();

      REQUIRE(bytes.has_value());
      // NOLINTNEXTLINE(bugprone-unchecked-optional-access): the REQUIRE above checks it.
      REQUIRE(arude::config::as_text(bytes.value()) == "relative\n");
    }
  }

  GIVEN("a redirect to another https origin")
  {
    THEN("it is followed there, verified against the same CA")
    {
      const auto bytes = transport_https{server.url("/elsewhere"), options}.read();

      REQUIRE(bytes.has_value());
      // NOLINTNEXTLINE(bugprone-unchecked-optional-access): the REQUIRE above checks it.
      REQUIRE(arude::config::as_text(bytes.value()) == "elsewhere\n");
      REQUIRE(transport_https_test::targets_seen_by(other) == std::vector<std::string>{"/app.toml"});
    }
  }

  GIVEN("a redirect to an http URL")
  {
    THEN("it is an io_error naming both URLs, and the plaintext server is never asked")
    {
      const auto url = server.url("/to-plain");
      const auto failure = transport_https_test::failure_of(
        [&url, &options] -> void { std::ignore = transport_https{url, options}.read(); });

      REQUIRE(failure.has_value());
      // NOLINTNEXTLINE(bugprone-unchecked-optional-access): the REQUIRE above checks it.
      REQUIRE(failure->data() == config_error::io_error);
      // NOLINTNEXTLINE(bugprone-unchecked-optional-access): the REQUIRE above checks it.
      REQUIRE_THAT(failure->str(), ContainsSubstring(url) && ContainsSubstring(plain.url("/app.toml")));
      REQUIRE(plain.requests().empty());
    }
  }

  GIVEN("a redirect to an https URL carrying userinfo")
  {
    THEN("it is an io_error that does not quote the userinfo, and the other server is never asked")
    {
      for(const auto* const target : {"/to-userinfo", "/to-slashed-userinfo"})
      {
        const auto failure = transport_https_test::failure_of(
          [&server, target, &options] -> void { std::ignore = transport_https{server.url(target), options}.read(); });

        REQUIRE(failure.has_value());
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access): the REQUIRE above checks it.
        REQUIRE(failure->data() == config_error::io_error);
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access): the REQUIRE above checks it.
        REQUIRE_THAT(failure->to_string(), !ContainsSubstring("secret"));
      }

      REQUIRE(other.requests().empty());
    }
  }

  GIVEN("a redirect answer without a Location")
  {
    THEN("it is an io_error")
    {
      const auto failure = transport_https_test::failure_of(
        [&server, &options] -> void { std::ignore = transport_https{server.url("/nowhere"), options}.read(); });

      REQUIRE(failure.has_value());
      // NOLINTNEXTLINE(bugprone-unchecked-optional-access): the REQUIRE above checks it.
      REQUIRE(failure->data() == config_error::io_error);
    }
  }

  GIVEN("a URL that redirects to itself, and a limit of three redirects")
  {
    options.redirect_limit = 3;

    THEN("three redirects are followed and the fourth is an io_error naming where it led")
    {
      const auto failure = transport_https_test::failure_of(
        [&server, &options] -> void { std::ignore = transport_https{server.url("/loop"), options}.read(); });

      REQUIRE(failure.has_value());
      // NOLINTNEXTLINE(bugprone-unchecked-optional-access): the REQUIRE above checks it.
      REQUIRE(failure->data() == config_error::io_error);
      // NOLINTNEXTLINE(bugprone-unchecked-optional-access): the REQUIRE above checks it.
      REQUIRE_THAT(failure->str(), ContainsSubstring(std::format("redirected to '{}'", server.url("/loop"))));
      REQUIRE(server.requests().size() == 4);
    }
  }

  GIVEN("a limit of no redirects at all")
  {
    options.redirect_limit = 0;

    THEN("the first redirect is an io_error, and where it leads is never asked")
    {
      const auto failure = transport_https_test::failure_of(
        [&server, &options] -> void { std::ignore = transport_https{server.url("/moved"), options}.read(); });

      REQUIRE(failure.has_value());
      // NOLINTNEXTLINE(bugprone-unchecked-optional-access): the REQUIRE above checks it.
      REQUIRE(failure->data() == config_error::io_error);
      REQUIRE(transport_https_test::targets_seen_by(server) == std::vector<std::string>{"/moved"});
    }
  }
}

// Read-only is the default, and refusing a write must not cost a request. A
// write that is allowed is one PUT, and never more: a redirect is not followed.
SCENARIO("an https transport writes only when it is writable", "[config][transport][transport_https]")
{
  using arude::config::config_error;
  using arude::config::transport_https;

  const auto identity = transport_https_test::make_certificate();
  const auto directory = test_helpers::temp_directory{"arude_transport_https_write"};
  auto options = transport_https_test::options_trusting(directory, "ca.pem", identity);

  auto plain = transport_https_test::test_server{std::make_unique<httplib::Server>(), "http"};
  plain.start();
  auto server = transport_https_test::test_server{transport_https_test::make_https_server(identity), "https"};
  transport_https_test::add_routes(server.server(), plain.origin(), plain.origin());
  server.start();

  GIVEN("a transport left read-only")
  {
    const auto transport = transport_https{server.url("/app.toml"), options};

    THEN("a write is refused with read_only, and nothing reaches the server")
    {
      REQUIRE(!transport.writable());

      const auto failure = transport_https_test::failure_of(
        [&transport] -> void { transport.write(arude::config::as_bytes("new contents\n")); });

      REQUIRE(failure.has_value());
      // NOLINTNEXTLINE(bugprone-unchecked-optional-access): the REQUIRE above checks it.
      REQUIRE(failure->data() == config_error::read_only);
      REQUIRE(server.requests().empty());
    }
  }

  GIVEN("a writable transport")
  {
    options.writable = true;
    options.content_type = "application/toml";

    WHEN("it writes")
    {
      transport_https{server.url("/app.toml"), options}.write(arude::config::as_bytes("new contents\n"));

      THEN("the server receives one PUT with the bytes and the content type")
      {
        const auto requests = server.requests();

        REQUIRE(requests.size() == 1);
        REQUIRE(requests.front().method == "PUT");
        REQUIRE(requests.front().target == "/app.toml");
        REQUIRE(requests.front().body == "new contents\n");
        REQUIRE(requests.front().content_type == "application/toml");
      }
    }

    WHEN("it writes to a URL that answers the PUT with a redirect")
    {
      const auto failure = transport_https_test::failure_of(
        [&server, &options] -> void
        { transport_https{server.url("/moved"), options}.write(arude::config::as_bytes("new contents\n")); });

      THEN("the write is an io_error, and the redirect is not followed")
      {
        REQUIRE(failure.has_value());
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access): the REQUIRE above checks it.
        REQUIRE(failure->data() == config_error::io_error);
        REQUIRE(transport_https_test::targets_seen_by(server) == std::vector<std::string>{"/moved"});
      }
    }
  }
}

// Userinfo would put a password into location(), which the manager keys on and
// every error quotes, so it is refused, and the refusal does not quote it either.
SCENARIO("an https transport refuses a URL it cannot use", "[config][transport][transport_https]")
{
  using arude::config::config_error;
  using arude::config::transport_https;
  using Catch::Matchers::ContainsSubstring;

  // From the fourth on, the authority cannot be told apart, for a leading
  // space or a password that ends it early; the refusal must still not quote
  // the password.
  GIVEN("URLs whose authority carries userinfo")
  {
    THEN("each is an invalid_location whose message leaves the userinfo out")
    {
      for(const auto* const url :
          {"https://user:secret@127.0.0.1:8443/app.toml",
           "https://secret@example.com",
           "http://user:secret@example.com/x",
           " https://user:secret@example.com/x",
           "\thttps://user:secret@example.com/",
           "https://user:secret/x@example.com/x",
           "https://user:secret?x@example.com/x",
           "https://user:secret#x@example.com/x",
           "https:/user:secret@example.com/x",
           "user:secret@example.com/x"})
      {
        const auto failure = transport_https_test::failure_of([url] -> void { std::ignore = transport_https{url}; });

        REQUIRE(failure.has_value());
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access): the REQUIRE above checks it.
        REQUIRE(failure->data() == config_error::invalid_location);
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access): the REQUIRE above checks it.
        REQUIRE_THAT(failure->to_string(), !ContainsSubstring("secret"));
      }
    }
  }

  GIVEN("URLs with no host, or with a port that is not one")
  {
    THEN("each is an invalid_location")
    {
      for(const auto* const url :
          {"https://", "https:///app.toml", "https://:8443/", "https://host:http/", "https://[::1/"})
      {
        const auto failure = transport_https_test::failure_of([url] -> void { std::ignore = transport_https{url}; });

        REQUIRE(failure.has_value());
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access): the REQUIRE above checks it.
        REQUIRE(failure->data() == config_error::invalid_location);
      }
    }
  }

  GIVEN("a URL with an '@' only after the authority")
  {
    THEN("it is accepted, and location() gives it back exactly")
    {
      static constexpr auto url = "https://example.com/users/@me?at=a@b#c@d";

      REQUIRE(transport_https{url}.location() == url);
    }
  }
}

// The Location of a redirect is resolved the way a browser would, short of
// removing dot segments, and nothing but https comes out of it.
SCENARIO("a redirect's Location is resolved against the URL that answered", "[config][transport][transport_https]")
{
  using arude::config::detail::https_url;
  using arude::config::detail::https_url_error;
  using arude::config::detail::resolve_redirect;

  GIVEN("a URL with a path and a query")
  {
    const auto base = https_url{.origin = "https://example.com", .target = "/a/b.toml?v=1"};

    THEN("each form of reference resolves as RFC 3986 has it")
    {
      const auto resolved = [&base](const std::string_view location) -> std::string
      {
        const auto next = resolve_redirect(base, location);
        return next ? std::format("{}{}", next->origin, next->target) : std::string{"refused"};
      };

      REQUIRE(resolved("/c.toml") == "https://example.com/c.toml");
      REQUIRE(resolved("c.toml?w=2") == "https://example.com/a/c.toml?w=2");
      REQUIRE(resolved("?w=2") == "https://example.com/a/b.toml?w=2");
      REQUIRE(resolved("#part") == "https://example.com/a/b.toml?v=1");
      REQUIRE(resolved("//other.example:8443/d#part") == "https://other.example:8443/d");
      REQUIRE(resolved("HTTPS://other.example") == "https://other.example/");
      REQUIRE(resolved("https://other.example?x") == "https://other.example/?x");
    }

    THEN("anything but https is refused")
    {
      REQUIRE(resolve_redirect(base, "http://example.com/a").error() == https_url_error::not_https);
      REQUIRE(resolve_redirect(base, "ftp://example.com/a").error() == https_url_error::not_https);
      REQUIRE(resolve_redirect(base, "javascript:alert(1)").error() == https_url_error::not_https);
      REQUIRE(resolve_redirect(base, "https://user:pw@example.com/").error() == https_url_error::userinfo);
    }
  }
}
