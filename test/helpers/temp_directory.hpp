///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// Helpers shared by the tests live in test/helpers/, one per file, in
/// namespace test_helpers. They are compiled into the main test binary by the
/// glob in CMakeLists.txt; a test target that lists its sources by hand, as
/// test/module does, has to name the matching .cpp itself.
///
#pragma once

#include <filesystem>
#include <string_view>

namespace test_helpers
{

///
/// A directory under the system temporary directory, removed when it goes away.
/// Anything the test wrote into it goes with it, so a test that leaves a file
/// behind on failure does not leave it for the next run to trip over.
///
/// Give each test its own name: Catch2 runs the scenarios of one binary in one
/// process, and ctest may run several binaries at once, so a name shared by two
/// tests is two tests writing the same directory.
///
class temp_directory final
{
public: // Structors / Operators
  ///
  /// Creates the directory, replacing one of the same name left by an earlier run.
  /// \param name Name to use, unique within this test binary.
  ///
  explicit temp_directory(std::string_view name);

  ///
  /// Removes the directory and everything in it.
  ///
  ~temp_directory();

  temp_directory(const temp_directory&) = delete;
  temp_directory(temp_directory&&) = delete;
  auto operator=(const temp_directory&) -> temp_directory& = delete;
  auto operator=(temp_directory&&) -> temp_directory& = delete;

public: // Accessors
  ///
  /// Returns the directory itself.
  /// \return The path.
  ///
  [[nodiscard]] auto path() const -> const std::filesystem::path&;

  ///
  /// Returns the path of a file in the directory.
  /// \param name File name.
  /// \return The path, whether or not anything is there.
  ///
  [[nodiscard]] auto file(std::string_view name) const -> std::filesystem::path;

private: // Variables
  std::filesystem::path path_;
};

} // namespace test_helpers
