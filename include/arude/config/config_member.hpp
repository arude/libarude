///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// config_member<M>: one member of a file's root, for injecting into a
/// subsystem that must not see the whole root.
///
/// The type depends only on M, so a subsystem's constructor takes
/// config_member<network_v2> and never names the application's root type.
/// A handle is made only by config_file::member(); the member_source behind
/// it is libarude's own, not an extension point.
///
#pragma once

#include "arude/config/migration.hpp"
#include "arude/config/transport.hpp"
#include "arude/noncopyable.hpp"

#include <memory>
#include <source_location>
#include <string>

namespace arude::config
{

template<configuration_c T, transport_c Tr, typename F>
class config_file; // Defined in arude/config/config_file.hpp.

} // namespace arude::config

namespace arude::config::detail
{

///
/// Access to one member of some file's root, independent of the root's type.
/// One implementation per file type: the file_member in config_file.hpp.
///
/// \tparam M The member's type.
///
template<typename M>
class member_source : public noncopyable
{
public: // Structors / Operators
  ///
  /// Destroys the source.
  ///
  virtual ~member_source() = default;

  ///
  /// The source is made once and shared, so it is not copied.
  ///
  member_source(const member_source&) = delete;

  ///
  /// The source is made once and shared, so it is not moved.
  ///
  member_source(member_source&&) = delete;

  ///
  /// \see member_source(const member_source&)
  ///
  auto operator=(const member_source&) -> member_source& = delete;

  ///
  /// \see member_source(member_source&&)
  ///
  auto operator=(member_source&&) -> member_source& = delete;

public: // Methods
  // NOLINTBEGIN(portability-template-virtual-member-function)
  // The member's type is what the template is for: each instantiation has
  // its own virtual functions, all of one complete signature, and the check
  // is about virtual functions whose bodies are not instantiated.
  ///
  /// Returns a copy of the member.
  /// \param loc The call site a failure reports.
  /// \return The member.
  /// \throws exception_t With not_found if the root is not loaded or set.
  ///
  [[nodiscard]] virtual auto get(std::source_location loc) const -> M = 0;

  ///
  /// Replaces the member.
  /// \param value The member to hold.
  /// \param loc The call site a failure reports.
  /// \throws exception_t With not_found if the root is not loaded or set.
  ///
  virtual auto set(M value, std::source_location loc) -> void = 0;

  ///
  /// Reports whether the root holds a value.
  /// \return true once the file has been loaded or set.
  ///
  [[nodiscard]] virtual auto loaded() const -> bool = 0;

  ///
  /// Returns the file's location, as its transport names it.
  /// \return The location.
  ///
  [[nodiscard]] virtual auto location() const -> std::string = 0;

  ///
  /// Stores the whole file, not just the member.
  /// \param loc The call site a failure reports.
  /// \throws exception_t As config_file::store() does.
  ///
  virtual auto store(std::source_location loc) -> void = 0;
  // NOLINTEND(portability-template-virtual-member-function)

protected: // Structors
  ///
  /// Defaulted, for the derived implementations.
  ///
  member_source() = default;
};

} // namespace arude::config::detail

namespace arude::config
{

///
/// One member of a file's root, for injecting into a subsystem that must
/// not see the whole root. Copies share the member. A move copies too, so a
/// moved-from handle still refers to the same member. Keeps the file's
/// state alive, so a handle outliving its config_file still works. Made
/// only by config_file::member().
///
/// A subsystem's own unit test gets a handle without any I/O: build a
/// config_file over any transport, set() the root, and call member().
/// Nothing touches the medium until load() or store().
///
/// \tparam M The member's type, unwrapped of versioned<>.
///
template<typename M>
class config_member final
{
  template<configuration_c T, transport_c Tr, typename F>
  friend class config_file;

public: // Structors / Operators
  ///
  /// Copies a handle, which shares the member.
  /// \param other The handle to copy.
  ///
  config_member(const config_member& other) = default;

  ///
  /// Moves a handle by copying it: a moved-from handle still refers to the
  /// same member.
  /// \param other The handle to copy from.
  ///
  config_member(config_member&& other) noexcept;

  ///
  /// Destroys the handle.
  ///
  ~config_member() = default;

  ///
  /// Copies a handle, which shares the member.
  /// \param other The handle to copy.
  /// \return This handle.
  ///
  auto operator=(const config_member& other) -> config_member& = default;

  ///
  /// Moves a handle by copying it, as the move constructor does.
  /// \param other The handle to copy from.
  /// \return This handle.
  ///
  auto operator=(config_member&& other) noexcept -> config_member&;

public: // Accessors
  ///
  /// Returns a copy of the member.
  /// \param loc The call site a failure reports. Left to its default.
  /// \return The member.
  /// \throws exception_t With not_found if the root is not loaded or set.
  ///
  [[nodiscard]] auto get(std::source_location loc = std::source_location::current()) const -> M;

  ///
  /// Reports whether the root holds a value.
  /// \return true once the file has been loaded or set.
  ///
  [[nodiscard]] auto loaded() const -> bool;

  ///
  /// Returns the file's location, as its transport names it.
  /// \return The location.
  ///
  [[nodiscard]] auto location() const -> std::string;

public: // Methods
  ///
  /// Replaces the member.
  /// \param value The member to hold.
  /// \param loc The call site a failure reports. Left to its default.
  /// \throws exception_t With not_found if the root is not loaded or set.
  ///
  auto set(M value, std::source_location loc = std::source_location::current()) -> void;

  ///
  /// Stores the whole file, not just the member.
  /// \param loc The call site a failure reports. Left to its default.
  /// \throws exception_t As config_file::store() does.
  ///
  auto store(std::source_location loc = std::source_location::current()) -> void;

private: // Structors
  ///
  /// Constructs a handle over a source. Called by config_file::member().
  /// \param source The source, which the handle shares.
  ///
  explicit config_member(std::shared_ptr<detail::member_source<M>> source);

private: // Variables
  ///
  /// The source behind the handle. Never null.
  ///
  std::shared_ptr<detail::member_source<M>> source_;
};

///
///
template<typename M>
config_member<M>::config_member(config_member&& other) noexcept
  // NOLINTNEXTLINE(performance-move-constructor-init): copying is the contract, so a moved-from handle still works.
  : source_{other.source_}
{
}

///
///
template<typename M>
auto config_member<M>::operator=(config_member&& other) noexcept -> config_member&
{
  source_ = other.source_;

  return *this;
}

///
///
template<typename M>
auto config_member<M>::get(const std::source_location loc) const -> M
{
  return source_->get(loc);
}

///
///
template<typename M>
auto config_member<M>::loaded() const -> bool
{
  return source_->loaded();
}

///
///
template<typename M>
auto config_member<M>::location() const -> std::string
{
  return source_->location();
}

///
///
template<typename M>
auto config_member<M>::set(M value, const std::source_location loc) -> void
{
  source_->set(std::move(value), loc);
}

///
///
template<typename M>
auto config_member<M>::store(const std::source_location loc) -> void
{
  source_->store(loc);
}

///
///
template<typename M>
config_member<M>::config_member(std::shared_ptr<detail::member_source<M>> source)
  : source_{std::move(source)}
{
}

} // namespace arude::config
