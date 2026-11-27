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
///   - Each type states its version once, as config_version, known at
///     compile time. The constant is not a data member, so reflect-cpp never
///     sees it: arude::config writes it as the table's version key itself and
///     takes the key out of the table again before the rest is read.
///   - Version n names version n-1 as previous_t and declares upgrade() and
///     downgrade() for the step between them. Nothing needs to know about the
///     whole chain, and version n-1 never learns that n exists.
///   - migrate() walks that chain in either direction, one step at a time, at
///     compile time.
///   - previous_t and upgrade() state each step twice, once in each direction,
///     and a walk checks that the two agree for every version it passes: where
///     version n names previous_t and that version declares an upgrade(), the
///     upgrade() has to return version n; and where version n declares an
///     upgrade() whose result names previous_t, that previous_t has to be
///     version n. Otherwise the walk does not compile.
///
/// upgrade() and downgrade() are free functions found by argument-dependent
/// lookup, which is what lets version n declare the step out of version n-1
/// without version n-1's header including it. The consequence is that whether
/// a version can be upgraded depends on which headers a translation unit has
/// seen, so include the newest version's header everywhere the configuration
/// is used rather than picking out an older one. That header reaches every
/// older one, as each version includes the one it names as previous_t;
/// arude/config.hpp is the library's and includes none of them.
///
#pragma once

#include <concepts>
#include <cstdint>
#include <type_traits>
#include <utility>

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
/// its version as a compile-time constant, config_version. The file's version
/// key is written and read by arude::config from that constant.
///
/// \tparam T Type to test.
///
template<typename T>
concept configuration_c = std::is_aggregate_v<T> && std::default_initializable<T> && requires {
  { T::config_version } -> std::convertible_to<version_t>;
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
/// than a silent partial migration. So is a version passed on the way whose
/// previous_t and upgrade() disagree; see detail::upgrade_matches_previous()
/// and detail::upgrade_matches_next().
///
/// \tparam To Version to end up at.
/// \tparam From Version being migrated, deduced.
/// \param from Configuration to migrate. Not retained.
/// \return The configuration as version To.
///
template<configuration_c To, configuration_c From>
[[nodiscard]] constexpr auto migrate(const From& from) -> To;

} // namespace arude::config

namespace arude::config::detail
{

///
/// Reports whether a version and the one before it describe the step between
/// them alike. previous_t walks the chain backwards and upgrade() walks it
/// forwards, and nothing else ties the two together: a version could name a
/// previous_t whose upgrade() leads to another version, and a walk in one
/// direction would then take a step that a walk in the other never does.
///
/// A version that names no previous_t, or whose previous_t declares no
/// upgrade() in this translation unit, has nothing to disagree with.
///
/// \tparam T Configuration type.
/// \return false if T names a previous_t whose upgrade() returns anything but
///         T, true otherwise.
///
template<configuration_c T>
[[nodiscard]] consteval auto upgrade_matches_previous() -> bool;

///
/// Reports whether a version's upgrade() leads to the version that names it as
/// previous_t. The counterpart of upgrade_matches_previous(), checked from the
/// version the step starts at: a walk upwards that passes a version only by
/// way of its upgrade() result never visits the version in between, so a step
/// that skips one has to be caught where it is taken.
///
/// A version that declares no upgrade() in this translation unit, or whose
/// upgrade() returns a version naming no previous_t, has nothing to disagree
/// with.
///
/// \tparam T Configuration type.
/// \return false if upgrade() on T returns a version whose previous_t is not
///         T, true otherwise.
///
template<configuration_c T>
[[nodiscard]] consteval auto upgrade_matches_next() -> bool;

///
///
template<configuration_c T>
consteval auto upgrade_matches_previous() -> bool
{
  // Nested rather than joined with &&: T::previous_t is only a type to test
  // once T is known to name one.
  if constexpr(has_previous_configuration_c<T>)
  {
    if constexpr(upgradable_configuration_c<typename T::previous_t>)
    {
      return std::same_as<std::remove_cvref_t<decltype(upgrade(std::declval<const typename T::previous_t&>()))>, T>;
    }
  }

  return true;
}

///
///
template<configuration_c T>
consteval auto upgrade_matches_next() -> bool
{
  // Nested for the same reason as in upgrade_matches_previous().
  if constexpr(upgradable_configuration_c<T>)
  {
    using next_t = std::remove_cvref_t<decltype(upgrade(std::declval<const T&>()))>;

    if constexpr(has_previous_configuration_c<next_t>)
    {
      return std::same_as<typename next_t::previous_t, T>;
    }
  }

  return true;
}

} // namespace arude::config::detail

namespace arude::config
{

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
  static_assert(
    detail::upgrade_matches_previous<From>(),
    "arude::config::migrate: this version names a previous_t whose upgrade() returns another version. "
    "previous_t and upgrade() must describe the same step.");
  static_assert(
    detail::upgrade_matches_next<From>(),
    "arude::config::migrate: this version's upgrade() returns a version whose previous_t is another version. "
    "previous_t and upgrade() must describe the same step.");

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
