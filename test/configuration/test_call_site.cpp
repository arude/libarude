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
// concepts ask for callable, so arude's own transport and endpoint still
// satisfy them.
static_assert(arude::config::transport_c<arude::config::transport_file>);
static_assert(arude::config::path_transport_c<arude::config::transport_file>);
static_assert(arude::config::endpoint_c<arude::config::endpoint_toml, arude::config::transport_file>);

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

SCENARIO("a section missing from the source is reported where load() was called", "[config][call_site]")
{
  GIVEN("a document whose source has no section of the name added")
  {
    const auto file = test_helpers::temp_file{"arude_call_site_missing_section.toml"};
    test_helpers::write_text(file.path(), "version = 2\n");

    auto manager = arude::config::config_manager{};
    auto document =
      manager.register_document(arude::config::endpoint_toml{}, arude::config::transport_file{file.path()});
    auto network = document.add_section<test_helpers::config_test_t>("network");

    WHEN("the document is loaded through the section's handle without creating it")
    {
      auto call = std::source_location{};
      const auto failure = call_site_test::failure_of<arude::config::exception_t>(
        [&call, &network] -> void
        {
          call = std::source_location::current();
          network.load(arude::config::load_policy::none);
        });

      THEN("it is a not_found raised at that call, which reads as one failure")
      {
        REQUIRE(failure.data() == arude::config::config_error::not_found);
        REQUIRE(call_site_test::raised_at(failure, call));
        REQUIRE(call_site_test::reads_as_one_failure(failure));
      }
    }

    WHEN("its value is asked for before any load")
    {
      auto call = std::source_location{};
      const auto failure = call_site_test::failure_of<arude::config::exception_t>(
        [&call, &network] -> void
        {
          call = std::source_location::current();
          std::ignore = network.get();
        });

      THEN("it is a not_found raised at the get()")
      {
        REQUIRE(failure.data() == arude::config::config_error::not_found);
        REQUIRE(call_site_test::raised_at(failure, call));
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

SCENARIO("a binary field that is not base64 is reported where it was read", "[config][call_site]")
{
  GIVEN("TOML text whose binary field is not base64")
  {
    constexpr auto text = std::string_view{"version = 2\nsecret = \"!!!!\"\n"};

    WHEN("it is parsed as a configuration")
    {
      auto call = std::source_location{};
      const auto failure = call_site_test::failure_of<arude::config::exception_t>(
        [&call, &text] -> void
        {
          call = std::source_location::current();
          std::ignore = arude::config::from_toml<test_helpers::config_test_t>(text);
        });

      THEN("it is an invalid_payload raised at the call, naming the base64 failure as text")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
        REQUIRE(failure.str().contains("is not a base64 character"));
        REQUIRE(call_site_test::raised_at(failure, call));
        REQUIRE(call_site_test::reads_as_one_failure(failure));
      }
    }
  }

  GIVEN("a document whose source holds such a field")
  {
    const auto file = test_helpers::temp_file{"arude_call_site_base64.toml"};
    test_helpers::write_text(file.path(), "version = 2\nsecret = \"!!!!\"\n");

    auto manager = arude::config::config_manager{};
    auto document =
      manager.register_document(arude::config::endpoint_toml{}, arude::config::transport_file{file.path()});
    std::ignore = document.add_root<test_helpers::config_test_t>();

    WHEN("the document is loaded")
    {
      auto call = std::source_location{};
      const auto failure = call_site_test::failure_of<arude::config::exception_t>(
        [&call, &document] -> void
        {
          call = std::source_location::current();
          document.load();
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
  GIVEN("a document whose file would have to sit beneath a regular file")
  {
    const auto blocker = test_helpers::temp_file{"arude_call_site_blocker"};
    test_helpers::write_text(blocker.path(), "not a directory\n");

    auto manager = arude::config::config_manager{};
    auto document = manager.register_document(
      arude::config::endpoint_toml{}, arude::config::transport_file{blocker.path() / "app.toml"});
    auto root = document.add_root<test_helpers::config_test_t>();
    root.set(test_helpers::config_test_t{});

    WHEN("it is stored")
    {
      auto call = std::source_location{};
      const auto failure = call_site_test::failure_of<arude::config::exception_t>(
        [&call, &document] -> void
        {
          call = std::source_location::current();
          document.store();
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

SCENARIO("the manager reports a failure where it was asked", "[config][call_site]")
{
  GIVEN("a manager with a document registered")
  {
    const auto file = test_helpers::temp_file{"arude_call_site_manager.toml"};

    auto manager = arude::config::config_manager{};
    std::ignore = manager.register_document(arude::config::endpoint_toml{}, arude::config::transport_file{file.path()});

    WHEN("a type registered nowhere is asked for")
    {
      auto call = std::source_location{};
      const auto failure = call_site_test::failure_of<arude::config::exception_t>(
        [&call, &manager] -> void
        {
          call = std::source_location::current();
          std::ignore = manager.get<test_helpers::config_test_t>();
        });

      THEN("it is a not_found raised at the get()")
      {
        REQUIRE(failure.data() == arude::config::config_error::not_found);
        REQUIRE(call_site_test::raised_at(failure, call));
      }
    }

    WHEN("the same location is registered again")
    {
      auto call = std::source_location{};
      const auto failure = call_site_test::failure_of<arude::config::exception_t>(
        [&call, &manager, &file] -> void
        {
          call = std::source_location::current();
          std::ignore =
            manager.register_document(arude::config::endpoint_toml{}, arude::config::transport_file{file.path()});
        });

      THEN("it is an already_registered_location raised at the call")
      {
        REQUIRE(failure.data() == arude::config::config_error::already_registered_location);
        REQUIRE(std::string_view{failure.where().file_name()} == std::string_view{call.file_name()});
      }
    }

    WHEN("every document is loaded, and the file is not there")
    {
      auto call = std::source_location{};
      const auto failure = call_site_test::failure_of<arude::config::exception_t>(
        [&call, &manager] -> void
        {
          call = std::source_location::current();
          manager.load_all(arude::config::load_policy::none);
        });

      THEN("it is a not_found raised at the load_all()")
      {
        REQUIRE(failure.data() == arude::config::config_error::not_found);
        REQUIRE(call_site_test::raised_at(failure, call));
      }
    }
  }
}
