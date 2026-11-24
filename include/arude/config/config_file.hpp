///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// config_file<T, Tr>: one configuration file — a transport, the format,
/// and the cached root — read and written whole.
///
/// The root is one versioned struct per file: load() reads the whole
/// document through the versioned machinery in toml.hpp, migrating the root
/// and every versioned member to the newest version this build knows, and
/// installs the result in one step, all or nothing. store() writes the whole
/// struct back through the transport, after probing the source so a file
/// written by a newer build is never overwritten by accident. get() and
/// set() touch only the cache, so they never wait for I/O.
///
#pragma once

#include "arude/config/common.hpp"
#include "arude/config/config_member.hpp"
#include "arude/config/load_policy.hpp"
#include "arude/config/migration.hpp"
#include "arude/config/toml.hpp"
#include "arude/config/transport.hpp"
#include "arude/noncopyable.hpp"

#include <toml++/toml.hpp>

#include <concepts>
#include <cstdint>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <source_location>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace arude::config::detail
{

///
/// The member a pointer to member names, unwrapping versioned<>.
///
/// \tparam M The member's type, or the type a versioned<> member holds.
/// \tparam T The root's type.
/// \param root The root to take the member from.
/// \param member The member to name.
/// \return The member.
///
template<typename M, typename T>
[[nodiscard]] constexpr auto member_of(T& root, M T::* member) -> M&;

///
/// \see member_of(T&, M T::*)
///
template<configuration_c M, typename T>
[[nodiscard]] constexpr auto member_of(T& root, versioned<M> T::* member) -> M&;

///
/// \see member_of(T&, M T::*)
///
template<typename M, typename T>
[[nodiscard]] constexpr auto member_of(const T& root, M T::* member) -> const M&;

///
/// \see member_of(T&, M T::*)
///
template<configuration_c M, typename T>
[[nodiscard]] constexpr auto member_of(const T& root, versioned<M> T::* member) -> const M&;

///
///
template<typename M, typename T>
constexpr auto member_of(T& root, M T::* member) -> M&
{
  return root.*member;
}

///
///
template<configuration_c M, typename T>
constexpr auto member_of(T& root, versioned<M> T::* member) -> M&
{
  return (root.*member).value();
}

///
///
template<typename M, typename T>
constexpr auto member_of(const T& root, M T::* member) -> const M&
{
  return root.*member;
}

///
///
template<configuration_c M, typename T>
constexpr auto member_of(const T& root, versioned<M> T::* member) -> const M&
{
  return (root.*member).value();
}

///
/// What one config_file and every handle made from it share.
/// mutex_ guards value_ and the load numbers; io_mutex_ guards no data and
/// orders stores, so the newest snapshot is written last.
///
/// \tparam T The root's configuration type.
/// \tparam Tr The transport type.
/// \tparam F The format type.
///
template<configuration_c T, transport_c Tr, typename F>
class file_state final : public noncopyable
{
public: // Structors
  ///
  /// Constructs the state over a transport and a format.
  /// \param carrier The transport, moved in.
  /// \param format The format's options, moved in.
  ///
  file_state(Tr carrier, F format);

public: // Accessors
  ///
  /// Returns the file's location, as its transport names it.
  /// \return The location.
  ///
  [[nodiscard]] auto location() const -> std::string;

  ///
  /// Reports whether the transport can be written.
  /// \return true if store() may attempt a write.
  ///
  [[nodiscard]] auto writable() const -> bool;

  ///
  /// Reports whether the root holds a value.
  /// \return true once the file has been loaded or set.
  ///
  [[nodiscard]] auto loaded() const -> bool;

  ///
  /// Returns a copy of the cached root.
  /// \param loc The call site a failure reports.
  /// \return The root.
  /// \throws exception_t With not_found if the root is not loaded or set.
  ///
  [[nodiscard]] auto get(std::source_location loc) const -> T;

  ///
  /// Returns a copy of one member of the cached root.
  /// \tparam M The member's type, unwrapped of versioned<>.
  /// \tparam Ptr The pointer-to-member type.
  /// \param member The member to take.
  /// \param loc The call site a failure reports.
  /// \return The member.
  /// \throws exception_t With not_found if the root is not loaded or set.
  ///
  template<typename M, typename Ptr>
  [[nodiscard]] auto get_member(Ptr member, std::source_location loc) const -> M;

public: // Methods
  ///
  /// Replaces the cached root.
  /// \param value The root to hold.
  ///
  auto set(T value) -> void;

  ///
  /// Replaces one member of the cached root.
  /// \tparam M The member's type, unwrapped of versioned<>.
  /// \tparam Ptr The pointer-to-member type.
  /// \param member The member to replace.
  /// \param value The member to hold.
  /// \param loc The call site a failure reports.
  /// \throws exception_t With not_found if the root is not loaded or set.
  ///
  template<typename M, typename Ptr>
  auto set_member(Ptr member, M value, std::source_location loc) -> void;

  ///
  /// Loads the file: reads and parses outside any lock, then installs the
  /// result in one step. A load that fails changes nothing, and a load
  /// never writes, on any policy.
  ///
  /// \param policy What to do about a missing or older source.
  /// \param loc The call site a failure reports.
  /// \throws exception_t As detail::read_root() does, with not_found where
  ///         the source is missing without create and io_error where the
  ///         transport fails.
  ///
  auto load(load_policy policy, std::source_location loc) -> void;

  ///
  /// Stores the cached root through the transport. With probe set, the
  /// source is read first and the store refused where a newer build wrote
  /// it; see the config_file::store() documentation.
  ///
  /// \param probe Whether to read the source first and refuse newer data.
  /// \param loc The call site a failure reports.
  /// \throws exception_t With read_only where the transport cannot be
  ///         written, invalid_version where the probe refuses, not_found
  ///         where nothing is cached, or io_error where the transport fails.
  ///
  auto store(bool probe, std::source_location loc) -> void;

private: // Helpers
  ///
  /// Reads the source through the transport and parses it, outside any lock.
  /// \param loc The call site a failure reports.
  /// \return The parsed table, or none where the transport has nothing.
  /// \throws exception_t With io_error where the transport fails, or
  ///         invalid_payload where the bytes are not TOML.
  ///
  [[nodiscard]] auto read_source(std::source_location loc) const -> std::optional<toml::table>;

  ///
  /// Returns a copy of the cached root, for a store to write.
  /// \param loc The call site a failure reports.
  /// \return The root.
  /// \throws exception_t With not_found if the root is not loaded or set.
  ///
  [[nodiscard]] auto snapshot(std::source_location loc) const -> T;

private: // Variables
  ///
  /// The transport, which the state is the owner of.
  /// Const, as the state is the one owner and the transport never moves
  /// again; the state is not copyable or movable either way.
  ///
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members): never reassigned, and the class is not copyable.
  const Tr carrier_;

  ///
  /// The format's options.
  ///
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members): never reassigned, and the class is not copyable.
  const F format_;

  ///
  /// Guards value_ and the load numbers.
  ///
  mutable std::shared_mutex mutex_;

  ///
  /// The cached root. Disengaged until loaded or set.
  ///
  std::optional<T> value_;

  ///
  /// Numbers each load as it starts.
  ///
  std::uint64_t loads_begun_ = 0;

  ///
  /// The number of the load whose result value_ holds.
  ///
  std::uint64_t load_installed_ = 0;

  ///
  /// Held by a store from its probe until its write has finished, so stores
  /// are serialised and the newest snapshot is written last.
  ///
  std::mutex io_mutex_;
};

