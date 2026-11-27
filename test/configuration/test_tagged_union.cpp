///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// rfl::TaggedUnion alternatives holding versioned<> members: the tag picks
/// the one alternative read, its member keeps the version rules, and a tag
/// no alternative answers to is taken for newer data.
///

#include "arude/config.hpp"
#include "test/configuration/helpers/memory_transport.hpp"

#include <catch2/catch_test_macros.hpp>
#include <rfl/Attribute.hpp>
#include <rfl/DefaultVal.hpp>
#include <rfl/Description.hpp>
#include <rfl/Field.hpp>
#include <rfl/Flatten.hpp>
#include <rfl/Literal.hpp>
#include <rfl/Ref.hpp>
#include <rfl/Rename.hpp>
#include <rfl/Result.hpp>
#include <rfl/Skip.hpp>
#include <rfl/TaggedUnion.hpp>
#include <rfl/Validator.hpp>
#include <rfl/Variant.hpp>

#include <chrono>
#include <concepts>
#include <cstdint>
#include <deque>
#include <format>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace tagged_union_test
{
namespace
{

///
/// Version 1 of a shape, which names its size r.
///
struct shape_v1
{
  ///
  /// The version this type is.
  ///
  static constexpr auto config_version = arude::config::version_t{1};

  ///
  /// The size, as version 1 names it.
  ///
  std::int32_t r = 1;
};

///
/// Version 2 of a shape, which names its size radius.
///
struct shape_v2
{
  ///
  /// The version before this one.
  ///
  using previous_t = shape_v1;

  ///
  /// \see shape_v1::config_version
  ///
  static constexpr auto config_version = arude::config::version_t{2};

  ///
  /// Compares two shapes field by field.
  /// \param other The shape to compare against.
  /// \return true if every field is equal.
  ///
  [[nodiscard]] auto operator==(const shape_v2& other) const -> bool = default;

  ///
  /// The size, as version 2 names it.
  ///
  std::int32_t radius = 2;
};

///
/// Upgrades a shape from version 1 to version 2.
/// \param val The version 1 shape. Not retained.
/// \return The same shape as version 2.
///
[[nodiscard]] auto upgrade(const shape_v1& val) -> shape_v2
{
  return shape_v2{.radius = val.r};
}

///
/// Downgrades a shape from version 2 to version 1.
/// \param val The version 2 shape. Not retained.
/// \return The same shape as version 1.
///
[[nodiscard]] auto downgrade(const shape_v2& val) -> shape_v1
{
  return shape_v1{.r = val.radius};
}

///
/// An alternative with an explicit tag field and a versioned member.
///
struct circle
{
  ///
  /// Compares two circles field by field.
  /// \param other The circle to compare against.
  /// \return true if every field is equal.
  ///
  [[nodiscard]] auto operator==(const circle& other) const -> bool = default;

  ///
  /// The tag.
  ///
  rfl::Literal<"circle"> kind;

  ///
  /// The versioned member.
  ///
  arude::config::versioned<shape_v2> shape;

  ///
  /// A colour.
  ///
  std::string colour = "red";
};

///
/// An alternative with an explicit tag field and nothing versioned.
///
struct square
{
  ///
  /// Compares two squares field by field.
  /// \param other The square to compare against.
  /// \return true if every field is equal.
  ///
  [[nodiscard]] auto operator==(const square& other) const -> bool = default;

  ///
  /// The tag.
  ///
  rfl::Literal<"square"> kind;

  ///
  /// The side.
  ///
  std::int32_t side = 3;
};

///
/// The tagged union, circle first.
///
using outline_t = rfl::TaggedUnion<"kind", circle, square>;

///
/// The same alternatives, square first.
///
using reversed_outline_t = rfl::TaggedUnion<"kind", square, circle>;

///
/// A root holding one tagged union.
/// \tparam Outline The tagged union, in either order.
///
template<typename Outline>
struct basic_drawing
{
  ///
  /// The version this type is.
  ///
  static constexpr auto config_version = arude::config::version_t{1};

  ///
  /// Compares two drawings field by field.
  /// \param other The drawing to compare against.
  /// \return true if every field is equal.
  ///
  [[nodiscard]] auto operator==(const basic_drawing& other) const -> bool = default;

  ///
  /// The outline.
  ///
  Outline outline = square{};
};

///
/// The root with circle first.
///
using drawing_t = basic_drawing<outline_t>;

///
/// The root with square first.
///
using reversed_drawing_t = basic_drawing<reversed_outline_t>;

///
/// A root holding tagged unions in a vector.
///
struct gallery
{
  ///
  /// The version this type is.
  ///
  static constexpr auto config_version = arude::config::version_t{1};

  ///
  /// The outlines.
  ///
  std::vector<outline_t> outlines;
};

///
/// An alternative tagged by its type name, holding a versioned member.
///
struct dot
{
  ///
  /// Compares two dots field by field.
  /// \param other The dot to compare against.
  /// \return true if every field is equal.
  ///
  [[nodiscard]] auto operator==(const dot& other) const -> bool = default;

  ///
  /// The versioned member.
  ///
  arude::config::versioned<shape_v2> shape;
};

///
/// An alternative tagged by its type name, holding nothing versioned.
///
struct dash
{
  ///
  /// Compares two dashs field by field.
  /// \param other The dash to compare against.
  /// \return true if every field is equal.
  ///
  [[nodiscard]] auto operator==(const dash& other) const -> bool = default;

  ///
  /// The length.
  ///
  std::int32_t length = 4;
};

///
/// A root holding a tagged union whose alternatives declare no tag field.
///
struct sketch
{
  ///
  /// The version this type is.
  ///
  static constexpr auto config_version = arude::config::version_t{1};

  ///
  /// Compares two sketches field by field.
  /// \param other The sketch to compare against.
  /// \return true if every field is equal.
  ///
  [[nodiscard]] auto operator==(const sketch& other) const -> bool = default;

  ///
  /// The stroke.
  ///
  rfl::TaggedUnion<"kind", dot, dash> stroke = dash{};
};

///
/// A struct holding a versioned member, for the variant checks below.
///
struct holder
{
  ///
  /// The versioned member.
  ///
  arude::config::versioned<shape_v2> shape;
};

///
/// A struct holding nothing versioned, for the variant checks below.
///
struct plain
{
  ///
  /// A count.
  ///
  std::int32_t count = 1;
};

///
/// A struct holding a C array of alternatives with a versioned member, for
/// the rule checks below.
///
struct carr
{
  ///
  /// The array.
  ///
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays): the searches must see in.
  holder h[1];
};

///
/// A class reflect-cpp reads through an rfl::Reflector specialisation, whose
/// ReflType is a variant over a versioned member. The searches must see through
/// the Reflector.
///
class wrapped_through_reflector
{
public:
  ///
  /// Defaults the variant to plain.
  ///
  wrapped_through_reflector() = default;

private:
  ///
  /// The variant.
  ///
  std::variant<holder, plain> picked_ = plain{};
};

///
/// A class that names both Type and ReflectionType, where reflect-cpp's
/// default parser reads the ReflectionType. The searches must follow the
/// ReflectionType, not the Type.
///
class both_names
{
public:
  ///
  /// Not what reflect-cpp reads; there to tempt the searches into following it.
  ///
  // NOLINTNEXTLINE(readability-identifier-naming): reflect-cpp's spelling.
  using Type = std::int32_t;

  ///
  /// What reflect-cpp's default parser reads.
  ///
  // NOLINTNEXTLINE(readability-identifier-naming): reflect-cpp's spelling.
  using ReflectionType = std::variant<holder, plain>;

  ///
  /// Defaults the class.
  ///
  both_names() = default;
};

///
/// A class read through its ReflectionType, which holds nothing versioned:
/// the searches must see through it and allow it.
///
class via_reflection_type
{
public:
  ///
  /// What reflect-cpp's default parser reads.
  ///
  // NOLINTNEXTLINE(readability-identifier-naming): reflect-cpp's spelling.
  using ReflectionType = plain;

  ///
  /// Defaults the class.
  ///
  via_reflection_type() = default;
};

///
/// A root holding an rfl::Result of an alternative with a versioned member,
/// which the rule must refuse as an untagged variant.
///
struct result_drawing
{
  ///
  /// The version this type is.
  ///
  static constexpr auto config_version = arude::config::version_t{1};

  ///
  /// The result.
  ///
  rfl::Result<holder> result = rfl::error("none");
};

///
/// A recursive struct through rfl::Ref: the searches must terminate instead of
/// instantiating itself forever, and find nothing banned.
///
struct tree
{
  ///
  /// A count.
  ///
  std::int32_t count = 0;

  ///
  /// The next node, or none.
  ///
  std::optional<rfl::Ref<tree>> next;
};

///
/// A recursive struct that holds an untagged variant: the searches must still see the
/// variant through the recursion.
///
struct node
{
  ///
  /// The variant over a versioned member.
  ///
  std::variant<holder, plain> pick = plain{};

  ///
  /// The next node, or none.
  ///
  std::optional<rfl::Ref<node>> next;
};

///
/// A root holding a recursive tree, for the round trip below.
///
struct grove
{
  ///
  /// The version this type is.
  ///
  static constexpr auto config_version = arude::config::version_t{1};

  ///
  /// The tree.
  ///
  tree trunk;
};

} // namespace

