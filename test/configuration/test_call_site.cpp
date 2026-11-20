///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// A failure raised deep inside the configuration subsystem is reported at the
/// call the application made, with a message that carries the failure
/// underneath as text alone: one location and one stacktrace per report.
///

#include "arude/config.hpp"
#include "arude/exception.hpp"
#include "test/configuration/helpers/memory_transport.hpp"
#include "test/helpers/config_test_v2.hpp"
#include "test/helpers/temp_file.hpp"
#include "test/helpers/write_text.hpp"

#include <catch2/catch_test_macros.hpp>

#include <concepts>
#include <cstddef>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>

// A defaulted trailing std::source_location leaves the nullary forms the
// concepts ask for callable, so arude's own transports still satisfy them.
static_assert(arude::config::transport_c<arude::config::transport_file>);
static_assert(arude::config::path_transport_c<arude::config::transport_file>);

namespace
{
namespace call_site_test
{

///
/// Counts the occurrences of a piece of text.
///
/// \param text Text to search. Not retained.
/// \param part Text to count. Not retained.
/// \return How often part occurs in text, without overlapping.
///
[[nodiscard]] auto occurrences(std::string_view text, std::string_view part) -> std::size_t;

///
/// Reports whether an exception was raised at a call site.
///
/// \param failure The exception.
/// \param before Where the line just before the call is, as std::source_location::current() names it there.
/// \return true if the exception names that file, and the line after.
///
[[nodiscard]] auto raised_at(const arude::exception_base& failure, const std::source_location& before) -> bool;

///
/// Reports whether an exception reads as one failure: a message that holds no
/// location and no stacktrace of anything underneath, and a report that holds
/// one of each.
///
/// \param failure The exception.
/// \return true if it does.
///
[[nodiscard]] auto reads_as_one_failure(const arude::exception_base& failure) -> bool;

///
/// Runs a call that is expected to throw, and returns what it threw.
/// The exception comes back by value, so a scenario has nothing to check for
/// emptiness before reading it.
///
/// \tparam E The exception type expected.
/// \param call The call. Not retained.
/// \return The exception the call threw.
/// \throws std::logic_error If the call returns instead, which fails the scenario.
///
template<typename E>
[[nodiscard]] auto failure_of(const std::invocable auto& call) -> E;

///
///
auto occurrences(const std::string_view text, const std::string_view part) -> std::size_t
{
  auto count = std::size_t{0};

  for(auto at = text.find(part); at != std::string_view::npos; at = text.find(part, at + part.size()))
  {
    ++count;
  }

  return count;
}

///
///
auto raised_at(const arude::exception_base& failure, const std::source_location& before) -> bool
{
  return std::string_view{failure.where().file_name()} == std::string_view{before.file_name()} &&
         failure.where().line() == before.line() + 1;
}

///
///
auto reads_as_one_failure(const arude::exception_base& failure) -> bool
{
  const auto report = failure.to_string();

  return !failure.str().contains("stacktrace:") &&
         !failure.str().contains("location:") &&
         occurrences(report, "location:") == 1 &&
         occurrences(report, "stacktrace:") == 1;
}

///
///
template<typename E>
auto failure_of(const std::invocable auto& call) -> E
{
  try
  {
    call();
  }
  catch(const E& ex)
  {
    return ex;
  }

  throw std::logic_error{"The call was expected to throw, and returned."};
}

} // namespace call_site_test

} // namespace

SCENARIO("a source that is not there is reported where load() was called", "[config][call_site]")
{
  GIVEN("a file whose source is not there")
  {
    auto store = config_test::memory_transport::store_t{};

    auto file = arude::config::config_file<test_helpers::config_test_t, config_test::memory_transport>{
      config_test::memory_transport{"app", store}
    };

    WHEN("the file is loaded without creating it")
    {
      auto call = std::source_location{};
      const auto failure = call_site_test::failure_of<arude::config::exception_t>(
        [&call, &file] -> void
        {
          call = std::source_location::current();
          file.load(arude::config::load_policy::none);
        });

      THEN("it is a not_found raised at that call, which reads as one failure")
      {
        REQUIRE(failure.data() == arude::config::config_error::not_found);
        REQUIRE(call_site_test::raised_at(failure, call));
        REQUIRE(call_site_test::reads_as_one_failure(failure));
      }
    }
  }
}