///
///
template<configuration_c T, transport_c Tr, typename F>
file_state<T, Tr, F>::file_state(Tr carrier, F format)
  : carrier_{std::move(carrier)}
  , format_{std::move(format)}
{
}

///
///
template<configuration_c T, transport_c Tr, typename F>
auto file_state<T, Tr, F>::location() const -> std::string
{
  return carrier_.location();
}

///
///
template<configuration_c T, transport_c Tr, typename F>
auto file_state<T, Tr, F>::writable() const -> bool
{
  return carrier_.writable();
}

///
///
template<configuration_c T, transport_c Tr, typename F>
auto file_state<T, Tr, F>::loaded() const -> bool
{
  const auto lock = std::shared_lock{mutex_};

  return value_.has_value();
}

///
///
template<configuration_c T, transport_c Tr, typename F>
auto file_state<T, Tr, F>::get(const std::source_location loc) const -> T
{
  const auto lock = std::shared_lock{mutex_};

  if(!value_.has_value())
  {
    throw exception_t{
      std::format("arude::config: '{}' has not been loaded or set.", carrier_.location()),
      config_error::not_found,
      loc};
  }

  return *value_;
}

///
///
template<configuration_c T, transport_c Tr, typename F>
template<typename M, typename Ptr>
auto file_state<T, Tr, F>::get_member(const Ptr member, const std::source_location loc) const -> M
{
  const auto lock = std::shared_lock{mutex_};

  if(!value_.has_value())
  {
    throw exception_t{
      std::format("arude::config: '{}' has not been loaded or set.", carrier_.location()),
      config_error::not_found,
      loc};
  }

  return member_of(*value_, member);
}

