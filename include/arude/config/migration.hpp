///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// Versioning for configuration types, and the migration between versions.
///
/// A configuration is written once and then read for years, so its type will
/// change while files written by an older build are still on disk. The shape
/// here keeps that manageable:
///
///   - Every version is a separate type in its own header, and never edited
///     again once released: version 1's type stays what version 1 meant.
///   - Each type states its version twice over: as config_version, known at
///     compile time, and as the version data member, which is what ends up in
///     the file. The member is initialised from the constant, so the two
///     cannot drift apart.
///   - Version n names version n-1 as previous_t and declares upgrade() and
///     downgrade() for the step between them. Nothing needs to know about the
///     whole chain, and version n-1 never learns that n exists.
///   - migrate() walks that chain in either direction, one step at a time, at
///     compile time.
///
/// upgrade() and downgrade() are free functions found by argument-dependent
/// lookup, which is what lets version n declare the step out of version n-1
/// without version n-1's header including it. The consequence is that whether
/// a version can be upgraded depends on which headers a translation unit has
/// seen, so include arude/config.hpp, or at least the newest version's header,
/// everywhere rather than picking out an older one.
///
#pragma once

#include <concepts>
#include <cstdint>
#include <type_traits>

namespace arude::config
{

///
/// The version number a configuration carries.
/// Fixed width and unsigned, because it is written to a file and read back by
/// builds that were compiled years apart.
///
using version_t = std::uint32_t;

///
/// A versioned configuration type.
/// An aggregate, so reflect-cpp can reflect its public data members, carrying
/// its version as both a compile-time constant and a data member.
///
/// \tparam T Type to test.
///
template<typename T>
concept configuration_c = std::is_aggregate_v<T> && std::default_initializable<T> && requires(const T& val) {
  { T::config_version } -> std::convertible_to<version_t>;
  { val.version } -> std::convertible_to<version_t>;
};

///
/// A configuration that names the version before it.
/// Version 1 satisfies this nowhere, which is what ends a walk backwards
/// through the chain.
///
/// \tparam T Type to test.
///
template<typename T>
concept has_previous_configuration_c =
  configuration_c<T> && requires { typename T::previous_t; } && configuration_c<typename T::previous_t>;

///
/// A configuration a newer version can be produced from.
/// Satisfied once the header declaring upgrade() for this version is included,
/// and not before.
///
/// \tparam T Type to test.
///
template<typename T>
concept upgradable_configuration_c = configuration_c<T> && requires(const T& val) {
  { upgrade(val) } -> configuration_c;
};

///
/// A configuration an older version can be produced from.
/// \tparam T Type to test.
///
template<typename T>
concept downgradable_configuration_c = configuration_c<T> && requires(const T& val) {
  { downgrade(val) } -> configuration_c;
};

///
/// Returns the version a configuration type declares.
/// consteval rather than constexpr: the version is a property of the type, so
/// asking for it at run time would mean something has gone wrong.
///
/// \tparam T Configuration type.
/// \return The version number.
///
template<configuration_c T>
[[nodiscard]] consteval auto version_of() -> version_t;

///
/// Converts a configuration to another version of itself.
/// Walks the chain one version at a time, upgrading or downgrading according
/// to which of the two versions is newer, and copies the value unchanged when
/// there is nothing to walk. Every step is a plain function call, so a
/// migration is as constexpr as the steps it is made of.
///
/// A missing step is a compile error naming the version it stopped at, rather
/// than a silent partial migration.
///
/// \tparam To Version to end up at.
/// \tparam From Version being migrated, deduced.
/// \param from Configuration to migrate. Not retained.
/// \return The configuration as version To.
///
template<configuration_c To, configuration_c From>
[[nodiscard]] constexpr auto migrate(const From& from) -> To;

///
///
template<configuration_c T>
consteval auto version_of() -> version_t
{
  return T::config_version;
}

///
///
// Recursion is the mechanism, not an oversight: each call is one version step,
// and the static_assert below ends the walk where the chain does.
// NOLINTNEXTLINE(misc-no-recursion)
template<configuration_c To, configuration_c From>
constexpr auto migrate(const From& from) -> To
{
  if constexpr(std::same_as<To, From>)
  {
    return from;
  }
  else if constexpr(version_of<To>() > version_of<From>())
  {
    // The step is asked for before it is taken, so a chain that stops short
    // fails on the message below rather than on whatever the missing call
    // would have said.
    if constexpr(upgradable_configuration_c<From>)
    {
      static_assert(
        version_of<std::remove_cvref_t<decltype(upgrade(from))>>() > version_of<From>(),
        "arude::config::migrate: upgrade() must return a newer version, or the walk would not terminate.");

      return migrate<To>(upgrade(from));
    }
    else
    {
      static_assert(
        upgradable_configuration_c<From>,
        "arude::config::migrate: this version declares no upgrade(). Include the header of the version after it.");

      return To{};
    }
  }
  else
  {
    if constexpr(downgradable_configuration_c<From>)
    {
      static_assert(
        version_of<std::remove_cvref_t<decltype(downgrade(from))>>() < version_of<From>(),
        "arude::config::migrate: downgrade() must return an older version, or the walk would not terminate.");

      return migrate<To>(downgrade(from));
    }
    else
    {
      static_assert(
        downgradable_configuration_c<From>,
        "arude::config::migrate: this version declares no downgrade(). Include the header that declares it.");

      return To{};
    }
  }
}

} // namespace arude::config
