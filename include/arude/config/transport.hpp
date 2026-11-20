///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// Transports: where a configuration is kept, as opposed to what it is written
/// in.
///
/// A transport carries its own address, in its own natural type — a path, a
/// URL, an EEPROM slot — and deals in bytes, not text. A config_file deals in
/// a configuration and knows nothing about files or sockets; a transport
/// deals in bytes and knows nothing about TOML or versions. Neither has to be
/// touched when the other gains a member.
///
/// The contract is a concept rather than a base class: a transport has state
/// (its address) and its operations are ordinary const members, so there is
/// nothing to inherit and no virtuals to route through. Read and write take no
/// address because the address is the instance's own — one blob per transport
/// instance is the assumption the whole design rests on.
///
/// A read is one step: it returns the bytes, or says that there is nothing at
/// the address, in a single request. There is no separate question of whether
/// anything is there, so a file cannot vanish between that question and the
/// read, and a transport over a network makes one request where it would
/// otherwise make two. Saying "nothing there" is all an empty result may mean;
/// every other failure is thrown.
///
/// A transport says whether it can be written as well as read. A store on a
/// read-only one fails with config_error::read_only rather than at the point of
/// writing, so a configuration served over HTTPS is refused before anything is
/// serialized.
///
#pragma once

#include <concepts>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace arude::config
{

///
/// The transport contract.
/// Tr::name is a plain label for diagnostics. location() is what errors quote;
/// it is the transport's own business to prefix it with its name, so a file
/// and an EEPROM slot spelled alike stay distinct.
///
/// read() returns the bytes at the address, or std::nullopt where nothing is
/// there, and means nothing else by std::nullopt. Every other failure throws:
/// an exception_t with the error that fits, usually io_error, or anything
/// else, which a config_file reports as io_error. A failure must never come
/// back as std::nullopt. load_policy::create would then hand out defaults for
/// a source that is there but could not be read, and the next store() would
/// overwrite it with them.
///
/// read() and write() may take a trailing std::source_location as well, which
/// is the call site a failure should report. A transport that takes one is
/// handed it, through detail::read_from() and detail::write_to(); it still has
/// to be callable without one, and so has to default it.
///
/// \tparam Tr Type to test.
///
template<typename Tr>
concept transport_c = std::move_constructible<Tr> && requires(const Tr& carrier, std::span<const std::byte> bytes) {
  { Tr::name } -> std::convertible_to<std::string_view>; // Label, for diagnostics.
  { carrier.location() } -> std::same_as<std::string>;   // Identity and error text.
  { carrier.writable() } -> std::same_as<bool>;
  { carrier.read() } -> std::same_as<std::optional<std::vector<std::byte>>>;
  { carrier.write(bytes) } -> std::same_as<void>;
};

///
/// A transport whose address is a filesystem path.
/// Exists so a medium-integrated component can require it and fail at the
/// interface when it is mispaired with a transport that has no path.
///
/// \tparam Tr Type to test.
///
template<typename Tr>
concept path_transport_c = transport_c<Tr> && requires(const Tr& carrier) {
  { carrier.path() } -> std::same_as<std::filesystem::path>;
};

} // namespace arude::config

namespace arude::config::detail
{

///
/// Reads through a transport, handing it the call site where it takes one.
///
/// \tparam Tr Transport type.
/// \param carrier The transport.
/// \param loc The call site a failure should report.
/// \return Whatever the transport's read() returns.
/// \throws Whatever the transport's read() throws.
///
template<transport_c Tr>
[[nodiscard]] auto read_from(const Tr& carrier, std::source_location loc) -> std::optional<std::vector<std::byte>>;

///
/// Writes through a transport, handing it the call site where it takes one.
///
/// \tparam Tr Transport type.
/// \param carrier The transport.
/// \param bytes Bytes to write. Not retained.
/// \param loc The call site a failure should report.
/// \throws Whatever the transport's write() throws.
///
template<transport_c Tr>
auto write_to(const Tr& carrier, std::span<const std::byte> bytes, std::source_location loc) -> void;

///
///
template<transport_c Tr>
auto read_from(const Tr& carrier, const std::source_location loc) -> std::optional<std::vector<std::byte>>
{
  if constexpr(requires { carrier.read(loc); })
  {
    return carrier.read(loc);
  }
  else
  {
    return carrier.read();
  }
}

///
///
template<transport_c Tr>
auto write_to(const Tr& carrier, const std::span<const std::byte> bytes, const std::source_location loc) -> void
{
  if constexpr(requires { carrier.write(bytes, loc); })
  {
    carrier.write(bytes, loc);
  }
  else
  {
    carrier.write(bytes);
  }
}

} // namespace arude::config::detail
