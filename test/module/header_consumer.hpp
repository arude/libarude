///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// Declares the one function test_module.cpp borrows from the header-consuming
/// half of the mixed-consumer test. Deliberately mentions no arude type, so
/// test_module.cpp still proves a module consumer needs no arude header.
///
#pragma once

///
/// Throws an arude exception from a translation unit that included the header.
/// \throws arude::exception Always.
///
auto throw_from_header_translation_unit() -> void;
