///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// Version 2 of the worked example configuration, and the step to and from
/// version 1.
///
/// The pattern a version 3 would follow: include the version before it, name
/// it as previous_t, declare upgrade() and downgrade() for the one step
/// between the two, and move config_test_t here. Version 1 is not touched.
///
#pragma once

#include "arude/config/config_test_v1.hpp"
#include "arude/config/migration.hpp"

#include <cstdint>
#include <string>

namespace arude::config
{

///
/// The example configuration, version 2.
/// Adds the endpoint that version 1 had no way of expressing.
///
struct config_test_v2
{
  ///
  /// The version before this one, which migration walks back through.
  ///
  using previous_t = config_test_v1;

  ///
  /// \see config_test_v1::config_version
  ///
  static constexpr auto config_version = version_t{2};

  ///
  /// \see config_test_v1::version
  ///
  version_t version = config_version;

  ///
  /// \see config_test_v1::name
  ///
  std::string name = "arude";

  ///
  /// \see config_test_v1::retries
  ///
  std::uint32_t retries = 3;

  ///
  /// Host the configured service is reached at.
  /// New in version 2. A version 1 file has nothing to say about it, so an
  /// upgraded configuration takes the default below.
  ///
  std::string endpoint = "localhost";

  ///
  /// \see config_test_v1::secret
  ///
  binary secret;
};

///
/// The version of the example configuration this build uses.
/// Moves to the newest version as one is added, so an application that spells
/// its configuration config_test_t follows along by recompiling. The version
/// types themselves stay where they are.
///
using config_test_t = config_test_v2;

///
/// Produces a version 2 configuration from a version 1 one.
/// endpoint has no version 1 counterpart and takes its default; every other
/// member carries across unchanged.
///
/// \param val Configuration to upgrade. Not retained.
/// \return The same configuration as version 2.
///
[[nodiscard]] constexpr auto upgrade(const config_test_v1& val) -> config_test_v2;

///
/// Produces a version 1 configuration from a version 2 one.
/// endpoint is dropped, because version 1 has nowhere to keep it: downgrading
/// and upgrading again returns the default rather than what was there. That is
/// what makes this lossy, and it is the reason to keep the newest version's
/// file around rather than overwrite it with a downgraded one.
///
/// \param val Configuration to downgrade. Not retained.
/// \return The same configuration as version 1, less what version 1 cannot hold.
///
[[nodiscard]] constexpr auto downgrade(const config_test_v2& val) -> config_test_v1;

///
///
constexpr auto upgrade(const config_test_v1& val) -> config_test_v2
{
  return config_test_v2{.name = val.name, .retries = val.retries, .secret = val.secret};
}

///
///
constexpr auto downgrade(const config_test_v2& val) -> config_test_v1
{
  return config_test_v1{.name = val.name, .retries = val.retries, .secret = val.secret};
}

} // namespace arude::config