///
///
template<configuration_c T, transport_c Tr, typename F>
auto file_state<T, Tr, F>::set(T value) -> void
{
  const auto lock = std::unique_lock{mutex_};

  value_ = std::move(value);
}

///
///
template<configuration_c T, transport_c Tr, typename F>
template<typename M, typename Ptr>
auto file_state<T, Tr, F>::set_member(const Ptr member, M value, const std::source_location loc) -> void
{
  const auto lock = std::unique_lock{mutex_};

  if(!value_.has_value())
  {
    throw exception_t{
      std::format("arude::config: '{}' has not been loaded or set.", carrier_.location()),
      config_error::not_found,
      loc};
  }

  member_of(*value_, member) = std::move(value);
}

///
///
template<configuration_c T, transport_c Tr, typename F>
auto file_state<T, Tr, F>::load(const load_policy policy, const std::source_location loc) -> void
{
  // Numbered under the lock, so two overlapping loads can be told apart:
  // the later-started one stands.
  const auto number = [this] -> std::uint64_t
  {
    const auto lock = std::unique_lock{mutex_};

    return ++loads_begun_;
  }();

  // Read and parse with no lock held: get() and set() never wait for I/O.
  auto source = read_source(loc);

  auto result = T{};

  if(!source.has_value())
  {
    if(!is_set(policy, load_policy::create))
    {
      throw exception_t{
        std::format("arude::config: there is no configuration at '{}'.", carrier_.location()),
        config_error::not_found,
        loc};
    }
  }
  else
  {
    result = read_root<T>(*source, policy, format_, carrier_.location(), loc);
  }

  // Installed only after the whole read, so a load that fails has changed
  // nothing.
  const auto lock = std::unique_lock{mutex_};

  if(load_installed_ > number)
  {
    return; // A load started later has already installed its value.
  }

  load_installed_ = number;
  value_ = std::move(result);
}

///
///
template<configuration_c T, transport_c Tr, typename F>
auto file_state<T, Tr, F>::store(const bool probe, const std::source_location loc) -> void
{
  if(!carrier_.writable())
  {
    throw exception_t{
      std::format("arude::config: the transport at '{}' is read-only.", carrier_.location()),
      config_error::read_only,
      loc};
  }

  // Held to the end, so stores are serialised and snapshots are written in
  // the order they were taken.
  const auto io_lock = std::scoped_lock{io_mutex_};

  if(probe)
  {
    // The source is read first so that newer data is not overwritten: a root
    // or member a newer build wrote cannot be read back by this one, and
    // writing over it would lose what that build stored. Anything else this
    // build cannot read, a hand-edit that broke the TOML or a value of the
    // wrong type, is replaced: the cached root is what is wanted there.
    auto source = std::optional<toml::table>{};

    try
    {
      source = read_source(loc);
    }
    catch(const exception_t& ex)
    {
      // Only a parse failure has that code here, and a source that is not
      // TOML holds nothing a newer build could have written.
      if(ex.data() != config_error::invalid_payload)
      {
        throw;
      }
    }

    if(source.has_value())
    {
      if(const auto newer = newer_in<T>(*source); newer.has_value())
      {
        throw exception_t{
          std::format(
            "arude::config: refusing to store over '{}', which a newer build wrote: {}",
            carrier_.location(),
            newer.value()),
          config_error::invalid_version,
          loc};
      }
    }
  }

  const auto current = snapshot(loc);

  // Rendered outside any cache lock, and only after the probe: an
  // unrepresentable value throws here, before anything is written.
  const auto text = render_toml(write_root(current, loc));

  try
  {
    write_to(carrier_, as_bytes(text), loc);
  }
  catch(...)
  {
    rethrow_as(config_error::io_error, std::format("'{}' could not be written", carrier_.location()), loc);
  }
}

