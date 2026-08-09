///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
#pragma once

#include "arude/type_name.hpp"

#include <concepts>
#include <cstddef>
#include <format>
#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

// Out of the sorted group above because it is conditional: libc++ does not
// implement <stacktrace> at any version, so the header has to be asked for
// rather than assumed. __has_include and not ARUDE_EXCEPTION_HAS_STACKTRACE
// below, because the feature test the latter reads only exists once the header
// has been included.
#if __has_include(<stacktrace>) && !(defined ARUDE_EXCEPTION_NO_STACKTRACE)
  #include <stacktrace>
#endif // #if __has_include(<stacktrace>) && !(defined ARUDE_EXCEPTION_NO_STACKTRACE)

// Likewise conditional: std::runtime_error is only ever named as the configured
// base below, so the default configuration does not pay for the header.
#if (defined ARUDE_EXCEPTION_RUNTIME_ERROR_BASE)
  #include <stdexcept>
#endif // #if (defined ARUDE_EXCEPTION_RUNTIME_ERROR_BASE)

// Whether a usable std::stacktrace is both present and wanted: the header
// exists, the standard library actually implements it — __cpp_lib_stacktrace
// distinguishes a library that ships <stacktrace> from one that also
// implements it — and ARUDE_EXCEPTION_NO_STACKTRACE has not turned it off.
//
// Derived, not a knob. Set ARUDE_EXCEPTION_NO_STACKTRACE to opt out, and read
// arude::stacktrace_available rather than this to branch in C++ rather than in
// the preprocessor.
//
// 1 or 0 rather than defined or undefined, so that the negation is
// `#if !ARUDE_EXCEPTION_HAS_STACKTRACE` and needs no second spelling.
#if (defined __cpp_lib_stacktrace) && !(defined ARUDE_EXCEPTION_NO_STACKTRACE)
  #define ARUDE_EXCEPTION_HAS_STACKTRACE 1
#else
  #define ARUDE_EXCEPTION_HAS_STACKTRACE 0
#endif // #if (defined __cpp_lib_stacktrace) && !(defined ARUDE_EXCEPTION_NO_STACKTRACE)

#if !(defined ARUDE_EXCEPTION_STACKTRACE_SKIP)
  #define ARUDE_EXCEPTION_STACKTRACE_SKIP 1
#endif // #if !(defined ARUDE_EXCEPTION_STACKTRACE_SKIP)

#if !(defined ARUDE_EXCEPTION_STACKTRACE_MAX_DEPTH)
  #define ARUDE_EXCEPTION_STACKTRACE_MAX_DEPTH 10
#endif // #if !(defined ARUDE_EXCEPTION_STACKTRACE_MAX_DEPTH)

///
/// formatter specialization for std::source_location.
///
template<>
struct std::formatter<std::source_location> : formatter<string>
{
  ///
  /// Formats the std::source_location object into a string representation.
  ///
  auto format(const auto& val, auto& ctx) const
  {
    return format_to(ctx.out(), "{}({}:{}) '{}'", val.file_name(), val.line(), val.column(), val.function_name());
  }
};

namespace arude::detail
{

///
/// Base that exception_base inherits, selected by ARUDE_EXCEPTION_RUNTIME_ERROR_BASE.
/// With the macro defined an arude exception is a std::exception and can be
/// caught as one; without it the hierarchy stays separate. Either way the base
/// is constructed from the message, so exception_base initialises it the same
/// way in both configurations.
///
#if (defined ARUDE_EXCEPTION_RUNTIME_ERROR_BASE)

using std_runtime_error_base_t = std::runtime_error;

#else

///
/// Stand-in base for the configuration where arude exceptions are not std exceptions.
/// Empty, so it costs nothing under empty base optimisation, and it takes the
/// message purely so that its constructor matches std::runtime_error's.
///
struct std_runtime_error_noop_base
{
  ///
  /// Accepts the error message and discards it.
  /// \param str The error message, which this base has no use for.
  ///
  explicit std_runtime_error_noop_base([[maybe_unused]] std::string_view str) {}
};

using std_runtime_error_base_t = std_runtime_error_noop_base;

#endif // #if (defined ARUDE_EXCEPTION_RUNTIME_ERROR_BASE)

} // namespace arude::detail

