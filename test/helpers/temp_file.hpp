///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
#pragma once

#include <filesystem>
#include <string_view>

namespace test_helpers
{

///
/// A path in the system temporary directory, removed when it goes away.
/// Nothing is created: the path is somewhere for the code under test to write
/// to, and the file it leaves behind is what the destructor removes.
///
/// Prefer test_helpers::temp_directory where the test needs more than one file,
/// or where the code under test creates directories of its own. The naming rule
/// is the same either way: one name per test, never one shared by two.
///
class temp_file final
{
public: // Structors / Operators
  ///
  /// Names a file in the temporary directory, without creating it.
  /// \param name Name to use, unique within this test binary.
  ///
  explicit temp_file(std::string_view name);

  ///
  /// Removes the file if it was created.
  ///
  ~temp_file();

  temp_file(const temp_file&) = delete;
  temp_file(temp_file&&) = delete;
  auto operator=(const temp_file&) -> temp_file& = delete;
  auto operator=(temp_file&&) -> temp_file& = delete;

public: // Accessors
  ///
  /// Returns the path named.
  /// \return The path.
  ///
  [[nodiscard]] auto path() const -> const std::filesystem::path&;

private: // Variables
  std::filesystem::path path_;
};

} // namespace test_helpers