namespace
{

///
/// A versioned configuration holding an untagged variant whose first
/// alternative holds a versioned member.
///
struct choice
{
  ///
  /// The version this type is.
  ///
  static constexpr auto config_version = arude::config::version_t{1};

  ///
  /// The variant.
  ///
  std::variant<holder, plain> picked = plain{};
};

///
/// A validator that accepts everything, for the wrapper checks below.
///
struct always_ok
{
  ///
  /// Accepts the value unchanged.
  /// \tparam T The value's type.
  /// \param value The value. Not retained.
  /// \return The value.
  ///
  template<typename T>
  static auto validate(T value) -> rfl::Result<T>
  {
    return value;
  }

  ///
  /// A schema, which these checks never ask for.
  /// \tparam T The value's type.
  /// \return A placeholder.
  ///
  template<typename T>
  static auto to_schema() -> int
  {
    return 0;
  }
};

///
/// An untagged variant whose alternative is a wrapper around a versioned
/// member, one wrapper per type. Each is a type the searches must see through.
///
using wrapped_default_t = std::variant<rfl::DefaultVal<holder>, plain>;
///
/// \see wrapped_default_t
///
using wrapped_field_t = std::variant<rfl::Field<"v", holder>, plain>;
///
/// \see wrapped_default_t
///
using wrapped_flatten_t = std::variant<rfl::Flatten<holder>, plain>;
///
/// \see wrapped_default_t
///
using wrapped_skip_t = std::variant<rfl::Skip<holder>, plain>;
///
/// \see wrapped_default_t
///
using wrapped_skip_serialization_t = std::variant<rfl::SkipSerialization<holder>, plain>;
///
/// \see wrapped_default_t
///
using wrapped_skip_deserialization_t = std::variant<rfl::SkipDeserialization<holder>, plain>;
///
/// \see wrapped_default_t
///
using wrapped_result_t = std::variant<rfl::Result<holder>, plain>;
///
/// \see wrapped_default_t
///
using wrapped_validator_t = std::variant<rfl::Validator<holder, always_ok>, plain>;
///
/// \see wrapped_default_t
///
using wrapped_description_t = std::variant<rfl::Description<"d", holder>, plain>;
///
/// \see wrapped_default_t
///
using wrapped_attribute_t = std::variant<rfl::Attribute<holder>, plain>;

///
/// A root holding a wrapper around a versioned member in an untagged variant.
/// \tparam Variant The variant, which the rule must refuse.
///
template<typename Variant>
struct wrapped_drawing
{
  ///
  /// The version this type is.
  ///
  static constexpr auto config_version = arude::config::version_t{1};

