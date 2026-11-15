///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///

#include "arude/config.hpp"
#include "test/helpers/temp_directory.hpp"
#include "test/helpers/write_text.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <ranges>
#include <string>
#include <system_error>

namespace transport_file_test
{
namespace
{

///
/// Returns the whole file as text.
/// \param path File to read.
/// \return The text.
///
auto text_of(const std::filesystem::path& path) -> std::string
{
  auto stream = std::ifstream{path};
  return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

///
/// Counts what a directory holds, so a test can tell that a write left
/// nothing behind beside the file it wrote.
///
/// \param directory Directory to look in.
/// \return The number of entries in it.
///
auto entries_in(const std::filesystem::path& directory) -> std::ptrdiff_t
{
  return std::ranges::distance(std::filesystem::directory_iterator{directory}, std::filesystem::directory_iterator{});
}

} // namespace

} // namespace transport_file_test

// A write goes to a temporary file that is then renamed over the target, so a
// reader never meets the target empty or half-written. These are the things
// that replacement has to carry over, or clean up, to pass for an in-place write.
SCENARIO("a file transport replaces the file in one step", "[config][transport][transport_file]")
{
  GIVEN("a file with permissions of its own")
  {
    const auto directory = test_helpers::temp_directory{"arude_transport_file_replace"};
    const auto path = directory.file("app.toml");
    test_helpers::write_text(path, "old contents\n");

    std::filesystem::permissions(
      path,
      std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
      std::filesystem::perm_options::replace);
    const auto before = std::filesystem::status(path).permissions();

    WHEN("the transport writes it")
    {
      arude::config::transport_file{path}.write(arude::config::as_bytes("new contents\n"));

      THEN("it holds the new contents")
      {
        REQUIRE(transport_file_test::text_of(path) == "new contents\n");
      }

      THEN("it keeps its permissions")
      {
        REQUIRE(std::filesystem::status(path).permissions() == before);
      }

      THEN("nothing is left beside it")
      {
        REQUIRE(transport_file_test::entries_in(directory.path()) == 1);
      }
    }
  }

  GIVEN("a symbolic link to a file")
  {
    const auto directory = test_helpers::temp_directory{"arude_transport_file_link"};
    const auto target = directory.file("target.toml");
    const auto link = directory.file("link.toml");
    test_helpers::write_text(target, "old contents\n");

    // Windows grants the right to create one only to some accounts.
    auto refused = std::error_code{};
    std::filesystem::create_symlink(target, link, refused);

    if(refused)
    {
      SKIP("symbolic links cannot be created here: " << refused.message());
    }

    WHEN("the transport writes through the link")
    {
      arude::config::transport_file{link}.write(arude::config::as_bytes("new contents\n"));

      THEN("the link is still a link to the same file")
      {
        REQUIRE(std::filesystem::is_symlink(link));
        REQUIRE(std::filesystem::equivalent(link, target));
      }

      THEN("the file it names holds the new contents")
      {
        REQUIRE(transport_file_test::text_of(target) == "new contents\n");
      }
    }
  }

  GIVEN("a path at which there is a directory, which no file can replace")
  {
    const auto directory = test_helpers::temp_directory{"arude_transport_file_blocked"};
    const auto path = directory.file("app.toml");
    std::filesystem::create_directory(path);

    WHEN("the transport writes it")
    {
      THEN("the write is an io_error, and no temporary file is left behind")
      {
        try
        {
          arude::config::transport_file{path}.write(arude::config::as_bytes("new contents\n"));
          FAIL("replacing a directory with a file should have thrown");
        }
        catch(const arude::config::exception_t& ex)
        {
          REQUIRE(ex.data() == arude::config::config_error::io_error);
        }

        REQUIRE(std::filesystem::is_directory(path));
        REQUIRE(transport_file_test::entries_in(directory.path()) == 1);
      }
    }
  }
}
