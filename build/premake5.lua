workspace "libarude"
  flags { "MultiProcessorCompile", "NoPCH", "ShadowedVariables", "Unicode" }
  editandcontinue "Off"

  configurations { "debug", "release" }
  filter "configuration:debug"
    defines { "DEBUG", "_DEBUG" }
    flags { "Symbols", "FatalWarnings" }
  filter "configuration:release"
    defines { "NDEBUG" }
    optimize "Full"
    flags { "LinkTimeOptimization", "NoFramePointer" }

  platforms { "windows_x68", "windows_x64", "linux" }
  filter "platforms:windows_x86"
    system "windows"
    architecture "x86"
    toolset "msc-150"
  filter "platforms:windows_x64"
    system "windows"
    architecture "x86_64"
    toolset "msc-150"
  filter "platforms:linux"
    system "linux"
    toolset "clang"


project "libarude"
  kind "SharedLib"
  language "C++"
  targetdir "../bin/%{cfg.buildcfg}"
  links { "boost" }
  defines { "BOOST_ALL_DYN_LINK" }

  includedirs
  {
    "/usr/include",
    "../"
  }

  files { "../libarude/**.hpp", "../src/**.cpp" }


project "libarude_test"
  kind "ConsoleApp"
  language "C++"
  targetdir "../bin/%{cfg.buildcfg}"
  links { "libarude" }

  includedirs
  {
    "../"
  }

  files { "../test/**.hpp", "../test/**.cpp" }