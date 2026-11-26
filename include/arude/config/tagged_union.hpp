///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// rfl::TaggedUnion, read and written through libarude's reader and writer.
///
/// A tagged union is the one variant whose alternatives may hold versioned<>
/// members: its tag names the alternative before anything is parsed, and
/// only that alternative is read, so which version applies never depends on
/// the order of the alternatives. reflect-cpp's own Parser reads it the same
/// way; this one replaces it to tell an unknown tag apart. A tag that names
/// no alternative is what an alternative a newer build added looks like, so
/// it is recorded through read_context::note_newer() as well as failing the
/// read, and store() refuses to write over it. A missing tag names nothing at
/// all, and is an invalid_payload like any other missing field.
///
/// A tagged union inside a std::variant or rfl::Variant alternative, at any
/// depth, is what the configuration support's coarse rule refuses: a type
/// that contains an rfl::TaggedUnion anywhere must not contain an untagged
/// variant anywhere, because reflect-cpp tries the alternatives in turn, so a
/// tag this build does not know would be recorded as newer data even where a
/// later alternative reads the table.
///
/// The tag is best an explicit rfl::Literal field of each alternative,
/// spelling the tag that alternative answers to:
///
/// ```cpp
/// struct circle { rfl::Literal<"circle"> kind; versioned<shape_v2> shape; };
/// struct square { rfl::Literal<"square"> kind; std::int32_t side = 0; };
///
/// rfl::TaggedUnion<"kind", circle, square> outline;
/// ```
///
/// An alternative without one is tagged with its type name, which is then the
/// type's to keep: renaming it changes the file. Such an alternative also
/// does not declare the tag as a field, so a format with
/// reject_unknown_fields refuses every table it is read from.
///
/// The write replicates reflect-cpp's, which adds the tag to an alternative
/// that does not hold one: it names rfl::internal::make_tag() and the
/// rfl::parsing::TaggedUnionWrapper types to do it, both reflect-cpp's
/// implementation.
///
#pragma once

#include "arude/config/common.hpp"
#include "arude/config/toml_reader.hpp"
#include "arude/config/toml_writer.hpp"
#include "arude/config/versioned.hpp"
#include "arude/exception_report.hpp"

#include <rfl/internal/has_fields.hpp>
#include <rfl/internal/make_tag.hpp>
#include <rfl/internal/StringLiteral.hpp>
#include <rfl/internal/tag_t.hpp>
#include <rfl/named_tuple_t.hpp>
#include <rfl/parsing/Parser_base.hpp>
#include <rfl/parsing/schema/Type.hpp>
#include <rfl/parsing/TaggedUnionWrapper.hpp>
#include <rfl/Result.hpp>
#include <rfl/TaggedUnion.hpp>
#include <rfl/visit.hpp>
#include <toml++/toml.hpp>

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace arude::config::detail
{

///
/// The message for a tagged union held in something other than a table.
/// \return The message, without a prefix.
///
[[nodiscard]] auto tagged_union_not_table_message() -> std::string;

///
/// The message for a tagged union whose table lacks its tag, or holds one
/// that is not a string.
/// \param discriminator The name of the tag field.
/// \param present Whether the field is there at all, holding something other
///                than a string.
/// \return The message, without a prefix.
///
[[nodiscard]] auto missing_tag_message(std::string_view discriminator, bool present) -> std::string;

///
/// The message for a tag that names no alternative this build knows.
/// \param discriminator The name of the tag field.
/// \param tag The tag the table holds.
/// \param known The tags of the alternatives this build knows, in order.
/// \return The message, without a prefix.
///
[[nodiscard]] auto unknown_tag_message(
  std::string_view discriminator, std::string_view tag, const std::vector<std::string>& known) -> std::string;

///
/// The message for a selected alternative that does not read.
/// \param discriminator The name of the tag field.
/// \param tag The tag that selected the alternative.
/// \param reason Why the alternative does not read.
/// \return The message, without a prefix.
///
[[nodiscard]] auto alternative_failed_message(
  std::string_view discriminator, std::string_view tag, std::string_view reason) -> std::string;

} // namespace arude::config::detail

