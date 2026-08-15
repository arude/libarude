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
/// The configuration support is carried under the same guard the headers use.
/// ARUDE_NO_CONFIG is a PUBLIC compile definition of the library target, so it
/// arrives here from the build that decided it, and the module offers what the
/// build has: with the support declined, reflect-cpp and Boost.URL were never
/// fetched, the header cannot compile, and there are no names to export.
///

module;

// Everything the headers need has to be included here. An include in the
// purview would re-declare all of std as module-attached.
#include "arude/enum.hpp"
#include "arude/exception.hpp"
#include "arude/exception_report.hpp"
#include "arude/hello_world.hpp"
#include "arude/noncopyable.hpp"
#include "arude/type_name.hpp"

// Out of the sorted group above because it is conditional, and guarded exactly
// as arude/arude.hpp guards it. This pulls reflect-cpp's and Boost.URL's
// headers into the fragment as well, which is what a module consumer of the
// configuration interface needs and costs a header consumer nothing.
#if !(defined ARUDE_NO_CONFIG)
  #include "arude/config.hpp"
#endif // #if !(defined ARUDE_NO_CONFIG)

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

#if !(defined ARUDE_NO_CONFIG)

namespace arude::config::detail
{

// Reachability anchors, for the reason given above. The reflector is the one
// that matters most: it is what stores a binary payload as base64, and it is
// found by name lookup where a configuration holding one is parsed, which
// happens in the consumer's translation unit rather than here. Discarded, it
// would take a configuration with a payload down with it and leave the rest of
// the interface working.
//
// The second anchors one instantiation of the formatter arude/enum.hpp carries
// for every enum, which is the instantiation the library's own error reports
// need. It cannot do more than that: what the fragment holds is a partial
// specialization, a using-declaration can only name an instantiation of one,
// and so a module consumer formatting an enum of its own is relying on that
// partial specialization surviving the prune rather than on anything here.
using binary_reflector_t = rfl::Reflector<arude::config::binary>;
using config_errors_formatter_t = std::formatter<arude::config::config_manager::errors>;

} // namespace arude::config::detail

#endif // #if !(defined ARUDE_NO_CONFIG)

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
using ::arude::stacktrace_available;
using ::arude::type_name;

} // namespace arude

export namespace arude::detail
{

using ::arude::detail::exception_base_formatter_t;
using ::arude::detail::source_location_formatter_t;

} // namespace arude::detail

#if !(defined ARUDE_NO_CONFIG)

///
/// The configuration interface, re-exported.
/// Everything arude/config.hpp offers, under the same guard: a build that
/// declined the support has none of these names to export, and a module
/// consumer of that build sees the module without them rather than a module
/// that fails to compile.
///
export namespace arude::config
{

using ::arude::config::base64_decode;
using ::arude::config::base64_decoded_size;
using ::arude::config::base64_encode;
using ::arude::config::base64_encoded_size;
using ::arude::config::binary;
using ::arude::config::byte_range_c;
using ::arude::config::config_manager;
using ::arude::config::config_test_t;
using ::arude::config::config_test_v1;
using ::arude::config::config_test_v2;
using ::arude::config::configuration_c;
using ::arude::config::create_uri;
using ::arude::config::downgradable_configuration_c;
using ::arude::config::downgrade;
using ::arude::config::endpoint_c;
using ::arude::config::endpoint_toml;
using ::arude::config::from_toml;
using ::arude::config::from_toml_migrated;
using ::arude::config::has_previous_configuration_c;
using ::arude::config::is_set;
using ::arude::config::load;
using ::arude::config::load_migrated;
using ::arude::config::migrate;
using ::arude::config::operator&;
using ::arude::config::operator|;
using ::arude::config::store;
using ::arude::config::to_toml;
using ::arude::config::toml_version;
using ::arude::config::upgradable_configuration_c;
using ::arude::config::upgrade;
using ::arude::config::uri_scheme;
using ::arude::config::uri_transport_file;
using ::arude::config::version_of;
using ::arude::config::version_t;

} // namespace arude::config

export namespace arude::config::detail
{

using ::arude::config::detail::binary_reflector_t;
using ::arude::config::detail::config_errors_formatter_t;

} // namespace arude::config::detail

#endif // #if !(defined ARUDE_NO_CONFIG)