  ///
  /// The variant.
  ///
  Variant picked = plain{};
};

///
/// A drawing whose variant holds a versioned member behind rfl::DefaultVal.
///
using wrapped_default_drawing_t = wrapped_drawing<wrapped_default_t>;

///
/// A drawing whose outline is a circle, its shape at the version given.
/// \param shape_table The [outline.shape] table's body.
/// \return The text.
///
[[nodiscard]] auto circle_text(const std::string_view shape_table) -> std::string
{
  return std::format("version = 1\n[outline]\nkind = \"circle\"\ncolour = \"blue\"\n[outline.shape]\n{}", shape_table);
}

///
/// Runs a call that is expected to throw, and returns what it threw.
/// \tparam E The exception type expected.
/// \param call The call. Not retained.
/// \return The exception the call threw.
/// \throws std::logic_error If the call returns instead, which fails the scenario.
///
template<typename E>
[[nodiscard]] auto failure_of(const std::invocable auto& call) -> E
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

} // namespace

} // namespace tagged_union_test

///
/// Reads and writes tagged_union_test::wrapped_through_reflector as a
/// std::variant over holder and plain, the way a user's Reflector would.
///
template<>
struct rfl::Reflector<tagged_union_test::wrapped_through_reflector>
{
  ///
  /// The type reflect-cpp reads and writes the class as.
  ///
  // NOLINTNEXTLINE(readability-identifier-naming): reflect-cpp's spelling.
  using ReflType = std::variant<tagged_union_test::holder, tagged_union_test::plain>;

  ///
  /// Builds the class from the reflected type.
  /// \param value The value. Not retained.
  /// \return The class.
  ///
  static auto to(const ReflType& value) -> tagged_union_test::wrapped_through_reflector
  {
    std::ignore = value;
    return tagged_union_test::wrapped_through_reflector{};
  }

  ///
  /// Reflects the class as the reflected type.
  /// \param value The class. Not retained.
  /// \return The value as the reflected type.
  ///
  static auto from(const tagged_union_test::wrapped_through_reflector& value) -> ReflType
  {
    std::ignore = value;
    return tagged_union_test::plain{};
  }
};