namespace rfl::parsing
{

///
/// Reads and writes an rfl::TaggedUnion through libarude's reader and
/// writer. Selected over reflect-cpp's own, which it replaces to record an
/// unknown tag as newer data; see the file documentation.
///
/// \tparam Discriminator The name of the tag field.
/// \tparam Alternatives The alternatives, each a struct.
/// \tparam ProcessorsType The processors the read or write runs under.
///
template<internal::StringLiteral Discriminator, typename... Alternatives, typename ProcessorsType>
struct Parser<
  arude::config::detail::toml_reader,
  arude::config::detail::toml_writer,
  TaggedUnion<Discriminator, Alternatives...>,
  ProcessorsType>
{
  static_assert(
    !(arude::config::detail::is_versioned_v<Alternatives> || ...),
    "arude::config: versioned<> cannot be an rfl::TaggedUnion alternative itself. reflect-cpp reflects each "
    "alternative to find its tag, and versioned<> has no fields to reflect. Make the versioned<> a member of a "
    "struct alternative, beside an rfl::Literal field holding the tag.");

  static_assert(
    !ProcessorsType::no_field_names_, "arude::config: a TOML tagged union is a table, and needs field names.");

public: // Typedefs
  ///
  /// The tagged union read and written.
  ///
  using union_t = TaggedUnion<Discriminator, Alternatives...>;

  ///
  /// The node type reflect-cpp hands the reader.
  ///
  // NOLINTNEXTLINE(readability-identifier-naming): reflect-cpp's spelling.
  using InputVarType = arude::config::detail::toml_reader::InputVarType;

  ///
  /// Whether an alternative declares the tag as a field of its own, which
  /// is then written as it stands.
  /// \tparam Alternative The alternative.
  ///
  template<typename Alternative>
  static constexpr auto holds_tag_v = named_tuple_t<Alternative>::Names::template contains<Discriminator>();

  ///
  /// What an alternative is written as: itself where it holds its tag, and
  /// otherwise reflect-cpp's wrapper adding the tag beside its fields.
  /// \tparam Alternative The alternative.
  ///
  template<typename Alternative>
  using written_t = std::conditional_t<
    holds_tag_v<Alternative>,
    Alternative,
    std::conditional_t<
      internal::has_fields<Alternative>(),
      TaggedUnionWrapperWithFields<Alternative, internal::tag_t<Discriminator, Alternative>, Discriminator>,
      TaggedUnionWrapperNoFields<Alternative, internal::tag_t<Discriminator, Alternative>, Discriminator>>>;

public: // Methods
  ///
  /// Reads a table as the alternative its tag names. Only that alternative
  /// is parsed. A tag that names none is recorded as newer data through the
  /// context's note_newer(), and fails the read with invalid_version; a
  /// missing tag, or one that is not a string, fails it as an
  /// invalid_payload and records nothing.
  ///
  /// \param reader The reader, carrying the context.
  /// \param var The node holding the table.
  /// \return The tagged union, or why the table is not one.
  ///
  [[nodiscard]] static auto read(const arude::config::detail::toml_reader& reader, const InputVarType& var) noexcept
    -> Result<union_t>;

  ///
  /// Writes the alternative held, with its tag.
  /// \tparam P The parent type, as rfl::parsing::Parent spells it.
  /// \param writer The writer.
  /// \param val The tagged union.
  /// \param parent The parent holding the member.
  /// \throws std::bad_alloc If the table cannot grow, as may reflect-cpp's
  ///         writers below it. The versioned<> write it sits under records
  ///         whatever is thrown as a failure.
  ///
  template<typename P>
  static auto write(const arude::config::detail::toml_writer& writer, const union_t& val, const P& parent) -> void;

  ///
  /// Forwards the schema of the alternatives, as written.
  /// \param definitions Schema definitions collected so far.
  /// \return The schema.
  ///
  static auto to_schema(std::map<std::string, schema::Type>* definitions) -> schema::Type;

private: // Methods
  ///
  /// Reads the table as the alternative the tag names, if it names one.
  /// \param reader The reader.
  /// \param tag The tag the table holds.
  /// \param var The node holding the table.
  /// \return The tagged union or why the alternative does not read, or
  ///         std::nullopt where the tag names no alternative.
  ///
  [[nodiscard]] static auto
  read_selected(const arude::config::detail::toml_reader& reader, const std::string& tag, const InputVarType& var)
    -> std::optional<Result<union_t>>;

  ///
  /// Writes one alternative, adding the tag where it holds none.
  /// \tparam Alternative The alternative held.
  /// \tparam P The parent type, as rfl::parsing::Parent spells it.
  /// \param writer The writer.
  /// \param val The alternative.
  /// \param parent The parent holding the member.
  ///
  template<typename Alternative, typename P>
  static auto
  write_alternative(const arude::config::detail::toml_writer& writer, const Alternative& val, const P& parent) -> void;
};

///
///
template<internal::StringLiteral Discriminator, typename... Alternatives, typename ProcessorsType>
auto Parser<
  arude::config::detail::toml_reader,
  arude::config::detail::toml_writer,
  TaggedUnion<Discriminator, Alternatives...>,
  ProcessorsType>::read(const arude::config::detail::toml_reader& reader, const InputVarType& var) noexcept
  -> Result<union_t>
{
  try
  {
    auto* const table = var->as_table();

    if(table == nullptr)
    {
      return error(arude::config::detail::tagged_union_not_table_message());
    }

    auto* const tag_node = table->get(Discriminator.string_view());
    const auto* const tag = tag_node == nullptr ? nullptr : tag_node->as_string();

    if(tag == nullptr)
    {
      return error(arude::config::detail::missing_tag_message(Discriminator.string_view(), tag_node != nullptr));
    }

    auto selected = read_selected(reader, tag->get(), var);

    if(!selected.has_value())
    {
      auto message = arude::config::detail::unknown_tag_message(
        Discriminator.string_view(), tag->get(), possible_tags_t<union_t>::names());
      reader.context().note_newer(message);

      return error(reader.context().fail(arude::config::config_error::invalid_version, std::move(message)));
    }

    return std::move(*selected);
  }
  catch(...)
  {
    // Nothing above is expected to throw, but read() is noexcept and a
    // throwing allocator must not terminate the process.
    return error(arude::exception_message());
  }
}

///
///
template<internal::StringLiteral Discriminator, typename... Alternatives, typename ProcessorsType>
template<typename P>
auto Parser<
  arude::config::detail::toml_reader,
  arude::config::detail::toml_writer,
  TaggedUnion<Discriminator, Alternatives...>,
  ProcessorsType>::write(const arude::config::detail::toml_writer& writer, const union_t& val, const P& parent) -> void
{
  rfl::visit(
    [&writer, &parent](const auto& alternative) -> void { write_alternative(writer, alternative, parent); },
    val.variant());
}

///
///
template<internal::StringLiteral Discriminator, typename... Alternatives, typename ProcessorsType>
auto Parser<
  arude::config::detail::toml_reader,
  arude::config::detail::toml_writer,
  TaggedUnion<Discriminator, Alternatives...>,
  ProcessorsType>::to_schema(std::map<std::string, schema::Type>* const definitions) -> schema::Type
{
  return Parser<
    arude::config::detail::toml_reader,
    arude::config::detail::toml_writer,
    std::variant<written_t<Alternatives>...>,
    ProcessorsType>::to_schema(definitions);
}

///
///
template<internal::StringLiteral Discriminator, typename... Alternatives, typename ProcessorsType>
auto Parser<
  arude::config::detail::toml_reader,
  arude::config::detail::toml_writer,
  TaggedUnion<Discriminator, Alternatives...>,
  ProcessorsType>::
  read_selected(const arude::config::detail::toml_reader& reader, const std::string& tag, const InputVarType& var)
    -> std::optional<Result<union_t>>
{
  auto selected = std::optional<Result<union_t>>{};

  // reflect-cpp refuses two alternatives with one tag, so at most one
  // answers; the others are not parsed at all, which is what keeps a
  // versioned<> member of one from being read as part of another.
  const auto try_alternative = [&reader, &tag, &var, &selected]<typename Alternative>() -> void
  {
    if(selected.has_value() || !internal::tag_t<Discriminator, Alternative>::contains(tag))
    {
      return;
    }

    auto parsed =
      Parser<arude::config::detail::toml_reader, arude::config::detail::toml_writer, Alternative, ProcessorsType>::read(
        reader, var);

    if(parsed.has_value())
    {
      selected.emplace(union_t{std::move(*parsed)});
    }
    else
    {
      selected.emplace(error(
        arude::config::detail::alternative_failed_message(Discriminator.string_view(), tag, parsed.error().what())));
    }
  };

  (try_alternative.template operator()<Alternatives>(), ...);

  return selected;
}

///
///
template<internal::StringLiteral Discriminator, typename... Alternatives, typename ProcessorsType>
template<typename Alternative, typename P>
auto Parser<
  arude::config::detail::toml_reader,
  arude::config::detail::toml_writer,
  TaggedUnion<Discriminator, Alternatives...>,
  ProcessorsType>::
  write_alternative(const arude::config::detail::toml_writer& writer, const Alternative& val, const P& parent) -> void
{
  using parser_t = Parser<
    arude::config::detail::toml_reader,
    arude::config::detail::toml_writer,
    written_t<Alternative>,
    ProcessorsType>;

  if constexpr(holds_tag_v<Alternative>)
  {
    parser_t::write(writer, val, parent);
  }
  else
  {
    parser_t::write(
      writer,
      written_t<Alternative>{.tag = internal::make_tag<Discriminator, Alternative>(val), .fields = &val},
      parent);
  }
}

} // namespace rfl::parsing
