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
/// caches what it reads:
///
/// ```cpp
/// auto manager = arude::config::config_manager{};
/// const auto uri = arude::config::create_uri("app.toml");
///
/// auto config = manager.load<arude::config::config_test_t>(uri);
/// config.retries = 5;
/// manager.store(uri, config);
/// ```
///
/// arude/config/config_file.hpp is the layer under that, for a single file
/// with no manager and no cache in the way.
///

#include "arude/config/base64.hpp"
#include "arude/config/binary.hpp"
#include "arude/config/config_file.hpp"
#include "arude/config/config_manager.hpp"
#include "arude/config/config_test_v1.hpp"
#include "arude/config/config_test_v2.hpp"
#include "arude/config/endpoint.hpp"
#include "arude/config/migration.hpp"

///
/// Configuration types, and the reading, writing and versioning of them.
/// Documented here so Doxygen produces a namespace index; without it the API
/// is only reachable through the per-file pages.
///
namespace arude::config
{

}
