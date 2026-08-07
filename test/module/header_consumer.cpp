///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// The other half of the mixed-consumer test: this translation unit includes
/// the header the ordinary way, while test_module.cpp imports the module. Both
/// are linked into one program, which is what a codebase migrating one file at
/// a time looks like.
///

#include "test/module/header_consumer.hpp"

#include "arude/exception.hpp"

///
///
auto throw_from_header_translation_unit() -> void
{
  throw arude::exception{"thrown from a header consumer"};
}
