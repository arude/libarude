///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///

#include <rfl/from_generic.hpp>
#include <rfl/Generic.hpp>
#include <rfl/Literal.hpp>
#include <rfl/Timestamp.hpp>
#include <rfl/to_generic.hpp>
#include <rfl/toml/read.hpp>
#include <rfl/toml/write.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace generic_toml_test
{

///
/// A type with an optional field, for the empty-state spike.
///
struct person
{
  rfl::Field<"name", std::string> name;
  rfl::Field<"count", int> count;
  rfl::Field<"note", std::optional<std::string>> note;
};

///
/// A type with a timestamp field, for the datetime spike.
///
struct launch_event
{
  rfl::Field<"title", std::string> title;
  rfl::Field<"when", rfl::Timestamp<"%Y-%m-%d">> when;
};

///
/// Reads the string stored under \p key in \p object.
///
/// \param object The document to read from.
/// \param key The field name.
/// \return The stored string.
///
auto string_at(const rfl::Generic::Object& object, const std::string& key) -> std::string
{
  return object.get(key).value().to_string().value();
}

///
/// Reads the integer stored under \p key in \p object.
///
/// \param object The document to read from.
/// \param key The field name.
/// \return The stored integer.
///
auto int_at(const rfl::Generic::Object& object, const std::string& key) -> std::int64_t
{
  return object.get(key).value().to_int64().value();
}

///
/// Reads the nested table stored under \p key in \p object.
///
/// \param object The document to read from.
/// \param key The field name.
/// \return The stored table.
///
auto table_at(const rfl::Generic::Object& object, const std::string& key) -> rfl::Generic::Object
{
  return object.get(key).value().to_object().value();
}

} // namespace generic_toml_test

// Writing works on rfl::Generic::Object, not on a bare rfl::Generic: the toml
// writer rejects scalar values at the root, and writing a Generic instantiates
// every alternative of its variant, scalars included, so that spelling fails
// to compile even for a Generic that holds a table. The document type the
// design uses is Generic::Object, which is what this scenario pins.
SCENARIO("a toml document with nested tables round-trips through rfl::Generic", "[config][generic_toml]")
{
  GIVEN("a document with top-level keys and two subsections")
  {
    const auto text = std::string_view{"version = 1\n"
                                       "name = \"example\"\n"
                                       "\n"
                                       "[network]\n"
                                       "host = \"localhost\"\n"
                                       "port = 8443\n"
                                       "\n"
                                       "[logging]\n"
                                       "level = \"debug\"\n"};

    WHEN("it is read into a Generic and written back out as a Generic::Object")
    {
      const auto first = rfl::toml::read<rfl::Generic>(text);
      REQUIRE(first.has_value());

      const auto object = first.value().to_object().value();
      const auto emitted = rfl::toml::write(object);

      THEN("every key and value came through")
      {
        REQUIRE(object.size() == 4);
        REQUIRE(generic_toml_test::int_at(object, "version") == 1);
        REQUIRE(generic_toml_test::string_at(object, "name") == "example");

        const auto network = generic_toml_test::table_at(object, "network");
        REQUIRE(generic_toml_test::string_at(network, "host") == "localhost");
        REQUIRE(generic_toml_test::int_at(network, "port") == 8443);

        const auto logging = generic_toml_test::table_at(object, "logging");
        REQUIRE(generic_toml_test::string_at(logging, "level") == "debug");
      }

      THEN("the written text reads back as the same document")
      {
        const auto second = rfl::toml::read<rfl::Generic>(emitted);
        REQUIRE(second.has_value());
        REQUIRE(rfl::toml::write(second.value().to_object().value()) == emitted);
      }

      THEN("both subsections arrive as tables in the emitted text")
      {
        REQUIRE(emitted.find("[network]") != std::string::npos);
        REQUIRE(emitted.find("[logging]") != std::string::npos);
      }
    }
  }
}

// TOML has no null, and reflect-cpp's answer is to omit an unset optional
// field rather than spell a null state: to_generic leaves it out of the
// object, so the write never sees it, and from_generic puts the disengaged
// optional back when the key is absent. That is exactly the behaviour the
// never-clobber rule needs, and it is pinned here so an upstream change to
// the omission is noticed rather than silently relied on.
SCENARIO("an unset optional field survives the trip into rfl::Generic and out through toml", "[config][generic_toml]")
{
  GIVEN("a struct whose optional field is disengaged")
  {
    const auto person = generic_toml_test::person{.name = "ada", .count = 3, .note = std::nullopt};

    WHEN("it is converted to a Generic and written as toml")
    {
      const auto generic = rfl::to_generic(person);
      const auto object = generic.to_object().value();
      const auto text = rfl::toml::write(object);

      THEN("the field is left out of the object rather than stored as a null state")
      {
        REQUIRE(object.size() == 2);
        REQUIRE(!object.get("note").has_value());
        REQUIRE(!text.contains("note"));
      }

      THEN("the emitted toml reads back into a Generic and then the struct, with the field disengaged")
      {
        const auto back = rfl::toml::read<rfl::Generic>(text);
        REQUIRE(back.has_value());

        const auto round_tripped = rfl::from_generic<generic_toml_test::person>(back.value());
        REQUIRE(round_tripped.has_value());
        REQUIRE(round_tripped->name() == "ada");
        REQUIRE(round_tripped->count() == 3);
        REQUIRE(!round_tripped->note().has_value());
      }
    }
  }

  GIVEN("a file whose optional field is an explicit empty string")
  {
    const auto text = std::string_view{"name = \"ada\"\n"
                                       "count = 3\n"
                                       "note = \"\"\n"};

    WHEN("it is read and converted back to the struct")
    {
      const auto back = rfl::toml::read<rfl::Generic>(text);
      REQUIRE(back.has_value());

      const auto round_tripped = rfl::from_generic<generic_toml_test::person>(back.value());

      THEN("the field is engaged and empty, not disengaged and not an error")
      {
        REQUIRE(round_tripped.has_value());
        REQUIRE(round_tripped->note().has_value());
        REQUIRE(round_tripped->note()->empty());
      }
    }
  }
}

// Generic's variant has no datetime alternative, so a timestamp field becomes
// a plain string on the way in. The degradation is pinned rather than left to
// drift: an endpoint round-tripping such a field through a struct of its own
// shape gets it back, but a consumer expecting a TOML native datetime would
// not.
SCENARIO("a timestamp field degrades to a string in the toml output", "[config][generic_toml]")
{
  GIVEN("a struct with a timestamp field")
  {
    const auto event = generic_toml_test::launch_event{.title = "launch", .when = "2026-08-17"};

    WHEN("it is converted to a Generic and written as toml")
    {
      const auto generic = rfl::to_generic(event);
      const auto object = generic.to_object().value();
      const auto text = rfl::toml::write(object);

      THEN("the timestamp arrives as a quoted string, not a toml datetime")
      {
        REQUIRE(text.contains("when = '2026-08-17'"));
        REQUIRE(!text.contains("when = 2026-08-17"));
      }

      THEN("it round-trips through the struct shape it came from")
      {
        const auto back = rfl::toml::read<rfl::Generic>(text);
        REQUIRE(back.has_value());

        const auto round_tripped = rfl::from_generic<generic_toml_test::launch_event>(back.value());
        REQUIRE(round_tripped.has_value());
        REQUIRE(round_tripped->title() == "launch");
        REQUIRE(round_tripped->when().reflection() == "2026-08-17");
      }
    }
  }
}
