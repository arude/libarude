///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// The module interface. A facade over the headers rather than a second
/// implementation: they stay canonical and are not aware this file exists.
///
/// The headers are included in the global module fragment, so every entity
/// keeps global-module linkage and is the same entity a header consumer sees.
/// Attaching them to the module instead — by compiling the headers in the
/// purview behind an export macro — would give a module consumer and a header
/// consumer two distinct arude::exception types. Nothing would fail to compile
/// or link; a throw from one would simply walk past catch(const exception_base&)
/// in the other. For an exception library that is the wrong trade at any price.
/// test/module/ links both consumption styles into one program to hold that.
///

module;

// Everything the headers need has to be included here. An include in the
// purview would re-declare all of std as module-attached.
#include "arude/enum.hpp"
#include "arude/exception.hpp"
#include "arude/exception_report.hpp"
#include "arude/hello_world.hpp"
#include "arude/noncopyable.hpp"
#include "arude/pimpl_owner.hpp"
#include "arude/type_name.hpp"

export module arude;

namespace arude::detail
{

// Reachability anchors. A declaration in the global module fragment is
// discarded unless it is decl-reachable from an exported declaration, and a
// std::formatter specialization for a re-exported type is not: nothing in the
// export list below mentions it. Naming each one in an alias that is itself
// exported is what keeps it.
//
// This is not belt-and-braces. GCC prunes as the standard allows and clang
// keeps the whole fragment, so without these, formatting an arude::exception
// through the module compiles under clang and fails under GCC only. The
// specializations cannot simply be moved into the purview instead: the headers
// format std::source_location themselves, so they need it while they are being
// included, which is before the purview begins.
using source_location_formatter_t = std::formatter<std::source_location>;
using exception_base_formatter_t = std::formatter<arude::exception_base>;

} // namespace arude::detail

///
/// The public interface, re-exported.
/// A using-declaration carries the whole overload set, so exception_report
/// needs one entry rather than one per overload. A public name added to a
/// header and not listed here is invisible to module consumers and to nobody
/// else, which is what test/module/test_module.cpp exists to catch.
///
export namespace arude
{

// The enum names are magic_enum's entities, re-exported a second time. They are
// attached to the global module like every other header entity here, so a
// module consumer and a header consumer share one enum_name and not two.
using ::arude::case_insensitive;
using ::arude::enum_c;
using ::arude::enum_cast;
using ::arude::enum_cast_throw;
using ::arude::enum_contains;
using ::arude::enum_count;
using ::arude::enum_entries;
using ::arude::enum_index;
using ::arude::enum_integer;
using ::arude::enum_name;
using ::arude::enum_names;
using ::arude::enum_type_name;
using ::arude::enum_value;
using ::arude::enum_values;

using ::arude::exception;
using ::arude::exception_base;
using ::arude::exception_report;
using ::arude::exception_stacktrace_t;
using ::arude::exception_string_t;
using ::arude::exception_user_data_c;
using ::arude::hello_world;
using ::arude::noncopyable;
using ::arude::pimpl_owner;
using ::arude::stacktrace_available;
using ::arude::type_name;

} // namespace arude

export namespace arude::detail
{

using ::arude::detail::exception_base_formatter_t;
using ::arude::detail::source_location_formatter_t;

} // namespace arude::detail