///
///
template<configuration_c T, transport_c Tr, typename F>
auto file_state<T, Tr, F>::read_source(const std::source_location loc) const -> std::optional<toml::table>
{
  const auto bytes = [this, loc]() -> std::optional<std::vector<std::byte>>
  {
    try
    {
      return read_from(carrier_, loc);
    }
    catch(...)
    {
      rethrow_as(config_error::io_error, std::format("'{}' could not be read", carrier_.location()), loc);
    }
  }();

  if(!bytes.has_value())
  {
    return std::nullopt;
  }

  return parse_toml(as_text(*bytes), carrier_.location(), loc);
}

///
///
template<configuration_c T, transport_c Tr, typename F>
auto file_state<T, Tr, F>::snapshot(const std::source_location loc) const -> T
{
  const auto lock = std::shared_lock{mutex_};

  if(!value_.has_value())
  {
    throw exception_t{
      std::format(
        "arude::config: '{}' has not been loaded or set, and there is nothing to store.", carrier_.location()),
      config_error::not_found,
      loc};
  }

  return *value_;
}

///
/// member_source<M> over one member of a file's root.
///
/// \tparam M The member's type, unwrapped of versioned<>.
/// \tparam T The root's configuration type.
/// \tparam Tr The transport type.
/// \tparam F The format type.
/// \tparam Ptr The pointer-to-member type.
///
template<typename M, configuration_c T, transport_c Tr, typename F, typename Ptr>
class file_member final : public member_source<M>
{
public: // Structors
  ///
  /// Constructs the source over the file's state.
  /// \param state The file's state, shared with the config_file.
  /// \param member The member this source is.
  ///
  file_member(std::shared_ptr<file_state<T, Tr, F>> state, Ptr member);

public: // Overrides
  ///
  /// \see member_source::get
  ///
  [[nodiscard]] auto get(std::source_location loc) const -> M override;

  ///
  /// \see member_source::set
  ///
  auto set(M value, std::source_location loc) -> void override;

  ///
  /// \see member_source::loaded
  ///
  [[nodiscard]] auto loaded() const -> bool override;

  ///
  /// \see member_source::location
  ///
  [[nodiscard]] auto location() const -> std::string override;

  ///
  /// \see member_source::store
  ///
  auto store(std::source_location loc) -> void override;

private: // Variables
  ///
  /// The file's state. Never null.
  ///
  std::shared_ptr<file_state<T, Tr, F>> state_;

  ///
  /// The member this source is.
  ///
  Ptr member_;
};

///
///
template<typename M, configuration_c T, transport_c Tr, typename F, typename Ptr>
file_member<M, T, Tr, F, Ptr>::file_member(std::shared_ptr<file_state<T, Tr, F>> state, const Ptr member)
  : state_{std::move(state)}
  , member_{member}
{
}

///
///
template<typename M, configuration_c T, transport_c Tr, typename F, typename Ptr>
auto file_member<M, T, Tr, F, Ptr>::get(const std::source_location loc) const -> M
{
  return state_->template get_member<M>(member_, loc);
}

///
///
template<typename M, configuration_c T, transport_c Tr, typename F, typename Ptr>
auto file_member<M, T, Tr, F, Ptr>::set(M value, const std::source_location loc) -> void
{
  state_->template set_member<M>(member_, std::move(value), loc);
}

///
///
template<typename M, configuration_c T, transport_c Tr, typename F, typename Ptr>
auto file_member<M, T, Tr, F, Ptr>::loaded() const -> bool
{
  return state_->loaded();
}

///
///
template<typename M, configuration_c T, transport_c Tr, typename F, typename Ptr>
auto file_member<M, T, Tr, F, Ptr>::location() const -> std::string
{
  return state_->location();
}

///
///
template<typename M, configuration_c T, transport_c Tr, typename F, typename Ptr>
auto file_member<M, T, Tr, F, Ptr>::store(const std::source_location loc) -> void
{
  state_->store(true, loc);
}

} // namespace arude::config::detail

