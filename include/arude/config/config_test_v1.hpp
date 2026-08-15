///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// Version 1 of the worked example configuration.
///
/// Released, and therefore frozen: what this type holds is what version 1
/// means, and a file written by any build that had it still reads back here.
/// A change to the configuration adds config_test_v2.hpp; it does not edit
/// this file.
///
#pragma once

#include "arude/config/binary.hpp"
#include "arude/config/migration.hpp"

#include <cstdint>
#include <string>

namespace arude::config
{

///
/// The example configuration, version 1.
/// An aggregate with public data members, which is what reflect-cpp reflects.
/// Every member carries a default, so config_test_v1{} is already a usable
/// configuration and an application has something to store on its first run.
///
struct config_test_v1
{
  ///
  /// The version this type is.
  /// Static, so it is available without a value in hand, and the one place the
  /// number is written down.
  ///
  static constexpr auto config_version = version_t{1};

  ///
  /// The version carried in the file.
  /// Initialised from config_version so the two cannot disagree; store()
  /// writes the type's version regardless of what this holds.
  ///
  version_t version = config_version;

  ///
  /// Name this configuration is known by.
  ///
  std::string name = "arude";

  ///
  /// Times an operation is retried before it is given up on.
  ///
  std::uint32_t retries = 3;

  ///
  /// Opaque payload, written to the file as base64 text.
  ///
  binary secret;
};

} // namespace arude::config
