///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// The handle a subsystem is injected with: one typed section of one document.
///
/// Copyable and self-contained: it holds the section and the document through
/// shared pointers, so it keeps both alive and stays valid even where the
/// config_manager it came from was destroyed first. The manager's lifetime is
/// deliberately the application's business.
///
#pragma once

#include "arude/config/document_base.hpp"
#include "arude/config/load_policy.hpp"
#include "arude/config/migration.hpp"

#include <memory>
#include <mutex>
#include <shared_mutex>
#include <source_location>
#include <string_view>
#include <utility>

namespace arude::config::detail
{

///
/// The section of one type, and the value it is a view of.
/// \see config_handle
///
template<configuration_c T>
class section;

} // namespace arude::config::detail

namespace arude::config
{

///
/// A handle to one typed section of one document.
/// get() and set() touch the cache only; load() and store() act on the whole
/// document, because a section cannot be read or written alone — the document
/// parses and emits as one blob, so a section handle that appeared to write
/// only its own section would be lying.
///
/// \tparam T Configuration type.
///
template<configuration_c T>
class config_handle final
{
public: // Structors
  ///
  /// Constructs a handle from the section and the document it belongs to.
  /// Public for config_manager and config_document_handle, which are where a
  /// handle normally comes from.
  ///
  /// \param section The section to view.
  /// \param document The document the section belongs to, kept alive.
  ///
  config_handle(std::shared_ptr<detail::section<T>> section, std::shared_ptr<detail::document_base> document);

public: // Accessors
  ///
  /// Returns a copy of the cached value.
  /// A copy, never a reference: a reference handed out from under the lock is
  /// a data race the caller cannot see.
  ///
  /// \param loc Where the call was made, which is what a failure reports. Left to its default.
  /// \return The cached value.
  /// \throws exception_t With not_found if the section has never been loaded or set.
  ///
  [[nodiscard]] auto get(std::source_location loc = std::source_location::current()) const -> T;

  ///
  /// Replaces the cached value. The document is untouched until store().
  ///
  /// A section that was loaded owns the keys it was read from, and store()
  /// replaces them: a key the new value no longer produces, such as a
  /// std::optional member that was reset, leaves the source. A section that
  /// was never loaded has read nothing and so owns nothing yet: store() merges
  /// its fields over whatever the section holds in the source, and removes no
  /// key from it.
  ///
  /// \param value Value to cache. Not retained.
  ///
  auto set(const T& value) -> void;

  ///
  /// Reports whether a value is cached — the section was loaded or set at
  /// least once. A load under load_policy::create that found the section
  /// missing counts: it leaves the section's defaults cached.
  ///
  /// \return true if get() would find a value.
  ///
  [[nodiscard]] auto loaded() const -> bool;

  ///
  /// Returns the section's name, empty for the root section.
  ///
  /// \return The name.
  ///
  [[nodiscard]] auto name() const -> std::string_view;

public: // Methods
  ///
  /// Loads the whole document this section belongs to.
  /// Delegates to the document: a section cannot be read alone. All or
  /// nothing: where it throws, this section and every other on the document
  /// hold what they held before.
  ///
  /// \param policy What to do about a source or a section that is missing, or
  ///               of another version.
  /// \param loc Where the call was made, which is what a failure reports. Left to its default.
  /// \throws exception_t With not_found if the source, or any section
  ///                     registered on the document, is not there and the
  ///                     policy does not carry load_policy::create; with
  ///                     invalid_payload or invalid_version if a section
  ///                     cannot be read; with the transport's own error, or
  ///                     io_error, if the source cannot be.
  ///
  auto
  load(load_policy policy = load_policy::upgrade_to_current, std::source_location loc = std::source_location::current())
    -> void;

  ///
  /// Stores the whole document this section belongs to.
  /// Delegates to the document: a section cannot be written alone. Whatever
  /// is in the source that no loaded or set section owns is carried over as
  /// it is, whether or not the document was loaded first. A document loaded
  /// carries over the source as that load read it; one never loaded reads the
  /// source again on every store. Stores of one document are written one at a
  /// time, as config_document_handle::store() describes.
  ///
  /// \param loc Where the call was made, which is what a failure reports. Left to its default.
  /// \throws exception_t With read_only if the transport cannot be written;
  ///                     with invalid_payload if the document was never
  ///                     loaded and the source does not parse, in which case
  ///                     the source is left as it is and
  ///                     config_document_handle::discard_source() is the way
  ///                     to replace it; with the transport's own error, or
  ///                     io_error, if the read or the write fails.
  ///
  auto store(std::source_location loc = std::source_location::current()) -> void;

private: // Variables
  std::shared_ptr<detail::section<T>> section_;
  std::shared_ptr<detail::document_base> document_;
};

///
///
template<configuration_c T>
config_handle<T>::config_handle(
  std::shared_ptr<detail::section<T>> section, std::shared_ptr<detail::document_base> document)
  : section_{std::move(section)}
  , document_{std::move(document)}
{
}

///
///
template<configuration_c T>
auto config_handle<T>::get(const std::source_location loc) const -> T
{
  const auto lock = std::shared_lock{document_->mutex()};

  return section_->value(loc);
}

///
///
template<configuration_c T>
auto config_handle<T>::set(const T& value) -> void
{
  const auto lock = std::scoped_lock{document_->mutex()};

  section_->set_value(value);
}

///
///
template<configuration_c T>
auto config_handle<T>::loaded() const -> bool
{
  const auto lock = std::shared_lock{document_->mutex()};

  return section_->engaged();
}

///
///
template<configuration_c T>
auto config_handle<T>::name() const -> std::string_view
{
  return section_->name();
}

///
///
template<configuration_c T>
auto config_handle<T>::load(const load_policy policy, const std::source_location loc) -> void
{
  document_->load(policy, loc);
}

///
///
template<configuration_c T>
auto config_handle<T>::store(const std::source_location loc) -> void
{
  document_->store(loc);
}

} // namespace arude::config