namespace arude::config
{

///
/// One configuration file: a transport, the format, and the cached root.
/// Not copyable or movable: it is the one owner of the file, and member()
/// handles are what is shared. Hold it in a std::unique_ptr or
/// std::optional where it must be moved; returning one by value still
/// works.
///
/// load(policy) reads, parses and migrates outside any lock and swaps the
/// result in under one lock, all or nothing; a load that fails changes
/// nothing, and a load never writes. store() probes the source first and
/// refuses with invalid_version over a file a newer build wrote, root or any
/// versioned member, so that build's data is not lost; a source this build
/// cannot otherwise read, not TOML or not a T, is replaced. overwrite() is
/// the deliberate way past the refusal. Stores are serialised,
/// so the newest snapshot is written last. get() and set() never wait for
/// I/O.
///
/// \tparam T The root's configuration type, normally the newest version of it.
/// \tparam Tr The transport type.
/// \tparam F The format type, which is TOML and nothing else today.
///
template<configuration_c T, transport_c Tr, typename F = toml_format>
class config_file final : public noncopyable
{
  static_assert(std::same_as<F, toml_format>, "arude::config: TOML is the only format there is.");

public: // Typedefs
  ///
  /// The root's configuration type.
  ///
  using value_t = T;

  ///
  /// The transport type.
  ///
  using transport_t = Tr;

  ///
  /// The format type.
  ///
  using format_t = F;

public: // Structors / Operators
  ///
  /// Constructs the file over a transport, with the format's options.
  /// \param carrier The transport, moved in.
  /// \param format The format's options. Left to its default.
  ///
  explicit config_file(Tr carrier, F format = {});

  ///
  /// Destroys the file, releasing the transport and the cache.
  ///
  ~config_file() = default;

  ///
  /// The file is the one owner of its transport and cache, so it is not
  /// copied.
  ///
  config_file(const config_file&) = delete;

  ///
  /// The file is the one owner of its transport and cache, so it is not
  /// moved either; share member() handles instead.
  ///
  config_file(config_file&&) = delete;

  ///
  /// \see config_file(const config_file&)
  ///
  auto operator=(const config_file&) -> config_file& = delete;

  ///
  /// \see config_file(config_file&&)
  ///
  auto operator=(config_file&&) -> config_file& = delete;

public: // Accessors
  ///
  /// Returns the file's location, as its transport names it.
  /// \return The location.
  ///
  [[nodiscard]] auto location() const -> std::string;

  ///
  /// Reports whether the transport can be written.
  /// \return true if store() may attempt a write.
  ///
  [[nodiscard]] auto writable() const -> bool;

  ///
  /// Reports whether the root holds a value.
  /// \return true once the file has been loaded or set.
  ///
  [[nodiscard]] auto loaded() const -> bool;

  ///
  /// Returns a copy of the cached root.
  /// \param loc The call site a failure reports. Left to its default.
  /// \return The root.
  /// \throws exception_t With not_found if the root is not loaded or set.
  ///
  [[nodiscard]] auto get(std::source_location loc = std::source_location::current()) const -> T;

  ///
  /// Makes a handle to one member of the root, for injecting into a
  /// subsystem that must not see the whole root. The handle reads and
  /// writes the member under the file's lock and stores the whole file.
  ///
  /// \tparam M The configuration type the versioned member holds.
  /// \param member The member to make a handle to.
  /// \return The handle.
  ///
  template<configuration_c M>
  [[nodiscard]] auto member(versioned<M> T::* member) -> config_member<M>;

  ///
  /// Makes a handle to one plain member of the root.
  /// \tparam M The member's type.
  /// \param member The member to make a handle to.
  /// \return The handle.
  ///
  template<typename M>
    requires(!std::is_function_v<M>)
  [[nodiscard]] auto member(M T::* member) -> config_member<M>;

public: // Methods
  ///
  /// Replaces the cached root. Never throws, and never touches the medium.
  /// \param value The root to hold.
  ///
  auto set(T value) -> void;

