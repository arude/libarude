///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// An identity token for a type, usable as the key of an associative container.
///
/// Two implementations, chosen by whether the build has RTTI, because neither
/// is right everywhere:
///
///   - With RTTI, std::type_index. It is the portable answer and, on the
///     Itanium ABI, the correct one across shared-library boundaries: two
///     instantiations of one type in two objects produce two std::type_info
///     objects, and operator== falls back to comparing the mangled names so
///     they still match.
///   - Without RTTI, the address of a per-type inline variable. Comparing keys
///     is then a pointer comparison, which is cheaper — but it is exactly the
///     case the name comparison above exists to rescue, so a type instantiated
///     in two objects built with -fvisibility=hidden can yield two keys and a
///     lookup that should hit will miss. Embedded builds are frequently
///     -fno-rtti and equally frequently a single binary, where that cannot
///     arise; a shared-library build that disables RTTI should know this.
///
/// Either way the key is opaque: compare it, hash it, do not persist it and do
/// not parse it. arude::type_name<T>() is what diagnostics use.
///
#pragma once

// clang and gcc define __GXX_RTTI, msvc defines _CPP_RTTI, and __cpp_rtti is
// the standard spelling newer front ends also provide. Any of the three means
// typeid is available.
#if (defined __cpp_rtti) || (defined __GXX_RTTI) || (defined _CPP_RTTI)
  #define ARUDE_TYPE_KEY_RTTI
#endif // #if (defined __cpp_rtti) || (defined __GXX_RTTI) || (defined _CPP_RTTI)

#if (defined ARUDE_TYPE_KEY_RTTI)
  #include <typeindex>
  #include <typeinfo>
#else
  #include <cstddef>
  #include <functional>
#endif // #if (defined ARUDE_TYPE_KEY_RTTI)

#if !(defined ARUDE_TYPE_KEY_RTTI)

namespace arude::detail
{

///
/// The per-type byte whose address stands in for the type.
/// One object per T across the program, which is what makes the address an
/// identity; see the file comment for where that breaks down.
///
/// \tparam T Type to identify.
///
template<typename T>
inline constexpr char type_key_storage = 0;

} // namespace arude::detail

#endif // #if !(defined ARUDE_TYPE_KEY_RTTI)

namespace arude
{

#if (defined ARUDE_TYPE_KEY_RTTI)

///
/// A type's identity, as a value to compare, hash and store.
/// std::type_index here; a pointer wrapper where the build has no RTTI. Treat
/// it as opaque either way, so code compiles under both.
///
using type_key_t = std::type_index;

#else

///
/// \see type_key_t
///
class type_key_t final
{
public: // Structors
  ///
  /// Constructs a key from a per-type address.
  /// \param id Address standing in for the type.
  ///
  explicit constexpr type_key_t(const void* id);

public: // Operators
  ///
  /// Compares two keys.
  /// \param other Key to compare against.
  /// \return true if both name the same type.
  ///
  [[nodiscard]] constexpr auto operator==(const type_key_t& other) const -> bool = default;

public: // Accessors
  ///
  /// Returns the address this key is built on, for hashing.
  /// \return The address.
  ///
  [[nodiscard]] constexpr auto id() const -> const void*;

private: // Variables
  const void* id_;
};

///
///
constexpr type_key_t::type_key_t(const void* id)
  : id_{id}
{
}

///
///
constexpr auto type_key_t::id() const -> const void*
{
  return id_;
}

#endif // #if (defined ARUDE_TYPE_KEY_RTTI)

///
/// Returns the identity token for a type.
/// Equal for one type and unequal for any two different types, so it is usable
/// as the key of an unordered_map. Not constexpr where the build has RTTI,
/// because std::type_index is not constructible at compile time; do not write
/// code that depends on it being one or the other.
///
/// \tparam T Type to identify. Need not be complete where the build has no RTTI.
/// \return The token for T.
///
template<typename T>
[[nodiscard]] auto type_key() -> type_key_t
{
#if (defined ARUDE_TYPE_KEY_RTTI)
  return type_key_t{typeid(T)};
#else
  return type_key_t{&detail::type_key_storage<T>};
#endif // #if (defined ARUDE_TYPE_KEY_RTTI)
}

} // namespace arude

#if !(defined ARUDE_TYPE_KEY_RTTI)

///
/// Hash support, so type_key_t can key an unordered container.
/// std::type_index brings its own; the pointer wrapper needs this one.
///
template<>
struct std::hash<arude::type_key_t>
{
  ///
  /// Hashes a key.
  /// \param value Key to hash.
  /// \return The hash of the address it carries.
  ///
  [[nodiscard]] auto operator()(const arude::type_key_t& value) const noexcept -> std::size_t
  {
    return std::hash<const void*>{}(value.id());
  }
};

#endif // #if !(defined ARUDE_TYPE_KEY_RTTI)
