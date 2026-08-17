///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// Version 1 of the example configuration the tests migrate, store and load.
///
/// A fixture rather than library API: nothing in arude/config/ mentions it, and
/// a consumer writes its own configuration types the way this pair is written.
/// It lives here so the library ships no example type a consumer could mistake
/// for something to build on.
///
/// Released, and therefore frozen: what this type holds is what version 1
/// means, and text written by any build that had it still reads back here. A
/// change to the configuration adds config_test_v2.hpp; it does not edit this
/// file.
///
#pragma once

#include "arude/config/binary.hpp"
#include "arude/config/migration.hpp"

#include <cstdint>
#include <string>

namespace test_helpers
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
  static constexpr auto config_version = arude::config::version_t{1};

  ///
  /// The version carried in the text.
  /// Initialised from config_version so the two cannot disagree; writing a
  /// configuration stamps the type's version regardless of what this holds.
  ///
  arude::config::version_t version = config_version;

  ///
  /// Name this configuration is known by.
  ///
  std::string name = "arude";

  ///
  /// Times an operation is retried before it is given up on.
  ///
  std::uint32_t retries = 3;

  ///
  /// Opaque payload, written as base64 text.
  ///
  arude::config::binary secret;
};

} // namespace test_helpers
