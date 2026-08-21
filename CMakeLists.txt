cmake_minimum_required(VERSION 4.2)

# ---- Project ----

# Note: update this to your new project's name and version
project(
  libarude
  VERSION 0.1
  LANGUAGES CXX
)

# ---- Include guards ----

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
  message(
    FATAL_ERROR
      "In-source builds not allowed. Please make a new directory (called a build directory) and run CMake from there."
  )
endif()

# ---- Static analysis ----

# clang-tidy needs a compilation database. Always generated: it costs nothing
# and editors and language servers use it too.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---- Language standard ----

# Prefer 26, fall back to 23 where the compiler cannot do it. CMake populates
# CMAKE_CXX_COMPILE_FEATURES from the actual compiler, so this asks the
# toolchain rather than guessing from its version.
if("cxx_std_26" IN_LIST CMAKE_CXX_COMPILE_FEATURES)
  set(arude_default_standard 26)
else()
  set(arude_default_standard 23)
endif()

set(ARUDE_CXX_STANDARD ${arude_default_standard} CACHE STRING "C++ standard to build with")
set_property(CACHE ARUDE_CXX_STANDARD PROPERTY STRINGS 23 26)
message(STATUS "libarude: building as C++${ARUDE_CXX_STANDARD}")

# ---- std::stacktrace runtime ----

include(cmake/stacktrace.cmake)

arude_find_stacktrace_library(ARUDE_STACKTRACE_LIBRARY ${ARUDE_CXX_STANDARD})

# ---- Coverage ----

option(ARUDE_ENABLE_COVERAGE "Build with coverage instrumentation" OFF)

# ---- Add dependencies via CPM ----
# see https://github.com/TheLartians/CPM.cmake for more info

include(cmake/CPM.cmake)

# The enum helpers are built on magic_enum. Unlike Catch2 and Google Benchmark
# below it is not optional: it is a dependency of the library itself, so it is
# fetched before any target is created rather than behind a feature switch.
#
# Its examples, tests and install rules key off whether it is the top-level
# project, so as a subproject they are already off and need no overriding here.
CPMAddPackage("gh:Neargye/magic_enum@0.9.8")

# ---- Add source files ----