SCENARIO(
  "the coarse rule refuses an untagged variant only beside a versioned member or a tagged union",
  "[config][tagged_union]")
{
  GIVEN("the two traits that read_root() and write_root() refuse a type through")
  {
    using arude::config::detail::contains_untagged_variant;
    using arude::config::detail::contains_versioned_or_tagged;

    // A type containing versioned<> or a TaggedUnion must not also contain
    // an untagged variant. Each refused shape below checks the conjunction.
    WHEN("a tagged union's alternatives hold versioned members, bare or in a vector")
    {
      THEN("the type carries a versioned or tagged thing, and no untagged variant")
      {
        STATIC_REQUIRE(contains_versioned_or_tagged<tagged_union_test::outline_t>::value);
        STATIC_REQUIRE(contains_versioned_or_tagged<tagged_union_test::drawing_t>::value);
        STATIC_REQUIRE(contains_versioned_or_tagged<tagged_union_test::gallery>::value);
        STATIC_REQUIRE_FALSE(contains_untagged_variant<tagged_union_test::outline_t>::value);
      }
    }

    WHEN("a versioned<> member and a std::variant sit in one type, through containers, wrappers and C arrays")
    {
      THEN("the conjunction holds, so the type is refused")
      {
        using variant_t = std::variant<tagged_union_test::holder, tagged_union_test::plain>;

        STATIC_REQUIRE(contains_versioned_or_tagged<arude::config::versioned<tagged_union_test::choice>>::value);
        STATIC_REQUIRE(contains_untagged_variant<arude::config::versioned<tagged_union_test::choice>>::value);
        STATIC_REQUIRE(
          contains_untagged_variant<rfl::Variant<tagged_union_test::holder, tagged_union_test::plain>>::value);
        STATIC_REQUIRE(
          contains_versioned_or_tagged<rfl::Variant<tagged_union_test::holder, tagged_union_test::plain>>::value);
        STATIC_REQUIRE(contains_versioned_or_tagged<std::deque<variant_t>>::value);
        STATIC_REQUIRE(contains_untagged_variant<std::deque<variant_t>>::value);
        STATIC_REQUIRE(contains_untagged_variant<std::list<variant_t>>::value);
        STATIC_REQUIRE(contains_untagged_variant<std::set<std::optional<variant_t>>>::value);
        STATIC_REQUIRE(contains_untagged_variant<std::multimap<std::string, variant_t>>::value);
        STATIC_REQUIRE(contains_untagged_variant<std::unique_ptr<variant_t>>::value);
        STATIC_REQUIRE(contains_untagged_variant<std::pair<std::int32_t, variant_t>>::value);
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays): the rule must see in.
        STATIC_REQUIRE(contains_untagged_variant<variant_t[1]>::value);
        STATIC_REQUIRE(
          contains_untagged_variant<std::variant<tagged_union_test::plain, tagged_union_test::carr>>::value);
        STATIC_REQUIRE(contains_untagged_variant<variant_t*>::value);
      }
    }

    WHEN("a C array, an rfl::Reflector or a ReflectionType hides a banned variant")
    {
      THEN("the search sees through whatever reflect-cpp reads, so the conjunction holds")
      {
        STATIC_REQUIRE(
          contains_untagged_variant<
            std::variant<tagged_union_test::plain, tagged_union_test::wrapped_through_reflector>>::value);
        STATIC_REQUIRE(
          contains_versioned_or_tagged<
            std::variant<tagged_union_test::plain, tagged_union_test::wrapped_through_reflector>>::value);
        STATIC_REQUIRE(
          contains_untagged_variant<std::variant<tagged_union_test::plain, tagged_union_test::both_names>>::value);
        STATIC_REQUIRE_FALSE(
          contains_versioned_or_tagged<
            std::variant<tagged_union_test::plain, tagged_union_test::via_reflection_type>>::value);
      }
    }

    WHEN("an rfl::Result of anything versioned or tagged is read or written")
    {
      THEN("the conjunction holds, as reflect-cpp reads a Result as an untagged variant")
      {
        STATIC_REQUIRE(contains_versioned_or_tagged<rfl::Result<tagged_union_test::holder>>::value);
        STATIC_REQUIRE(contains_versioned_or_tagged<rfl::Result<tagged_union_test::outline_t>>::value);
        STATIC_REQUIRE(contains_versioned_or_tagged<tagged_union_test::result_drawing>::value);
        STATIC_REQUIRE_FALSE(contains_versioned_or_tagged<rfl::Result<tagged_union_test::plain>>::value);
      }
    }

    WHEN("a recursive struct holds an untagged variant beside a versioned member")
    {
      THEN("the search terminates, and the conjunction holds through the recursion")
      {
        STATIC_REQUIRE_FALSE(contains_versioned_or_tagged<tagged_union_test::tree>::value);
        STATIC_REQUIRE_FALSE(contains_untagged_variant<tagged_union_test::tree>::value);
        STATIC_REQUIRE(contains_untagged_variant<tagged_union_test::node>::value);
        STATIC_REQUIRE(contains_versioned_or_tagged<tagged_union_test::node>::value);
      }
    }

    WHEN("a tagged union sits in an untagged variant alternative, even one with nothing versioned")
    {
      THEN("the conjunction holds, as its unknown tag would count as newer where another alternative reads the table")
      {
        STATIC_REQUIRE(
          contains_versioned_or_tagged<std::variant<tagged_union_test::outline_t, tagged_union_test::plain>>::value);
        STATIC_REQUIRE(
          contains_untagged_variant<
            std::variant<tagged_union_test::plain, std::vector<rfl::TaggedUnion<"kind", tagged_union_test::square>>>>::
            value);
      }
    }

    WHEN("a reflect-cpp wrapper around a versioned member sits in an untagged variant alternative")
    {
      THEN("it is refused, whatever the wrapper calls its inner type")
      {
        STATIC_REQUIRE(contains_versioned_or_tagged<tagged_union_test::wrapped_default_t>::value);
        STATIC_REQUIRE(contains_versioned_or_tagged<tagged_union_test::wrapped_field_t>::value);
        STATIC_REQUIRE(contains_versioned_or_tagged<tagged_union_test::wrapped_flatten_t>::value);
        STATIC_REQUIRE(contains_versioned_or_tagged<tagged_union_test::wrapped_skip_t>::value);
        STATIC_REQUIRE(contains_versioned_or_tagged<tagged_union_test::wrapped_skip_serialization_t>::value);
        STATIC_REQUIRE(contains_versioned_or_tagged<tagged_union_test::wrapped_skip_deserialization_t>::value);
        STATIC_REQUIRE(contains_versioned_or_tagged<tagged_union_test::wrapped_result_t>::value);
        STATIC_REQUIRE(contains_versioned_or_tagged<tagged_union_test::wrapped_validator_t>::value);
        STATIC_REQUIRE(contains_versioned_or_tagged<tagged_union_test::wrapped_description_t>::value);
        STATIC_REQUIRE(contains_versioned_or_tagged<tagged_union_test::wrapped_attribute_t>::value);
        STATIC_REQUIRE(contains_versioned_or_tagged<tagged_union_test::wrapped_default_drawing_t>::value);
      }
    }

    WHEN("a wrapper around nothing versioned sits in an untagged variant alternative")
    {
      THEN("it is allowed, so the rule is not simply every wrapper")
      {
        STATIC_REQUIRE_FALSE(
          contains_versioned_or_tagged<std::variant<rfl::DefaultVal<tagged_union_test::plain>, std::int32_t>>::value);
        STATIC_REQUIRE_FALSE(
          contains_versioned_or_tagged<std::variant<rfl::Field<"v", tagged_union_test::plain>, std::int32_t>>::value);
        STATIC_REQUIRE_FALSE(
          contains_versioned_or_tagged<std::variant<rfl::Skip<tagged_union_test::plain>, std::int32_t>>::value);
      }
    }

    WHEN("a wrapper around a tagged union sits in an untagged variant alternative")
    {
      THEN("the conjunction holds, as the wrapper is seen through to the tagged union")
      {
        STATIC_REQUIRE(
          contains_versioned_or_tagged<
            std::variant<rfl::SkipSerialization<tagged_union_test::outline_t>, tagged_union_test::plain>>::value);
        STATIC_REQUIRE(
          contains_versioned_or_tagged<
            std::variant<rfl::DefaultVal<tagged_union_test::outline_t>, tagged_union_test::plain>>::value);
      }
    }

    WHEN("a variant of plain types, chrono types included, carries no versioned<> anywhere")
    {
      THEN("it is allowed, whatever the alternative")
      {
        STATIC_REQUIRE_FALSE(contains_versioned_or_tagged<std::variant<tagged_union_test::plain, std::int32_t>>::value);
        STATIC_REQUIRE_FALSE(contains_versioned_or_tagged<std::variant<std::chrono::seconds, std::string>>::value);
        STATIC_REQUIRE_FALSE(
          contains_versioned_or_tagged<std::variant<std::chrono::sys_time<std::chrono::seconds>, std::string>>::value);
        STATIC_REQUIRE_FALSE(
          contains_versioned_or_tagged<std::variant<std::chrono::year_month_day, std::string>>::value);
        STATIC_REQUIRE_FALSE(
          contains_versioned_or_tagged<std::variant<std::chrono::hh_mm_ss<std::chrono::seconds>, std::string>>::value);
        STATIC_REQUIRE(contains_untagged_variant<std::variant<std::chrono::seconds, std::string>>::value);
      }
    }
  }
}

