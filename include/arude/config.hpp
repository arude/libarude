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
/// Include this one rather than the headers below. It covers the library and
/// nothing else: the version headers of a configuration are the application's
/// own, and this header cannot include them. Whether a configuration can be
/// upgraded is decided by which of them a translation unit has seen, so
/// including the newest version's header wherever the configuration is used
/// is the application's to do; see arude/config/migration.hpp.
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
/// one read of the file and nothing else, giving the endpoint's own tree, or
/// std::nullopt where there is no file. read_section() and write_section()
/// take a section out of such a tree and put one back.
/// arude/config/endpoint_toml.hpp also carries to_toml() and from_toml() for
/// text that never reaches a transport at all.
///
/// An endpoint of an application's own implements tree_reader and
/// tree_writer, in arude/config/tree_reader.hpp and tree_writer.hpp, over its
/// own tree, and satisfies endpoint_c; see arude/config/endpoint.hpp.
///

#include "arude/config/base64.hpp"
#include "arude/config/binary.hpp"
#include "arude/config/common.hpp"
#include "arude/config/config_document.hpp"
#include "arude/config/config_file.hpp"
#include "arude/config/config_handle.hpp"
#include "arude/config/config_manager.hpp"
#include "arude/config/config_member.hpp"
#include "arude/config/document_base.hpp"
#include "arude/config/endpoint.hpp"
#include "arude/config/endpoint_toml.hpp"
#include "arude/config/load_policy.hpp"
#include "arude/config/migration.hpp"
#include "arude/config/toml.hpp"
#include "arude/config/toml_parsers.hpp"
#include "arude/config/toml_reader.hpp"
#include "arude/config/toml_tree_reader.hpp"
#include "arude/config/toml_tree_writer.hpp"
#include "arude/config/toml_writer.hpp"
#include "arude/config/transport.hpp"
#include "arude/config/transport_file.hpp"
#include "arude/config/tree_reader.hpp"
#include "arude/config/tree_writer.hpp"
#include "arude/config/type_registry.hpp"
#include "arude/config/versioned.hpp"

// Out of the sorted group above because it is conditional. The HTTPS transport
// is the one part of the configuration support that has to be asked for, since
// TLS is a dependency the build cannot fetch on its own; see the header for why.
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