#if !ARUDE_EXCEPTION_HAS_STACKTRACE

namespace arude::detail
{

///
/// Stand-in for std::stacktrace where the standard library has none.
/// libc++ does not implement <stacktrace>, so on that platform the choice is
/// between a stand-in and no arude::exception at all. It holds no frames and
/// says as much when formatted, which is the honest answer: nothing here can
/// produce a trace. arude::stacktrace_available tells a caller which of the
/// two is in play, and the interface is the same either way.
///
class null_stacktrace final
{
public: // Typedefs
  using size_type = std::size_t;

public: // Accessors
  ///
  /// Captures nothing, so that the call site reads the same on every platform.
  ///
  /// \param skip Frames to skip, ignored.
  /// \param max_depth Frames to capture, ignored.
  /// \return An empty stacktrace.
  ///
  [[nodiscard]] static auto current(size_type skip = 0, size_type max_depth = 0) noexcept -> null_stacktrace;

  ///
  /// Reports that there are no frames, which is always the case.
  /// \return Always true.
  ///
  [[nodiscard]] constexpr auto empty() const noexcept -> bool;

  ///
  /// Returns the number of frames held, which is none.
  /// \return Always zero.
  ///
  [[nodiscard]] constexpr auto size() const noexcept -> size_type;
};

///
///
inline auto null_stacktrace::current(
  [[maybe_unused]] const size_type skip, [[maybe_unused]] const size_type max_depth) noexcept -> null_stacktrace
{
  return {};
}

///
///
constexpr auto null_stacktrace::empty() const noexcept -> bool
{
  return true;
}

///
///
constexpr auto null_stacktrace::size() const noexcept -> size_type
{
  return 0;
}

} // namespace arude::detail

///
/// formatter specialization for arude::detail::null_stacktrace.
///
template<>
struct std::formatter<arude::detail::null_stacktrace> : formatter<string>
{
  ///
  /// Writes a note that no stacktrace was available, rather than nothing at all.
  ///
  auto format([[maybe_unused]] const arude::detail::null_stacktrace& val, auto& ctx) const
  {
    return format_to(ctx.out(), "<stacktrace unsupported on this platform>");
  }
};

#endif // #if !ARUDE_EXCEPTION_HAS_STACKTRACE

// libstdc++ 13 ships <stacktrace> without the formatters the standard pairs
// with it, and to_string is what those formatters are specified to write, so
// supplying one here costs nothing in fidelity. __cpp_lib_formatters is the
// feature test for exactly this pair, so the specialization disappears on a
// standard library that has its own and cannot collide with it.
#if ARUDE_EXCEPTION_HAS_STACKTRACE && !(defined __cpp_lib_formatters)

///
/// formatter specialization for std::basic_stacktrace.
/// \tparam Allocator The stacktrace's allocator.
///
template<typename Allocator>
struct std::formatter<std::basic_stacktrace<Allocator>>
{
  ///
  /// Parses the format-spec, which the standard requires to be empty.
  ///
  /// \param ctx The parse context.
  /// \return The iterator at the end of the parsed spec.
  /// \throws std::format_error If anything precedes the closing brace.
  ///
  constexpr auto parse(std::format_parse_context& ctx) -> std::format_parse_context::iterator
  {
    auto it = ctx.begin();

    if(it != ctx.end() && *it != '}')
    {
      throw std::format_error{"arude: std::basic_stacktrace accepts no format spec."};
    }

    return it;
  }

