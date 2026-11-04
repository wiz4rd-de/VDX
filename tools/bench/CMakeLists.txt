# Benchmark (SPEC §9.1, AC-N1.1). Built natively and with Emscripten.
add_executable(vdx_bench bench.cpp)
target_link_libraries(vdx_bench PRIVATE vdx_core_internal)
vdx_set_strict_fp(vdx_bench)
if(EMSCRIPTEN)
  target_link_options(vdx_bench PRIVATE -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1 -sENVIRONMENT=node -sEXIT_RUNTIME=1)
endif()
