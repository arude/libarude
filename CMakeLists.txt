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

# ---- Coverage ----

option(ARUDE_ENABLE_COVERAGE "Build with coverage instrumentation" OFF)

# ---- Add dependencies via CPM ----
# see https://github.com/TheLartians/CPM.cmake for more info

include(cmake/CPM.cmake)

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

target_include_directories(
  ${PROJECT_NAME} PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
                         $<INSTALL_INTERFACE:include/${PROJECT_NAME}-${PROJECT_VERSION}>
)

# ---- Tests ----

# Catch2 is a test-only dependency: the library itself must not link it.
include(CTest)

if(BUILD_TESTING)
  CPMAddPackage("gh:catchorg/Catch2@3.15.3")

  file(GLOB_RECURSE test_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/test/*.cpp")

  add_executable(${PROJECT_NAME}_test ${test_sources})
  set_target_properties(${PROJECT_NAME}_test PROPERTIES CXX_STANDARD ${ARUDE_CXX_STANDARD}
                                                        CXX_STANDARD_REQUIRED ON
                                                        CXX_EXTENSIONS OFF)
  target_link_libraries(${PROJECT_NAME}_test PRIVATE ${PROJECT_NAME} Catch2::Catch2WithMain)

  # catch_discover_tests registers each test case with CTest individually.
  list(APPEND CMAKE_MODULE_PATH "${Catch2_SOURCE_DIR}/extras")
  include(Catch)
  catch_discover_tests(${PROJECT_NAME}_test)
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
