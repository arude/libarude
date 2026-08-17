///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// The entry point an application reaches for when it wants configuration.
///
/// One manager holds every document an application is configured by, keyed by
/// each document's location, and an index from configuration type to the
/// section handle that owns it. A registration binds one endpoint instance,
/// one transport instance and any number of sections into a document; the
/// manager hands back a config_document_handle and remembers the document.
///
/// ```cpp
/// auto manager = arude::config::config_manager{};
///
/// auto document = manager.register_document(
///   arude::config::endpoint_toml{}, arude::config::transport_file{"/etc/example/app.toml"});
///
/// auto app     = document.add_root<app_config>();
/// auto network = document.add_section<network_config>("network");
///
/// document.load();
/// auto server = http_server{network}; // Injected with the section, nothing more.
/// ```
///
/// It is one instance by construction and not by enforcement — there is no
/// singleton here, and where the instance lives and how long it lives are the
/// application's to decide. Inject the handle a subsystem needs, as
/// docs/cpp-conventions.md asks, rather than reaching for a global.
///
/// get<T>() exists deliberately as a convenience: it is a service-locator
/// shape, which pulls against the project's "inject collaborators through the
/// constructor" rule. Wiring code may use it; everything else should be
/// handed the config_handle<T> at construction.
///
#pragma once

#include "arude/config/config_document.hpp"
#include "arude/config/config_handle.hpp"
#include "arude/config/endpoint.hpp"
#include "arude/config/load_policy.hpp"
#include "arude/config/transport.hpp"
#include "arude/exception_report.hpp"
#include "arude/noncopyable.hpp"
#include "arude/type_key.hpp"
#include "arude/type_name.hpp"

