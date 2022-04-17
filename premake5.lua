workspace "fta2"
  location "project/"
  configurations {"Debug", "Release"}
  language "C++"
  cppdialect "C++20"
  flags { "MultiProcessorCompile", "FatalWarnings" }

filter { "system:windows" }
  platforms { "Win64" }

filter { "platforms:Win64" }
defines { "WIN32", "_CRT_SECURE_NO_WARNINGS", "WIN32_LEAN_AND_MEAN", "NOMINMAX" }
architecture "x64"

filter "configurations:Debug"
  defines { "FTA2_DEBUG" }
  symbols "On"

filter "configurations:Release"
  optimize "On"

project "fta2"
  location "project/"
  kind "WindowedApp"
  targetdir "project/bin/%{cfg.platform}/%{cfg.buildcfg}"
  files {"src/**.h", "src/**.cpp"}
  files { "src/res/rockstar.ico", "src/res/resources.rc" }
  includedirs { "src" }
  editandcontinue "Off"
  staticruntime "On"
  flags { "NoIncrementalLink", "NoPCH" }
