cmake_minimum_required(VERSION 3.25)

project(VDX VERSION 0.1.0 LANGUAGES C CXX)

option(VDX_BUILD_PLUGIN "Build the JUCE VST3/AU plugin and the standalone app" ON)
option(VDX_BUILD_WEB "Build the WebAssembly module for the web page (Emscripten only)" OFF)
option(VDX_BUILD_TESTS "Build the test suites, golden tool and benchmark" ON)

if(EMSCRIPTEN)
  set(VDX_BUILD_PLUGIN OFF CACHE BOOL "" FORCE)
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(VdxFlags)

add_subdirectory(core)
add_subdirectory(bridge)

if(VDX_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
  add_subdirectory(tools/bench)
endif()

if(VDX_BUILD_PLUGIN)
  add_subdirectory(third_party/JUCE EXCLUDE_FROM_ALL)
  add_subdirectory(plugin)
endif()

if(VDX_BUILD_WEB)
  add_subdirectory(web)
endif()