# Note: globbing sources is considered bad practice as CMake's generators may not detect new files
# automatically. Keep that in mind when changing files, or explicitly mention them here.
file(GLOB_RECURSE headers CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp")
file(GLOB_RECURSE sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

# ---- Create library ----

# Note: for header-only libraries change all PUBLIC flags to INTERFACE and create an interface
# target: add_library(${PROJECT_NAME} INTERFACE)
add_library(${PROJECT_NAME} ${headers} ${sources})
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD ${ARUDE_CXX_STANDARD}
                                                 CXX_STANDARD_REQUIRED ON
                                                 CXX_EXTENSIONS OFF)

# being a cross-platform target, we enforce standards conformance on MSVC
target_compile_options(${PROJECT_NAME} PUBLIC "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/permissive->")

# --coverage is understood by both GCC and Clang and emits gcov-format data.
# PUBLIC so the test executable is instrumented and linked the same way.
if(ARUDE_ENABLE_COVERAGE)
  target_compile_options(${PROJECT_NAME} PUBLIC --coverage -O0 -g)
  target_link_options(${PROJECT_NAME} PUBLIC --coverage)
endif()

# PUBLIC: arude/exception.hpp is a header, so every consumer needs the library
# too, not just this target.
if(ARUDE_STACKTRACE_LIBRARY)
  target_link_libraries(${PROJECT_NAME} PUBLIC ${ARUDE_STACKTRACE_LIBRARY})
endif()

# PUBLIC for the same reason: magic_enum is header-only, and a header of ours
# that includes it puts it on every consumer's include path too.
target_link_libraries(${PROJECT_NAME} PUBLIC magic_enum::magic_enum)

target_include_directories(
  ${PROJECT_NAME} PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
                         $<INSTALL_INTERFACE:include/${PROJECT_NAME}-${PROJECT_VERSION}>
)

# ---- C++20 module ----

# Off by default, and a separate target rather than a file set on the library:
# a target carrying CXX_MODULES needs a scanning generator even from consumers
# that never import it, so switching this on must not be able to happen by
# accident. Header consumers are unaffected either way.
option(ARUDE_BUILD_MODULE "Build the arude C++20 module interface" OFF)

if(ARUDE_BUILD_MODULE)
  # Module dependencies have to be scanned before anything is compiled, which
  # Makefiles cannot do. CMake would fail on its own later with a message that
  # says less about what to do next.
  if(NOT CMAKE_GENERATOR MATCHES "Ninja|Visual Studio")
    message(
      FATAL_ERROR
        "libarude: ARUDE_BUILD_MODULE requires a generator that scans C++ modules. "
        "The '${CMAKE_GENERATOR}' generator cannot; reconfigure with -G Ninja."
    )
  endif()

  # GCC 15.2 emits the data behind a std::source_location::current() default
  # argument as a local label, and it does not survive being emitted again in an
  # importing translation unit: the link fails with "undefined reference to
  # .Lsrc_loc1". arude/exception.hpp takes such a default on every constructor,
  # so nothing here can link under GCC. Reproduced in twelve lines with no arude
  # code involved, so this is the compiler rather than the module layout, and
  # clang 21 links the same code without complaint.
  #
  # Turn ARUDE_ALLOW_GCC_MODULE on to try anyway once GCC has fixed it.
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND NOT ARUDE_ALLOW_GCC_MODULE)
    message(
      FATAL_ERROR
        "libarude: ARUDE_BUILD_MODULE cannot work with GCC ${CMAKE_CXX_COMPILER_VERSION}, which mangles "
        "std::source_location default arguments across a module boundary. Build the module with clang, "
        "or set ARUDE_ALLOW_GCC_MODULE=ON to try regardless."
    )
  endif()

  add_library(${PROJECT_NAME}_module)
  target_sources(${PROJECT_NAME}_module PUBLIC FILE_SET CXX_MODULES FILES src/arude.cppm)
  set_target_properties(${PROJECT_NAME}_module PROPERTIES CXX_STANDARD ${ARUDE_CXX_STANDARD}
                                                          CXX_STANDARD_REQUIRED ON
                                                          CXX_EXTENSIONS OFF)
  # PUBLIC: importers still need the headers' out-of-line definitions, and the
  # stacktrace runtime that comes with them.
  target_link_libraries(${PROJECT_NAME}_module PUBLIC ${PROJECT_NAME})
endif()

# ---- Tests ----

# Catch2 is a test-only dependency: the library itself must not link it.
include(CTest)

