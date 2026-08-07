# Locating the runtime library that provides std::stacktrace.
#
# libstdc++ keeps std::stacktrace in a separate runtime library: stdc++exp as of
# GCC 14, stdc++_libbacktrace in GCC 13. libc++ and MSVC need no extra library.
# Probing beats branching on the compiler id, because clang built against
# libstdc++ needs the library exactly as GCC does, while clang against libc++
# does not.

include_guard(GLOBAL)

include(CheckCXXSourceCompiles)
include(CMakePushCheckState)

# Reports in out_var which library has to be linked for std::stacktrace:
#
#   <name>    link this one
#   ""        std::stacktrace links with no help
#   NOTFOUND  nothing worked, so arude/exception.hpp will not build
#
# All three are falsy or a plain library name, so the caller can pass the result
# straight to target_link_libraries behind a single if().
#
# cxx_standard is a parameter rather than something read from the enclosing
# scope, because the probe has to compile at the same standard as the library
# itself. Keeping this a function is what confines the CMAKE_CXX_STANDARD it
# sets: cmake_push_check_state restores only the CMAKE_REQUIRED_* variables, so
# at file scope that assignment would leak to every target configured after it.
function(arude_find_stacktrace_library out_var cxx_standard)
  set(program [[
#include <stacktrace>
int main()
{
  return static_cast<int>(std::stacktrace::current().size());
}
]])

  cmake_push_check_state(RESET)
  set(CMAKE_CXX_STANDARD ${cxx_standard})
  set(CMAKE_CXX_STANDARD_REQUIRED ON)

  set(found FALSE)
  set(found_library "")

  # The empty candidate goes first: if std::stacktrace already links, no library
  # should be added just because one happens to exist.
  foreach(candidate IN ITEMS "" "stdc++exp" "stdc++_libbacktrace")
    set(CMAKE_REQUIRED_LIBRARIES ${candidate})
    string(MAKE_C_IDENTIFIER "arude_have_stacktrace_${candidate}" probe)
    check_cxx_source_compiles("${program}" ${probe})

    if(${probe})
      set(found TRUE)
      set(found_library "${candidate}")
      break()
    endif()
  endforeach()

  cmake_pop_check_state()

  if(NOT found)
    # Not an error: arude/exception.hpp falls back to an empty stand-in where
    # the standard library has no std::stacktrace, which is libc++ at every
    # version. Reported so the reason a stacktrace is missing at run time is
    # visible at configure time rather than guessed at later.
    message(STATUS "libarude: no std::stacktrace available; exception traces will be empty")
    set(${out_var} "${out_var}-NOTFOUND" PARENT_SCOPE)
    return()
  endif()

  if(found_library STREQUAL "")
    message(STATUS "libarude: std::stacktrace needs no extra library")
  else()
    message(STATUS "libarude: std::stacktrace provided by ${found_library}")
  endif()

  set(${out_var} "${found_library}" PARENT_SCOPE)
endfunction()