SCENARIO("a versioned member of a tagged union alternative keeps the version rules", "[config][tagged_union]")
{
  GIVEN("a store and a file over a root holding a tagged union")
  {
    auto store = config_test::memory_transport::store_t{};

    auto file = arude::config::config_file<tagged_union_test::drawing_t, config_test::memory_transport>{
      config_test::memory_transport{"app", store}
    };

    WHEN("the circle's shape is at its current version")
    {
      store["app"] = arude::config::as_bytes(tagged_union_test::circle_text("version = 2\nradius = 9\n"));
      file.load();

      THEN("it reads as the circle the tag names")
      {
        const auto drawing = file.get();
        const auto* const held = rfl::get_if<tagged_union_test::circle>(&drawing.outline.variant());

        REQUIRE(held != nullptr);
        REQUIRE(held->shape.value().radius == 9);
        REQUIRE(held->colour == "blue");
      }
    }

    WHEN("the circle's shape is at an older version")
    {
      store["app"] = arude::config::as_bytes(tagged_union_test::circle_text("version = 1\nr = 4\n"));
      file.load();

      THEN("it is migrated to the current one")
      {
        const auto drawing = file.get();
        const auto* const held = rfl::get_if<tagged_union_test::circle>(&drawing.outline.variant());

        REQUIRE(held != nullptr);
        REQUIRE(held->shape.value().radius == 4);
      }
    }

    WHEN("the circle's shape is at a newer version")
    {
      file.set(tagged_union_test::drawing_t{});
      store["app"] = arude::config::as_bytes(tagged_union_test::circle_text("version = 3\nradius = 9\n"));
      const auto before = store.at("app");

      const auto load_failure =
        tagged_union_test::failure_of<arude::config::exception_t>([&file] -> void { file.load(); });
      const auto store_failure =
        tagged_union_test::failure_of<arude::config::exception_t>([&file] -> void { file.store(); });

      THEN("the load is refused, and so is the store, which leaves the source untouched")
      {
        REQUIRE(load_failure.data() == arude::config::config_error::invalid_version);
        REQUIRE(load_failure.str().contains("is version 3, newer than version 2"));
        REQUIRE(store_failure.data() == arude::config::config_error::invalid_version);
        REQUIRE(store_failure.str().contains("which a newer build wrote"));
        REQUIRE(store.at("app") == before);
      }
    }
  }
}

