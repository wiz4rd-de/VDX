# vdx_core: the C++17 engine + front-panel library (SPEC §9.4). No JUCE.
file(GLOB VDX_CORE_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/fm/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/sysex/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/panel/*.cpp)

# MSFA (Apache-2.0, from Dexed source/msfa). Only the files VDX uses are built.
set(VDX_MSFA_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/fm/third_party/msfa/sin.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/fm/third_party/msfa/exp2.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/fm/third_party/msfa/fm_op_kernel.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/fm/third_party/msfa/fm_core.cc)

add_library(vdx_core STATIC ${VDX_CORE_SOURCES} ${VDX_MSFA_SOURCES})
target_include_directories(vdx_core
  PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
          ${CMAKE_CURRENT_SOURCE_DIR}/fm/third_party/msfa
          ${CMAKE_CURRENT_SOURCE_DIR}/fm/shim/msfa
          ${CMAKE_SOURCE_DIR}/third_party/r8brain)
# Internal headers are visible to tests and tools through this interface target.
add_library(vdx_core_internal INTERFACE)
target_include_directories(vdx_core_internal INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/fm/third_party/msfa
  ${CMAKE_CURRENT_SOURCE_DIR}/fm/shim/msfa
  ${CMAKE_SOURCE_DIR}/third_party/r8brain)
target_link_libraries(vdx_core_internal INTERFACE vdx_core)

vdx_set_strict_fp(vdx_core)
# r8brain (included only by instrument.cpp) pulls in <windows.h> on Windows;
# NOMINMAX keeps its min/max macros away from std::min/std::max. PRIVATE, so
# JUCE targets, which define NOMINMAX themselves, are not affected.
target_compile_definitions(vdx_core PRIVATE $<$<PLATFORM_ID:Windows>:NOMINMAX>)
set_source_files_properties(${VDX_MSFA_SOURCES} PROPERTIES COMPILE_OPTIONS
  "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-unused-variable;-Wno-sign-compare;-Wno-deprecated-declarations;-Wno-unused-function>")
if(VDX_SUBNORMAL_CHECKS)
  target_compile_definitions(vdx_core PUBLIC VDX_SUBNORMAL_CHECKS=1)
endif()
if(NOT MSVC)
  find_package(Threads)
  if(Threads_FOUND AND NOT EMSCRIPTEN)
    target_link_libraries(vdx_core PUBLIC Threads::Threads)
  endif()
endif()