  ///
  /// Writes the stacktrace exactly as std::to_string renders it.
  ///
  /// \param val The stacktrace to format.
  /// \param ctx The format context.
  /// \return The iterator past the written output.
  ///
  auto format(const std::basic_stacktrace<Allocator>& val, std::format_context& ctx) const
    -> std::format_context::iterator
  {
    return std::format_to(ctx.out(), "{}", std::to_string(val));
  }
};

#endif // #if ARUDE_EXCEPTION_HAS_STACKTRACE && !(defined __cpp_lib_formatters)

namespace arude
{

using exception_string_t = std::string;

#if ARUDE_EXCEPTION_HAS_STACKTRACE

///
/// Whether this platform can capture a stacktrace.
/// False where the standard library has no working <stacktrace>, in which case
/// exception_base still carries a stack() and it is always empty. Branch on
/// this rather than on a compiler or platform macro.
///
inline constexpr auto stacktrace_available = true;

using exception_stacktrace_t = std::stacktrace;

#else

///
/// \see stacktrace_available
///
inline constexpr auto stacktrace_available = false;

using exception_stacktrace_t = detail::null_stacktrace;

#endif // #if ARUDE_EXCEPTION_HAS_STACKTRACE

///
/// Base class for arude exceptions containing message, source location and stacktrace.
///
// The check reports on the class rather than on the destructor, so the
// suppression has to sit here. Under ARUDE_EXCEPTION_RUNTIME_ERROR_BASE the
// base is std::runtime_error, whose virtual destructor makes this one
// implicitly virtual; the destructor is protected either way, which is what
// actually prevents deletion through a base pointer.
// NOLINTNEXTLINE(cppcoreguidelines-virtual-class-destructor)
class exception_base : public detail::std_runtime_error_base_t
{
public: // Typedefs / Constants
  using string_t = exception_string_t;
  using source_location_t = std::source_location;
  using stacktrace_t = exception_stacktrace_t;

public: // Structors / Operators
  ///
  /// Primary constructor.
  ///
  /// \param str The error message.
  /// \param loc The source location where the error occurred.
  /// \param st The stack trace at the time of the error.
  ///
  inline explicit exception_base(
    string_t str,
    source_location_t loc = source_location_t::current(),
    stacktrace_t st = stacktrace_t::current(ARUDE_EXCEPTION_STACKTRACE_SKIP, ARUDE_EXCEPTION_STACKTRACE_MAX_DEPTH));

  ///
  /// Defaulted copy/move constructors and assignment operators.
  ///
  exception_base(const exception_base&) = default;

  ///
  /// Defaulted copy/move constructors and assignment operators.
  ///
  exception_base(exception_base&&) = default;

  ///
  /// Defaulted copy/move constructors and assignment operators.
  ///
  auto operator=(const exception_base&) -> exception_base& = default;

  ///
  /// Defaulted copy/move constructors and assignment operators.
  ///
  auto operator=(exception_base&&) -> exception_base& = default;

public: // Accessors
  ///
  /// Returns the error message.
  /// \return The error message as string.
  ///
  [[nodiscard]] constexpr auto str() const -> const string_t&;

  ///
  /// Returns the error message as modifiable string.
  /// This can be used to modify the error message and then rethrow the exception.
  ///
  /// \return The error message as modifiable string.
  ///
  [[nodiscard]] constexpr auto str() -> string_t&;

  ///
  /// Returns the source location where the error occurred.
  /// \return The source location as source_location_t.
  ///
  [[nodiscard]] constexpr auto where() const -> const source_location_t&;

  ///
  /// Returns the stack trace at the time of the error.
  /// \return The stack trace as stacktrace_t.
  ///
  [[nodiscard]] constexpr auto stack() const -> const stacktrace_t&;

