# vdx_bridge: JSON <-> Command/Event translation shared by the desktop editor
# and the WebAssembly module (SPEC §14 UI bridge). No JUCE.
file(GLOB VDX_BRIDGE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
add_library(vdx_bridge STATIC ${VDX_BRIDGE_SOURCES})
target_include_directories(vdx_bridge PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vdx_bridge PUBLIC vdx_core_internal)
vdx_set_strict_fp(vdx_bridge)