#include <cstddef>
#include <format>
#include <memory>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace arude::config
{

///
/// The manager: two registries, documents by location and sections by type.
///
/// Move-only, through arude::noncopyable: two managers over one set of
/// sources would each cache half the truth, while moving one into place after
/// building it is exactly how an application is meant to get hold of one.
///
class config_manager final : public noncopyable
{
public: // Typedefs / Constants
  ///
  /// What went wrong, carried as the payload of every exception thrown here.
  /// Defined in arude/config/common.hpp; this alias keeps call sites spelling
  /// it config_manager::error_t.
  ///
  using error_t = arude::config::config_error;

  ///
  /// What load() should do about a source that is missing or of another version.
  /// Defined in arude/config/load_policy.hpp; this alias keeps call sites
  /// spelling it config_manager::load_policy_t.
  ///
  using load_policy_t = arude::config::load_policy;

public: // Methods
  ///
  /// Registers a document: one endpoint instance and one transport instance
  /// bound together, ready to take sections through
  /// config_document_handle::add_root() and add_section().
  ///
  /// Registration is a startup act, and a location already registered is
  /// refused rather than handed back bound to a different endpoint and
  /// transport: two documents over one source would each write half the
  /// truth.
  ///
  /// \tparam E Endpoint type, moved in.
  /// \tparam Tr Transport type, moved in. The manager keys on its location().
  /// \param format The endpoint instance.
  /// \param carrier The transport instance.
  /// \return The document handle, the only way to add sections to it.
  /// \throws exception_t With already_registered_location if a document is already there.
  ///
  template<endpoint_c E, transport_c Tr>
  [[nodiscard]] auto register_document(E format, Tr carrier) -> config_document_handle;

  ///
  /// Returns a copy of the value cached for T.
  /// A convenience: see the class documentation for where it is meant to be
  /// used, and prefer the handle everywhere else.
  ///
  /// \tparam T Configuration type.
  /// \return The cached value.
  /// \throws exception_t With not_found if T is registered nowhere, and
  ///                     ambiguous_type if it is registered in more than one place.
  ///
  template<configuration_c T>
  [[nodiscard]] auto get() const -> T;

  ///
  /// Returns the section handle T is registered as.
  ///
  /// \tparam T Configuration type.
  /// \return The handle.
  /// \throws exception_t With not_found if T is registered nowhere, and
  ///                     ambiguous_type if it is registered in more than one place.
  ///
  template<configuration_c T>
  [[nodiscard]] auto find() const -> config_handle<T>;

public: // Accessors
  ///
  /// Reports whether a document is registered at the location.
  ///
  /// \param location Location to look for.
  /// \return true if register_document() has been called with it.
  ///
  [[nodiscard]] auto contains(std::string_view location) const -> bool;

  ///
  /// Returns how many documents are registered.
  /// \return The count.
  ///
  [[nodiscard]] auto size() const -> std::size_t;

public: // Methods
  ///
  /// Loads every registered document.
  ///
  /// \param policy What to do about a missing or differently versioned document.
  ///
  auto load_all(load_policy policy = load_policy::upgrade_to_current) -> void;

private: // Helpers
  ///
  /// Registers one section with the type index, keeping every registration
  /// so an ambiguity error can name the locations.
  ///
  /// \param key The type key, from type_key<T>().
  /// \param section The section to register. Kept.
  /// \param document The document it belongs to. Kept.
  ///
  auto index_type(
    const void* key, std::shared_ptr<detail::section_base> section, std::shared_ptr<detail::document_base> document)
    -> void;

private: // Variables
  mutable std::shared_mutex mutex_;
  std::unordered_map<std::string, std::shared_ptr<detail::document_base>> documents_;
  std::unordered_map<const void*, detail::type_entry> types_; // Keyed by type_key<T>().
};

///
///
template<endpoint_c E, transport_c Tr>
auto config_manager::register_document(E format, Tr carrier) -> config_document_handle
{
  const auto location = carrier.location();

  {
    const auto lock = std::scoped_lock{mutex_};

    if(documents_.contains(location))
    {
      throw exception_t{
        std::format("arude::config: '{}' is already registered.", location), config_error::already_registered_location};
    }
  }

  auto document = std::make_shared<detail::document<E, Tr>>(
    std::move(format),
    std::move(carrier),
    [this](const void* key, std::shared_ptr<detail::section_base> section, std::shared_ptr<detail::document_base> doc)
      -> void { index_type(key, std::move(section), std::move(doc)); });

  {
    const auto lock = std::scoped_lock{mutex_};

    if(documents_.contains(location))
    {
      throw exception_t{
        std::format("arude::config: '{}' is already registered.", location), config_error::already_registered_location};
    }

    documents_.emplace(location, document);
  }

  return config_document_handle{std::move(document)};
}

///
///
template<configuration_c T>
auto config_manager::get() const -> T
{
  return find<T>().get();
}

///
///
template<configuration_c T>
auto config_manager::find() const -> config_handle<T>
{
  const auto lock = std::shared_lock{mutex_};

  const auto found = types_.find(type_key<T>());

  if(found == types_.end() || found->second.registrations.empty())
  {
    throw exception_t{std::format("arude::config: {} is not registered.", type_name<T>()), config_error::not_found};
  }

  if(found->second.registrations.size() > 1)
  {
    auto locations = std::string{};

    for(const auto& registration : found->second.registrations)
    {
      if(!locations.empty())
      {
        locations += ", ";
      }

      locations += '\'';
      locations += registration.document->location();
      locations += '\'';
    }

    throw exception_t{
      std::format(
        "arude::config: {} is registered in {} places: {}.",
        type_name<T>(),
        found->second.registrations.size(),
        locations),
      config_error::ambiguous_type};
  }

  const auto& registration = found->second.registrations.front();

  return config_handle<T>{std::static_pointer_cast<detail::section<T>>(registration.section), registration.document};
}

///
///
inline auto config_manager::contains(const std::string_view location) const -> bool
{
  const auto lock = std::shared_lock{mutex_};

  return documents_.contains(std::string{location});
}

///
///
inline auto config_manager::size() const -> std::size_t
{
  const auto lock = std::shared_lock{mutex_};

  return documents_.size();
}

///
///
inline auto config_manager::load_all(const load_policy policy) -> void
{
  std::vector<std::shared_ptr<detail::document_base>> documents;

  {
    const auto lock = std::shared_lock{mutex_};

    for(const auto& [location, document] : documents_)
    {
      documents.push_back(document);
    }
  }

  for(const auto& document : documents)
  {
    document->load(policy);
  }
}

///
///
inline auto config_manager::index_type(
  const void* key, std::shared_ptr<detail::section_base> section, std::shared_ptr<detail::document_base> document)
  -> void
{
  const auto lock = std::scoped_lock{mutex_};

  types_[key].registrations.push_back(
    detail::type_entry::registration{.section = std::move(section), .document = std::move(document)});
}

} // namespace arude::config
