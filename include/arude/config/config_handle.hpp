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

#include "arude/config/load_policy.hpp"
#include "arude/config/migration.hpp"
#include "arude/type_traits.hpp"

#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string_view>
#include <utility>

namespace arude::config::detail
{

class document_base;

///
/// The erased document a handle reaches through.
/// This header is parsed before document_base is defined, so a member access
/// written against the bare type would be diagnosed here as access into an
/// incomplete type. dependent_t makes the member's type depend on T, so the
/// access is a dependent expression and is only checked where the handle is
/// instantiated — by which point document_base is complete.
///
/// \tparam T Configuration type. Carried to make the alias dependent, and nothing else.
///
template<configuration_c T>
using document_ptr_t = std::shared_ptr<dependent_t<T, document_base>>;

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
  /// \return The cached value.
  ///
  [[nodiscard]] auto get() const -> T;

  ///
  /// Replaces the cached value. The document is untouched until store().
  ///
  /// \param value Value to cache. Not retained.
  ///
  auto set(const T& value) -> void;

  ///
  /// Reports whether a value is cached — the section was loaded or set at
  /// least once.
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
  /// Delegates to the document: a section cannot be read alone.
  ///
  /// \param policy What to do about a missing or differently versioned document.
  ///
  auto load(load_policy policy = load_policy::upgrade_to_current) -> void;

  ///
  /// Stores the whole document this section belongs to.
  /// Delegates to the document: a section cannot be written alone.
  ///
  auto store() -> void;

private: // Variables
  std::shared_ptr<detail::section<T>> section_;
  detail::document_ptr_t<T> document_;
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
auto config_handle<T>::get() const -> T
{
  const auto lock = std::shared_lock{document_->mutex()};

  return section_->value();
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
auto config_handle<T>::load(const load_policy policy) -> void
{
  document_->load(policy);
}

///
///
template<configuration_c T>
auto config_handle<T>::store() -> void
{
  document_->store();
}

} // namespace arude::config