  ///
  /// Returns a formatted string representation of the exception.
  /// Collects string form derived classes based on if the payload is formattable.
  ///
  /// \return The formatted string representation.
  ///
  [[nodiscard]] inline auto to_string() const -> string_t;

protected: // Structors
  ///
  /// Defaulted destructor (Protected).
  /// Protected rather than public so the class cannot be used directly, and so
  /// no one can delete a derived object through a base pointer. That is what
  /// a virtual destructor would have been guarding against, so it is left off.
  ///
  // Implicitly an override under ARUDE_EXCEPTION_RUNTIME_ERROR_BASE, where the
  // base contributes a virtual destructor. Annotating it would then satisfy the
  // check but fail to compile in the default configuration, where there is
  // nothing to override.
  // NOLINTNEXTLINE(modernize-use-override,cppcoreguidelines-explicit-virtual-functions)
  ~exception_base() noexcept = default;

private: // Virtuals
  ///
  /// Returns a formatted string representation of the derived exception.
  /// \return The formatted string representation.
  ///
  [[nodiscard]] virtual auto do_to_string() const -> string_t = 0;

private: // Variables
  string_t str_;
  source_location_t loc_;
  stacktrace_t st_;
};

///
/// Concept for valid user data types for arude exceptions.
/// Allows copy/move constructible types or void (for no payload).
///
/// \tparam UD User data type.
///
template<typename UD>
concept exception_user_data = std::copy_constructible<UD> || std::move_constructible<UD> || std::is_void_v<UD>;

///
/// Arude exception with user data payload.
/// \tparam UD User data type (default = void).
///
template<exception_user_data UD = void>
class exception : public exception_base
{
public: // Typedefs
  using user_data_t = UD;

public: // Structors
  ///
  /// Primary constructor.
  ///
  /// \param str The error message.
  /// \param ud The user data payload.
  /// \param loc The source location where the error occurred.
  /// \param st The stack trace at the time of the error.
  ///
  exception(
    string_t str,
    exception_user_data auto&& ud,
    source_location_t loc = source_location_t::current(),
    stacktrace_t st = stacktrace_t::current(ARUDE_EXCEPTION_STACKTRACE_SKIP, ARUDE_EXCEPTION_STACKTRACE_MAX_DEPTH));

  ///
  /// Defaulted copy/move constructors and assignment operators.
  ///
  exception(const exception&) = default;

  ///
  /// Defaulted copy/move constructors and assignment operators.
  ///
  exception(exception&&) = default;

  ///
  /// Defaulted copy/move constructors and assignment operators.
  ///
  auto operator=(const exception&) -> exception& = default;

  ///
  /// Defaulted copy/move constructors and assignment operators.
  ///
  auto operator=(exception&&) -> exception& = default;

  ///
  /// Virtual defaulted destructor.
  /// Public and virtual, unlike exception_base's, because this class is meant
  /// to be derived from and deleted as itself: std::throw_with_nested derives
  /// from the thrown type, and only does so when that type is not final.
  ///
  // Implicitly an override once the configured base brings a virtual
  // destructor with it; a plain new virtual otherwise. See ~exception_base.
  // NOLINTNEXTLINE(modernize-use-override,cppcoreguidelines-explicit-virtual-functions)
  virtual ~exception() noexcept = default;

public: // Accessors
  ///
  /// Returns the user data payload as const reference.
  /// \return The user data payload.
  ///
  [[nodiscard]] constexpr auto data() const -> const user_data_t&;

  ///
  /// Returns the user data payload as modifiable reference.
  /// This can be used to modify the user data payload and then rethrow the exception.
  ///
  /// \return The user data payload.
  ///
  [[nodiscard]] constexpr auto data() -> user_data_t&;

private: // Overrides
  ///
  /// \see exception_base::do_to_string
  ///
  [[nodiscard]] auto do_to_string() const -> string_t override;

private: // Variables
  user_data_t data_;
};

template<>
class exception<void> : public exception_base
{
public: // Typedefs
  using user_data_t = void;

public: // Structors
  using exception_base::exception_base;

  ///
  /// Defaulted copy/move constructors and assignment operators.
  ///
  exception(const exception&) = default;