SCENARIO("a tagged union's tag picks the alternative whatever their order", "[config][tagged_union]")
{
  GIVEN("a circle and a square, as text")
  {
    const auto circle = tagged_union_test::circle_text("version = 2\nradius = 9\n");
    const auto square = std::string{"version = 1\n[outline]\nkind = \"square\"\nside = 5\n"};

    WHEN("each is read with circle first and with square first")
    {
      const auto circle_first = arude::config::from_toml<tagged_union_test::drawing_t>(circle);
      const auto circle_second = arude::config::from_toml<tagged_union_test::reversed_drawing_t>(circle);
      const auto square_first = arude::config::from_toml<tagged_union_test::reversed_drawing_t>(square);
      const auto square_second = arude::config::from_toml<tagged_union_test::drawing_t>(square);

      THEN("each reads as the alternative its tag names")
      {
        REQUIRE(rfl::get_if<tagged_union_test::circle>(&circle_first.outline.variant())->shape.value().radius == 9);
        REQUIRE(rfl::get_if<tagged_union_test::circle>(&circle_second.outline.variant())->shape.value().radius == 9);
        REQUIRE(rfl::get_if<tagged_union_test::square>(&square_first.outline.variant())->side == 5);
        REQUIRE(rfl::get_if<tagged_union_test::square>(&square_second.outline.variant())->side == 5);
      }
    }

    WHEN("a circle is written and read back")
    {
      auto value = tagged_union_test::drawing_t{};
      value.outline = tagged_union_test::circle{.shape = tagged_union_test::shape_v2{.radius = 6}, .colour = "green"};
      const auto text = arude::config::to_toml(value);

      THEN("the text names it by its tag, and reads back as it")
      {
        REQUIRE(text.contains("kind = 'circle'"));
        REQUIRE(arude::config::from_toml<tagged_union_test::drawing_t>(text) == value);
      }
    }
  }
}