if(BUILD_TESTING)
  CPMAddPackage("gh:catchorg/Catch2@3.15.3")

  # Catch2 asks only for cxx_std_14, so it compiles at a lower standard than the
  # tests that link it. Its StringMaker<std::string_view> is then declared but
  # never defined — the library half never saw std::string_view exist — and a
  # test that compares string_views fails to link. That is what broke the MSVC
  # row; the Linux rows survived it only because their default standard is high
  # enough. Building Catch2 at our standard keeps the two halves in step.
  foreach(catch_target IN ITEMS Catch2 Catch2WithMain)
    if(TARGET ${catch_target})
      set_target_properties(${catch_target} PROPERTIES CXX_STANDARD ${ARUDE_CXX_STANDARD}
                                                       CXX_STANDARD_REQUIRED ON
                                                       CXX_EXTENSIONS OFF)
    endif()
  endforeach()

  file(GLOB_RECURSE test_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/test/*.cpp")

  # test/config holds translation units built against a different exception base.
  # They must stay out of the main binary: the macro changes exception_base's
  # base class, and two definitions of it in one program is an ODR violation.
  list(FILTER test_sources EXCLUDE REGEX "/test/config/")

  # test/module imports rather than includes, so it needs the module target and
  # is only built when that exists.
  list(FILTER test_sources EXCLUDE REGEX "/test/module/")

  add_executable(${PROJECT_NAME}_test ${test_sources})
  set_target_properties(${PROJECT_NAME}_test PROPERTIES CXX_STANDARD ${ARUDE_CXX_STANDARD}
                                                        CXX_STANDARD_REQUIRED ON
                                                        CXX_EXTENSIONS OFF)
  # The project root, so a test that needs a second translation unit — test/pimpl
  # holds the owners whose implementation must not be visible to the test itself
  # — can include its header from the root the way docs/cpp-conventions.md asks
  # rather than by relative path.
  target_include_directories(${PROJECT_NAME}_test PRIVATE ${PROJECT_SOURCE_DIR})
  target_link_libraries(${PROJECT_NAME}_test PRIVATE ${PROJECT_NAME} Catch2::Catch2WithMain)

  # catch_discover_tests registers each test case with CTest individually.
  list(APPEND CMAKE_MODULE_PATH "${Catch2_SOURCE_DIR}/extras")
  include(Catch)
  catch_discover_tests(${PROJECT_NAME}_test)

  # ARUDE_EXCEPTION_RUNTIME_ERROR_BASE is set here rather than in the source, so
  # the file cannot be built against the wrong configuration by accident.
  add_executable(${PROJECT_NAME}_test_runtime_error_base test/config/test_runtime_error_base.cpp)
  set_target_properties(${PROJECT_NAME}_test_runtime_error_base
                        PROPERTIES CXX_STANDARD ${ARUDE_CXX_STANDARD}
                                   CXX_STANDARD_REQUIRED ON
                                   CXX_EXTENSIONS OFF)
  target_compile_definitions(
    ${PROJECT_NAME}_test_runtime_error_base PRIVATE ARUDE_EXCEPTION_RUNTIME_ERROR_BASE
  )
  target_link_libraries(
    ${PROJECT_NAME}_test_runtime_error_base PRIVATE ${PROJECT_NAME} Catch2::Catch2WithMain
  )
  catch_discover_tests(${PROJECT_NAME}_test_runtime_error_base)

  # Guards the re-export list in src/arude.cppm. A public name added to a header
  # and not listed there is invisible to module consumers and to nobody else, so
  # nothing but this test would notice.
  if(ARUDE_BUILD_MODULE)
    # header_consumer.cpp includes the header while test_module.cpp imports the
    # module, so the pair also covers the two consumption styles meeting in one
    # program.
    add_executable(
      ${PROJECT_NAME}_test_module test/module/test_module.cpp test/module/header_consumer.cpp
    )
    set_target_properties(${PROJECT_NAME}_test_module PROPERTIES CXX_STANDARD ${ARUDE_CXX_STANDARD}
                                                                 CXX_STANDARD_REQUIRED ON
                                                                 CXX_EXTENSIONS OFF)
    # The project root, so the pair can include each other's declaration from
    # the root the way docs/cpp-conventions.md asks rather than by relative path.
    target_include_directories(${PROJECT_NAME}_test_module PRIVATE ${PROJECT_SOURCE_DIR})
    target_link_libraries(
      ${PROJECT_NAME}_test_module PRIVATE ${PROJECT_NAME}_module Catch2::Catch2WithMain
    )
    catch_discover_tests(${PROJECT_NAME}_test_module)
  endif()
endif()

# ---- Benchmarks ----

option(ARUDE_BUILD_BENCHMARKS "Build the benchmarks" ON)

if(ARUDE_BUILD_BENCHMARKS)
  # Without these, google/benchmark builds its own test suite and pulls in
  # GoogleTest to do it.
  set(BENCHMARK_ENABLE_TESTING OFF)
  set(BENCHMARK_ENABLE_GTEST_TESTS OFF)
  set(BENCHMARK_ENABLE_INSTALL OFF)
  CPMAddPackage("gh:google/benchmark@1.9.5")

  file(GLOB_RECURSE benchmark_sources CONFIGURE_DEPENDS
       "${CMAKE_CURRENT_SOURCE_DIR}/benchmark/*.cpp")

  add_executable(${PROJECT_NAME}_benchmark ${benchmark_sources})
  set_target_properties(${PROJECT_NAME}_benchmark PROPERTIES CXX_STANDARD ${ARUDE_CXX_STANDARD}
                                                             CXX_STANDARD_REQUIRED ON
                                                             CXX_EXTENSIONS OFF)
  # benchmark_main supplies main(); benchmark alone does not.
  target_link_libraries(
    ${PROJECT_NAME}_benchmark PRIVATE ${PROJECT_NAME} benchmark::benchmark_main
  )
endif()