  ///
  /// Defaulted copy/move constructors and assignment operators.
  ///
  exception(exception&&) = default;

  ///
  /// Defaulted copy/move constructors and assignment operators.
  ///
  auto operator=(const exception&) -> exception& = default;

  ///
  /// Defaulted copy/move constructors and assignment operators.
  ///
  auto operator=(exception&&) -> exception& = default;

  ///
  /// Virtual defaulted destructor.
  /// \see exception::~exception
  ///
  // Implicitly an override once the configured base brings a virtual
  // destructor with it; a plain new virtual otherwise. See ~exception_base.
  // NOLINTNEXTLINE(modernize-use-override,cppcoreguidelines-explicit-virtual-functions)
  virtual ~exception() noexcept = default;

private: // Overrides
  ///
  /// \see exception_base::do_to_string
  ///
  [[nodiscard]] inline auto do_to_string() const -> string_t override;
};

// CTAD guide for exception<void> - deduce void when only string-like argument is provided
exception(exception_base::string_t) -> exception<void>;
exception(const char*) -> exception<void>;

// CTAD guide for exception<UD> - deduce UD from the user data argument
template<exception_user_data UD>
exception(exception_base::string_t, UD&&) -> exception<std::remove_cvref_t<UD>>;
template<exception_user_data UD>
exception(const char*, UD&&) -> exception<std::remove_cvref_t<UD>>;

///
///
exception_base::exception_base(string_t str, const source_location_t loc, stacktrace_t st)
  : detail::std_runtime_error_base_t{str}
  , str_{std::move(str)}
  , loc_{loc}
  , st_{std::move(st)}
{
}

///
///
constexpr auto exception_base::str() const -> const string_t&
{
  return str_;
}

///
///
constexpr auto exception_base::str() -> string_t&
{
  return str_;
}

///
///
constexpr auto exception_base::where() const -> const source_location_t&
{
  return loc_;
}

///
///
constexpr auto exception_base::stack() const -> const stacktrace_t&
{
  return st_;
}

///
///
auto exception_base::to_string() const -> string_t
{
  return do_to_string();
}

///
///
template<exception_user_data UD>
exception<UD>::exception(string_t str, exception_user_data auto&& ud, const source_location_t loc, stacktrace_t st)
  : exception_base{std::move(str), loc, std::move(st)}
  , data_{std::forward<decltype(ud)>(ud)}
{
}

///
///
template<exception_user_data UD>
constexpr auto exception<UD>::data() const -> const user_data_t&
{
  return data_;
}

///
///
template<exception_user_data UD>
constexpr auto exception<UD>::data() -> user_data_t&
{
  return data_;
}

///
///
template<exception_user_data UD>
auto exception<UD>::do_to_string() const -> string_t
{
  try
  {
    if constexpr(std::formattable<UD, string_t::value_type>)
    {
      return std::format(
        "exception<{}> - str: '{}'\ndata: '{}'\nlocation: {}\nstacktrace: {}",
        type_name<UD>(),
        str(),
        data_,
        where(),
        stack());
    }
    else
    {
      return std::format(
        "exception<{}> - str: '{}'\ndata: '<not formattable>'\nlocation: {}\nstacktrace: {}",
        type_name<UD>(),
        str(),
        where(),
        stack());
    }
  }
  catch(...)
  {
    return "Error formatting exception details";
  }
}

///
///
auto exception<void>::do_to_string() const -> string_t
{
  try
  {
    return std::format("exception<void> - str: '{}'\nlocation: {}\nstacktrace: {}", str(), where(), stack());
  }
  catch(...)
  {
    return "Error formatting exception details";
  }
}

} // namespace arude

///
/// formatter specialization for arude::exception_base.
///
template<>
struct std::formatter<arude::exception_base> : formatter<string>
{
  auto format(const auto& val, auto& ctx) const { return format_to(ctx.out(), "{}", val.to_string()); }
};