SCENARIO("a tagged union without its tag is an invalid_payload a store replaces", "[config][tagged_union]")
{
  GIVEN("a file with a cached value, over a source whose tagged union has no tag")
  {
    auto store = config_test::memory_transport::store_t{};
    store["app"] = arude::config::as_bytes(std::string{"version = 1\n[outline]\nside = 5\n"});

    auto file = arude::config::config_file<tagged_union_test::drawing_t, config_test::memory_transport>{
      config_test::memory_transport{"app", store}
    };

    auto cached = tagged_union_test::drawing_t{};
    cached.outline = tagged_union_test::square{.side = 8};
    file.set(cached);

    WHEN("it is loaded")
    {
      const auto failure = tagged_union_test::failure_of<arude::config::exception_t>([&file] -> void { file.load(); });

      THEN("it is an invalid_payload naming the missing tag, and the cache is untouched")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
        REQUIRE(failure.str().contains("has no field 'kind'"));
        REQUIRE(file.get() == cached);
      }
    }

    WHEN("it is stored over")
    {
      file.store();

      THEN("the source is replaced by the cached value, which loads back")
      {
        auto second = arude::config::config_file<tagged_union_test::drawing_t, config_test::memory_transport>{
          config_test::memory_transport{"app", store}
        };
        second.load();

        REQUIRE(second.get() == cached);
      }
    }
  }
}

SCENARIO("a tagged union whose tag is no string, or which is no table, is an invalid_payload", "[config][tagged_union]")
{
  GIVEN("a file with a cached value")
  {
    auto store = config_test::memory_transport::store_t{};

    auto file = arude::config::config_file<tagged_union_test::drawing_t, config_test::memory_transport>{
      config_test::memory_transport{"app", store}
    };

    auto cached = tagged_union_test::drawing_t{};
    cached.outline = tagged_union_test::square{.side = 8};
    file.set(cached);

    WHEN("the source's tag is not a string")
    {
      store["app"] = arude::config::as_bytes(std::string{"version = 1\n[outline]\nkind = 3\nside = 5\n"});
      const auto failure = tagged_union_test::failure_of<arude::config::exception_t>([&file] -> void { file.load(); });
      file.store();

      THEN("the load is an invalid_payload saying so, and the store replaces the source")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
        REQUIRE(failure.str().contains("field 'kind', which names its alternative, is not a string"));
        REQUIRE(
          arude::config::from_toml<tagged_union_test::drawing_t>(arude::config::as_text(store.at("app"))) == cached);
      }
    }

    WHEN("the source's tagged union is not a table")
    {
      store["app"] = arude::config::as_bytes(std::string{"version = 1\noutline = 5\n"});
      const auto failure = tagged_union_test::failure_of<arude::config::exception_t>([&file] -> void { file.load(); });
      file.store();

      THEN("the load is an invalid_payload saying so, and the store replaces the source")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
        REQUIRE(failure.str().contains("a tagged union is held in a table"));
        REQUIRE(
          arude::config::from_toml<tagged_union_test::drawing_t>(arude::config::as_text(store.at("app"))) == cached);
      }
    }
  }
}

SCENARIO("a tag no alternative answers to is taken for newer data", "[config][tagged_union]")
{
  GIVEN("a file with a cached value, over a source whose tagged union has an unknown tag")
  {
    auto store = config_test::memory_transport::store_t{};
    store["app"] = arude::config::as_bytes(std::string{"version = 1\n[outline]\nkind = \"triangle\"\nedges = 3\n"});
    const auto before = store.at("app");

    auto file = arude::config::config_file<tagged_union_test::drawing_t, config_test::memory_transport>{
      config_test::memory_transport{"app", store}
    };
    file.set(tagged_union_test::drawing_t{});

    WHEN("it is loaded")
    {
      const auto failure = tagged_union_test::failure_of<arude::config::exception_t>([&file] -> void { file.load(); });

      THEN("it is an invalid_version naming the tag and the tags this build knows")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_version);
        REQUIRE(failure.str().contains("kind 'triangle' names no alternative this build knows"));
        REQUIRE(failure.str().contains("'circle', 'square'"));
      }
    }

    WHEN("it is stored over")
    {
      const auto failure = tagged_union_test::failure_of<arude::config::exception_t>([&file] -> void { file.store(); });

      THEN("the store is refused as an invalid_version, and the source is untouched")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_version);
        REQUIRE(failure.str().contains("which a newer build wrote"));
        REQUIRE(failure.str().contains("'triangle'"));
        REQUIRE(store.at("app") == before);
      }
    }
  }
}

