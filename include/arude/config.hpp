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
/// A configuration is one plain aggregate per file, reflected by reflect-cpp
/// and stored as TOML. The application holds the newest version of each type
/// in a config_file over a transport, and injects one member of it into a
/// subsystem through a handle:
///
/// ```cpp
/// auto file = arude::config::config_file<app_config, arude::config::transport_file>{
///   arude::config::transport_file{"app.toml"}};
///
/// file.load();                          // One read, one parse, one whole root.
///
/// auto server = http_server{file.member(&app_config::network)}; // Injected with the member, nothing more.
/// ```
///
/// A member whose type is wrapped in arude::config::versioned<> carries its
/// own version, read as whichever version of its previous_t chain the file
/// declares and migrated to the newest one this build knows. to_toml(),
/// from_toml() and from_toml_migrated() convert between a configuration and
/// TOML text with no transport in the way, for text that never touches one.
///
/// Enumerations of a configuration are reflected by name only. reflect-cpp
/// 0.19.0 cannot reflect a non-flag enum whose underlying type is not int
/// unless rfl::config::enum_range<E> is specialised with bounds of that type;
/// such an enum does not compile in a configuration without the
/// specialisation.
///

#include "arude/config/base64.hpp"
#include "arude/config/binary.hpp"
#include "arude/config/common.hpp"
#include "arude/config/config_file.hpp"
#include "arude/config/config_member.hpp"
#include "arude/config/load_policy.hpp"
#include "arude/config/migration.hpp"
#include "arude/config/toml.hpp"
#include "arude/config/toml_parsers.hpp"
#include "arude/config/toml_reader.hpp"
#include "arude/config/toml_time.hpp"
#include "arude/config/toml_writer.hpp"
#include "arude/config/transport.hpp"
#include "arude/config/transport_file.hpp"
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
