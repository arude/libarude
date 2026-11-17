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

#include "arude/config/common.hpp"
#include "arude/config/config_document.hpp"
#include "arude/config/config_handle.hpp"
#include "arude/config/document_base.hpp"
#include "arude/config/endpoint.hpp"
#include "arude/config/load_policy.hpp"
#include "arude/config/migration.hpp"
#include "arude/config/transport.hpp"
#include "arude/config/type_registry.hpp"
#include "arude/noncopyable.hpp"
#include "arude/type_key.hpp"
#include "arude/type_name.hpp"

#include <cstddef>
#include <format>
#include <memory>
#include <mutex>
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
/// Neither copyable nor movable. arude::noncopyable takes the copies away,
/// because two managers over one set of sources would each cache half the
/// truth, and the mutex guarding the documents takes the moves with it. So a
/// manager is constructed in place, where it is to live; what travels is the
/// handles, which stay valid after the manager is destroyed.
///
class config_manager final : public noncopyable
{
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
  /// \tparam E Endpoint type, moved in. It has to satisfy endpoint_c<E, Tr>:
  ///            an endpoint that constrains the transport it reads and writes
  ///            through is refused here, at the call, when paired with one
  ///            that does not fit.
  /// \tparam Tr Transport type, moved in. The manager keys on its location().
  /// \param format The endpoint instance.
  /// \param carrier The transport instance.
  /// \return The document handle, the only way to add sections to it.
  /// \throws exception_t With already_registered_location if a document is already there.
  ///
  template<typename E, transport_c Tr>
    requires endpoint_c<E, Tr>
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
  /// Each document's load is all or nothing, but the documents are loaded one
  /// after another: where one throws, those loaded before it keep what they
  /// read, and those after it are not loaded.
  ///
  /// \param policy What to do about a source or a section that is missing, or
  ///               of another version.
  /// \throws exception_t As config_document_handle::load does, for the first
  ///                     document that fails.
  ///
  auto load_all(load_policy policy = load_policy::upgrade_to_current) -> void;

private:                            // Variables
  mutable std::shared_mutex mutex_; // Guards documents_; the type index locks itself.
  std::unordered_map<std::string, std::shared_ptr<detail::document_base>> documents_;

  // Shared, so that a document can hold it weakly and find out that the
  // manager is gone rather than reach into a destroyed one.
  std::shared_ptr<detail::type_registry> types_ = std::make_shared<detail::type_registry>();
};

///
///
template<typename E, transport_c Tr>
  requires endpoint_c<E, Tr>
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

  auto document = std::make_shared<detail::document<E, Tr>>(std::move(format), std::move(carrier), types_);

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
  // A copy, so the registry is not held while the documents are asked for
  // their locations below.
  const auto registrations = types_->registrations(type_key<T>());

  if(registrations.empty())
  {
    throw exception_t{std::format("arude::config: {} is not registered.", type_name<T>()), config_error::not_found};
  }

  if(registrations.size() > 1)
  {
    auto locations = std::string{};

    for(const auto& registration : registrations)
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
      std::format("arude::config: {} is registered in {} places: {}.", type_name<T>(), registrations.size(), locations),
      config_error::ambiguous_type};
  }

  const auto& registration = registrations.front();

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

} // namespace arude::config