  ///
  /// Loads the file, all or nothing; see the class documentation.
  ///
  /// Every field must be in the source unless its type is a std::optional,
  /// or a std::shared_ptr, which reflect-cpp reads as optional too: a missing
  /// one is empty, and a missing std::optional is std::nullopt even where the
  /// member has an initialiser. Any other missing field is invalid_payload,
  /// naming the field, and so is a versioned table without its `version`
  /// key.
  ///
  /// `create` covers a source that is not there at all, and nothing else:
  /// the root is then `T{}`, so every member takes the root's default member
  /// initialiser, where without the flag the load is not_found. A source that
  /// is there is read the same with the flag or without it.
  ///
  /// \param policy What to do about a missing or older source. Left to its
  ///               default, upgrade_to_current.
  /// \param loc The call site a failure reports. Left to its default.
  /// \throws exception_t With invalid_payload if the source does not read
  ///         as T, a required field or a version key missing, invalid_version
  ///         if it declares a version this build cannot load under the
  ///         policy, not_found where the source is missing without create,
  ///         or io_error where the transport fails.
  ///
  auto
  load(load_policy policy = load_policy::upgrade_to_current, std::source_location loc = std::source_location::current())
    -> void;

  ///
  /// Stores the cached root through the transport, after probing the
  /// source; see the class documentation. The source is never touched by
  /// the probe, and a refusal writes nothing.
  ///
  /// \param loc The call site a failure reports. Left to its default.
  /// \throws exception_t With read_only where the transport cannot be
  ///         written, invalid_version where the source holds data a newer
  ///         build wrote, not_found where nothing is cached, or io_error
  ///         where the transport fails.
  ///
  auto store(std::source_location loc = std::source_location::current()) -> void;

  ///
  /// Stores the cached root without probing the source first: the
  /// deliberate way past store()'s refusal to write over newer data.
  ///
  /// \param loc The call site a failure reports. Left to its default.
  /// \throws exception_t As store() does, less the probe's refusals.
  ///
  auto overwrite(std::source_location loc = std::source_location::current()) -> void;

private: // Variables
  ///
  /// The state behind the file, and behind every handle made from it.
  /// Never null: set in the constructor, and the class cannot be moved from.
  ///
  std::shared_ptr<detail::file_state<T, Tr, F>> state_;
};

///
///
template<configuration_c T, transport_c Tr, typename F>
config_file<T, Tr, F>::config_file(Tr carrier, F format)
  : state_{std::make_shared<detail::file_state<T, Tr, F>>(std::move(carrier), std::move(format))}
{
}

///
///
template<configuration_c T, transport_c Tr, typename F>
auto config_file<T, Tr, F>::location() const -> std::string
{
  return state_->location();
}

///
///
template<configuration_c T, transport_c Tr, typename F>
auto config_file<T, Tr, F>::writable() const -> bool
{
  return state_->writable();
}

///
///
template<configuration_c T, transport_c Tr, typename F>
auto config_file<T, Tr, F>::loaded() const -> bool
{
  return state_->loaded();
}

///
///
template<configuration_c T, transport_c Tr, typename F>
auto config_file<T, Tr, F>::get(const std::source_location loc) const -> T
{
  return state_->get(loc);
}

///
///
template<configuration_c T, transport_c Tr, typename F>
template<configuration_c M>
auto config_file<T, Tr, F>::member(versioned<M> T::* member) -> config_member<M>
{
  return config_member<M>{std::make_shared<detail::file_member<M, T, Tr, F, versioned<M> T::*>>(state_, member)};
}

///
///
template<configuration_c T, transport_c Tr, typename F>
template<typename M>
  requires(!std::is_function_v<M>)
auto config_file<T, Tr, F>::member(M T::* member) -> config_member<M>
{
  return config_member<M>{std::make_shared<detail::file_member<M, T, Tr, F, M T::*>>(state_, member)};
}

///
///
template<configuration_c T, transport_c Tr, typename F>
auto config_file<T, Tr, F>::set(T value) -> void
{
  state_->set(std::move(value));
}

///
///
template<configuration_c T, transport_c Tr, typename F>
auto config_file<T, Tr, F>::load(const load_policy policy, const std::source_location loc) -> void
{
  state_->load(policy, loc);
}

///
///
template<configuration_c T, transport_c Tr, typename F>
auto config_file<T, Tr, F>::store(const std::source_location loc) -> void
{
  state_->store(true, loc);
}

///
///
template<configuration_c T, transport_c Tr, typename F>
auto config_file<T, Tr, F>::overwrite(const std::source_location loc) -> void
{
  state_->store(false, loc);
}

} // namespace arude::config
