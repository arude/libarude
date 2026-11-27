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
/// Every field of a configuration is required: a source that lacks one does
/// not load. A field that may be left out is a std::optional, which reads as
/// std::nullopt where it is missing, whatever its initialiser says;
/// reflect-cpp reads a std::shared_ptr the same way, as null.
/// load_policy::create only stands in for a source that is not there at all.
///
/// Enumerations of a configuration are reflected by name only, and only the
/// enumerators within reflect-cpp's range: -256 to 256 by default, cut to
/// what the underlying type holds, and rfl::config::enum_range<E> where that
/// is specialised.
///
/// A configuration type that contains a versioned<> member or an
/// rfl::TaggedUnion anywhere must not also contain a std::variant, an
/// rfl::Variant or an rfl::Result anywhere: that is a compile error in
/// read_root() and write_root(). reflect-cpp reads an untagged variant as the
/// first alternative that accepts the table, so a newer version reached
/// through a later one could be misread or overwritten; rfl::TaggedUnion is
/// the way to have a choice in a versioned configuration, and its tag picks
/// the one alternative read. A type with none of them may use variants
/// freely. A tag that names no alternative is taken for one a newer build
/// added, so it is refused on load and store() does not write over it; see
/// arude/config/tagged_union.hpp.
///
/// The support builds on reflect-cpp's implementation as well as its public
/// API: it specialises rfl::parsing::Parser and rfl::parsing::MapReader for its
/// own reader and writer, hides the reader's read_array() over the internal
/// rfl::parsing::VectorReader, ArrayReader and TupleReader, and writes a
/// tagged union through rfl::internal's tag helpers and reflect-cpp's
/// TaggedUnionWrapper types. reflect-cpp is pinned for that reason, and an
/// upgrade of it is a change to test on its own; see README.md.
///

#include "arude/config/base64.hpp"
#include "arude/config/binary.hpp"
#include "arude/config/common.hpp"
#include "arude/config/config_file.hpp"
#include "arude/config/config_member.hpp"
#include "arude/config/load_policy.hpp"
#include "arude/config/migration.hpp"
#include "arude/config/tagged_union.hpp"
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