SCENARIO("a store probe reads every tagged union of an array", "[config][tagged_union]")
{
  GIVEN("a file with a cached value, over a source holding tagged unions in an array")
  {
    auto store = config_test::memory_transport::store_t{};

    auto file = arude::config::config_file<tagged_union_test::gallery, config_test::memory_transport>{
      config_test::memory_transport{"app", store}
    };
    file.set(tagged_union_test::gallery{});

    WHEN("an element that does not read comes before one whose shape is newer")
    {
      store["app"] = arude::config::as_bytes(
        std::string{"version = 1\n"
                    "[[outlines]]\nkind = \"square\"\nside = \"wide\"\n"
                    "[[outlines]]\nkind = \"circle\"\ncolour = \"red\"\n[outlines.shape]\nversion = 3\nradius = 9\n"});
      const auto before = store.at("app");

      const auto failure = tagged_union_test::failure_of<arude::config::exception_t>([&file] -> void { file.store(); });

      THEN("the store is refused as an invalid_version, and the source is untouched")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_version);
        REQUIRE(failure.str().contains("is version 3, newer than version 2"));
        REQUIRE(store.at("app") == before);
      }
    }

    WHEN("an element that does not read comes before one with an unknown tag")
    {
      store["app"] = arude::config::as_bytes(
        std::string{"version = 1\n"
                    "[[outlines]]\nkind = \"square\"\nside = \"wide\"\n"
                    "[[outlines]]\nkind = \"triangle\"\nedges = 3\n"});
      const auto before = store.at("app");

      const auto failure = tagged_union_test::failure_of<arude::config::exception_t>([&file] -> void { file.store(); });

      THEN("the store is refused as an invalid_version, and the source is untouched")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_version);
        REQUIRE(failure.str().contains("'triangle'"));
        REQUIRE(store.at("app") == before);
      }
    }
  }
}

SCENARIO("a tagged union whose alternatives declare no tag is tagged by type name", "[config][tagged_union]")
{
  GIVEN("a root whose tagged union holds a dot, and a strict and a lenient file over one store")
  {
    auto store = config_test::memory_transport::store_t{};

    auto lenient = arude::config::config_file<tagged_union_test::sketch, config_test::memory_transport>{
      config_test::memory_transport{"app", store}
    };

    auto strict = arude::config::config_file<tagged_union_test::sketch, config_test::memory_transport>{
      config_test::memory_transport{"app", store},
      arude::config::toml_format{.reject_unknown_fields = true}
    };

    auto value = tagged_union_test::sketch{};
    value.stroke = tagged_union_test::dot{.shape = tagged_union_test::shape_v2{.radius = 5}};
    lenient.set(value);
    lenient.overwrite();

    WHEN("the lenient file loads what it stored")
    {
      lenient.load();

      THEN("the dot reads back, its tag written although no field declares it")
      {
        REQUIRE(lenient.get() == value);
        REQUIRE(arude::config::as_text(store.at("app")).contains("kind = "));
      }
    }

    WHEN("the strict file loads it")
    {
      const auto failure =
        tagged_union_test::failure_of<arude::config::exception_t>([&strict] -> void { strict.load(); });

      THEN("the tag is an unknown field, and the load an invalid_payload")
      {
        REQUIRE(failure.data() == arude::config::config_error::invalid_payload);
        REQUIRE(failure.str().contains("'kind' not used"));
      }
    }
  }

  GIVEN("a root whose alternatives declare their tag, and a strict file")
  {
    auto store = config_test::memory_transport::store_t{};
    store["app"] = arude::config::as_bytes(tagged_union_test::circle_text("version = 2\nradius = 9\n"));

    auto strict = arude::config::config_file<tagged_union_test::drawing_t, config_test::memory_transport>{
      config_test::memory_transport{"app", store},
      arude::config::toml_format{.reject_unknown_fields = true}
    };

    WHEN("it is loaded")
    {
      strict.load();

      THEN("the tag is a declared field, and the circle reads")
      {
        REQUIRE(rfl::get_if<tagged_union_test::circle>(&strict.get().outline.variant()) != nullptr);
      }
    }
  }
}

SCENARIO("a recursive struct through rfl::Ref round-trips", "[config][tagged_union]")
{
  GIVEN("a file over a root holding a recursive tree")
  {
    auto store = config_test::memory_transport::store_t{};

    auto file = arude::config::config_file<tagged_union_test::grove, config_test::memory_transport>{
      config_test::memory_transport{"app", store}
    };

    WHEN("a chain of two nodes is stored and loaded back")
    {
      auto value = tagged_union_test::grove{};
      value.trunk.count = 7;
      value.trunk.next = rfl::make_ref<tagged_union_test::tree>(tagged_union_test::tree{.count = 8});
      file.set(value);
      file.overwrite();
      file.load();

      THEN("it reads back the same chain, the searches having let the type compile")
      {
        const auto loaded = file.get();
        REQUIRE(loaded.trunk.count == 7);
        REQUIRE(loaded.trunk.next.has_value());

        if(loaded.trunk.next.has_value())
        {
          REQUIRE(loaded.trunk.next->get()->count == 8);
        }
        else
        {
          FAIL("the stored chain lost its next node");
        }
      }
    }
  }
}
