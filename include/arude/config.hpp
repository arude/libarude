///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
#pragma once

///
/// Umbrella header for libarude's configuration support.
///
/// Include this one rather than the headers below. Whether a configuration can
/// be upgraded is decided by which version headers a translation unit has
/// seen — see arude/config/migration.hpp — so every translation unit should
/// see the same set, and this header is what guarantees that.
///
/// A configuration is a plain aggregate, reflected by reflect-cpp and stored
/// as TOML. An application goes through arude::config::config_manager, which
/// registers documents and hands out section handles:
///
/// ```cpp
/// auto manager = arude::config::config_manager{};
///
/// auto document = manager.register_document(
///   arude::config::endpoint_toml{}, arude::config::transport_file{"app.toml"});
///
/// auto app     = document.add_root<app_config>();
/// auto network = document.add_section<network_config>("network");
///
/// document.load();                       // One read, one parse, all sections filled.
///
/// auto server = http_server{network};    // Injected with the section, nothing more.
/// ```
///
/// Under that sits the pair the document is built from, usable directly where
/// a manager and a cache would be in the way: an endpoint is the format and a
/// transport is the place, so endpoint_toml{}.parse(transport_file{path}) is
/// one file read and nothing else. arude/config/endpoint_toml.hpp also carries
/// to_toml() and from_toml() for text that never reaches a transport at all.
///

#include "arude/config/base64.hpp"
#include "arude/config/binary.hpp"
#include "arude/config/common.hpp"
#include "arude/config/config_document.hpp"
#include "arude/config/config_handle.hpp"
#include "arude/config/config_manager.hpp"
#include "arude/config/endpoint.hpp"
#include "arude/config/endpoint_toml.hpp"
#include "arude/config/load_policy.hpp"
#include "arude/config/migration.hpp"
#include "arude/config/transport.hpp"
#include "arude/config/transport_file.hpp"

// Out of the sorted group above because it is conditional. The HTTPS transport
// is the one part of libarude that has to be asked for, since TLS is a
// dependency the build cannot fetch on its own; see the header for why.
#if (defined ARUDE_HTTPS)
  #include "arude/config/transport_https.hpp"
#endif // #if (defined ARUDE_HTTPS)

///
/// Configuration types, and the reading, writing and versioning of them.
/// Documented here so Doxygen produces a namespace index; without it the API
/// is only reachable through the per-file pages.
///
namespace arude::config
{

}
