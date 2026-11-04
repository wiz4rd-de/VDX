# Browser module: vdx_core + vdx_bridge + worklet.cpp -> ui/public/vdx.{js,wasm,aw.js}
# (SPEC §15.2). Configure only with Emscripten; web/build.sh does this.
if(NOT EMSCRIPTEN)
  message(FATAL_ERROR "VDX_BUILD_WEB requires Emscripten (use web/build.sh)")
endif()
set(VDX_WEB_OUT "${CMAKE_SOURCE_DIR}/ui/public" CACHE PATH "Output directory of the web module")
add_executable(vdx_web worklet.cpp)
target_link_libraries(vdx_web PRIVATE vdx_bridge vdx_core)
vdx_set_strict_fp(vdx_web)
set_target_properties(vdx_web PROPERTIES OUTPUT_NAME vdx SUFFIX ".js" RUNTIME_OUTPUT_DIRECTORY "${VDX_WEB_OUT}")
target_link_options(vdx_web PRIVATE
  -O3 -msimd128
  -pthread -sAUDIO_WORKLET=1 -sWASM_WORKERS=1 -sPTHREAD_POOL_SIZE=0
  -sENVIRONMENT=web,worker,worklet
  -sMODULARIZE=1 -sEXPORT_NAME=createVdxModule
  -sINITIAL_MEMORY=67108864 -sSTACK_SIZE=1048576
  -sEXPORTED_RUNTIME_METHODS=UTF8ToString,stringToNewUTF8,HEAPU8,emscriptenRegisterAudioObject,emscriptenGetAudioObject
  -sEXPORTED_FUNCTIONS=_free,_malloc
  -sALLOW_MEMORY_GROWTH=0)