SCENARIO("text that is not TOML is reported where it was parsed", "[config][call_site]")
{
  GIVEN("text that is not TOML")
  {
    constexpr auto text = std::string_view{"version = = 2\n"};

    WHEN("it is parsed as a configuration")
    {
      auto call = std::source_location{};
      const auto failure = call_site_test::failure_of<arude::config::exception_t>(
        [&call, &text] -> void
        {
          call = std::source_location::current();
          std::ignore = arude::config::from_toml<test_helpers::config_test_t>(text);
        });

      THEN("it is an invalid_payload raised at the call, carrying toml++'s message as text")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
        REQUIRE(failure.str().contains("is not valid TOML"));
        REQUIRE(call_site_test::raised_at(failure, call));
        REQUIRE(call_site_test::reads_as_one_failure(failure));
      }
    }

    WHEN("its version is asked for")
    {
      auto call = std::source_location{};
      const auto failure = call_site_test::failure_of<arude::config::exception_t>(
        [&call, &text] -> void
        {
          call = std::source_location::current();
          std::ignore = arude::config::toml_version(text);
        });

      THEN("it is an invalid_payload raised at the call")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
        REQUIRE(call_site_test::raised_at(failure, call));
        REQUIRE(call_site_test::reads_as_one_failure(failure));
      }
    }
  }

  GIVEN("TOML text that declares no version")
  {
    constexpr auto text = std::string_view{"name = \"example\"\n"};

    WHEN("its version is asked for")
    {
      auto call = std::source_location{};
      const auto failure = call_site_test::failure_of<arude::config::exception_t>(
        [&call, &text] -> void
        {
          call = std::source_location::current();
          std::ignore = arude::config::toml_version(text);
        });

      THEN("the failure underneath is quoted by its message alone")
      {
        REQUIRE(failure.str().contains("declares no configuration version"));
        REQUIRE(call_site_test::raised_at(failure, call));
        REQUIRE(call_site_test::reads_as_one_failure(failure));
      }
    }
  }
}

SCENARIO("a binary field that is not base64 is reported where the file was loaded", "[config][call_site]")
{
  GIVEN("a file whose source holds a payload that is not base64")
  {
    const auto file = test_helpers::temp_file{"arude_call_site_base64.toml"};
    test_helpers::write_text(file.path(), "version = 2\nsecret = \"!!!!\"\n");

    auto config = arude::config::config_file<test_helpers::config_test_t, arude::config::transport_file>{
      arude::config::transport_file{file.path()}};

    WHEN("the file is loaded")
    {
      auto call = std::source_location{};
      const auto failure = call_site_test::failure_of<arude::config::exception_t>(
        [&call, &config] -> void
        {
          call = std::source_location::current();
          config.load();
        });

      THEN("it is an invalid_payload raised at the load()")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
        REQUIRE(failure.str().contains("is not a base64 character"));
        REQUIRE(call_site_test::raised_at(failure, call));
        REQUIRE(call_site_test::reads_as_one_failure(failure));
      }
    }
  }

  GIVEN("text that is not base64")
  {
    WHEN("it is decoded directly")
    {
      auto call = std::source_location{};
      const auto failure = call_site_test::failure_of<arude::exception<>>(
        [&call] -> void
        {
          call = std::source_location::current();
          std::ignore = arude::config::base64_decode("Zm9v!A==");
        });

      THEN("the failure is raised at the call and not inside the decoder")
      {
        REQUIRE(call_site_test::raised_at(failure, call));
      }
    }

    WHEN("it is given to a payload")
    {
      auto payload = arude::config::binary{};
      auto call = std::source_location{};
      const auto failure = call_site_test::failure_of<arude::exception<>>(
        [&call, &payload] -> void
        {
          call = std::source_location::current();
          payload.base64("Zm9");
        });

      THEN("the failure is raised at the call")
      {
        REQUIRE(call_site_test::raised_at(failure, call));
      }
    }
  }
}

SCENARIO("a write the transport cannot make is reported where store() was called", "[config][call_site]")
{
  GIVEN("a file whose target would have to sit beneath a regular file")
  {
    const auto blocker = test_helpers::temp_file{"arude_call_site_blocker"};
    test_helpers::write_text(blocker.path(), "not a directory\n");

    auto file = arude::config::config_file<test_helpers::config_test_t, arude::config::transport_file>{
      arude::config::transport_file{blocker.path() / "app.toml"}};
    file.set(test_helpers::config_test_t{});

    WHEN("it is stored")
    {
      auto call = std::source_location{};
      const auto failure = call_site_test::failure_of<arude::config::exception_t>(
        [&call, &file] -> void
        {
          call = std::source_location::current();
          file.store();
        });

      THEN("it is an io_error raised at the store(), quoting the filesystem error as text")
      {
        REQUIRE(failure.data() == arude::config::config_error::io_error);
        REQUIRE(failure.str().contains("could not be written"));
        REQUIRE(call_site_test::raised_at(failure, call));
        REQUIRE(call_site_test::reads_as_one_failure(failure));
      }
    }
  }
}

SCENARIO("a member handle on an unloaded root reports at the handle's call site", "[config][call_site]")
{
  GIVEN("a file with nothing loaded or set, and a handle made from it")
  {
    auto store = config_test::memory_transport::store_t{};

    auto file = arude::config::config_file<test_helpers::config_test_t, config_test::memory_transport>{
      config_test::memory_transport{"app", store}
    };
    auto handle = file.member(&test_helpers::config_test_t::name);

    WHEN("the member is asked for")
    {
      auto call = std::source_location{};
      const auto failure = call_site_test::failure_of<arude::config::exception_t>(
        [&call, &handle] -> void
        {
          call = std::source_location::current();
          std::ignore = handle.get();
        });

      THEN("it is a not_found raised at the handle's get()")
      {
        REQUIRE(failure.data() == arude::config::config_error::not_found);
        REQUIRE(call_site_test::raised_at(failure, call));
        REQUIRE(call_site_test::reads_as_one_failure(failure));
      }
    }
  }
}

SCENARIO("a store refused by the probe is reported where store() was called", "[config][call_site]")
{
  GIVEN("a loaded file whose source is then replaced by a newer version")
  {
    auto store = config_test::memory_transport::store_t{};
    store["app"] = arude::config::as_bytes(
      std::string{"version = 2\n"
                  "name = \"current\"\n"
                  "retries = 3\n"
                  "endpoint = \"localhost\"\n"
                  "secret = \"\"\n"});

    auto file = arude::config::config_file<test_helpers::config_test_t, config_test::memory_transport>{
      config_test::memory_transport{"app", store}
    };
    file.load();

    store["app"] = arude::config::as_bytes(std::string{"version = 99\n"});

    WHEN("it is stored")
    {
      auto call = std::source_location{};
      const auto failure = call_site_test::failure_of<arude::config::exception_t>(
        [&call, &file] -> void
        {
          call = std::source_location::current();
          file.store();
        });

      THEN("it is an invalid_version raised at the store(), naming the version found")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_version);
        REQUIRE(failure.str().contains("refusing to store over what this build cannot read"));
        REQUIRE(failure.str().contains("version 99"));
        REQUIRE(call_site_test::raised_at(failure, call));
        REQUIRE(call_site_test::reads_as_one_failure(failure));
      }
    }
  }
}
