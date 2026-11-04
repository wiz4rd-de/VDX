# Benchmark (SPEC §9.1, AC-N1.1). Built natively and with Emscripten.
add_executable(vdx_bench bench.cpp)
target_link_libraries(vdx_bench PRIVATE vdx_core_internal)
vdx_set_strict_fp(vdx_bench)
if(EMSCRIPTEN)
  target_link_options(vdx_bench PRIVATE -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1 -sENVIRONMENT=node -sEXIT_RUNTIME=1)
endif()

# AC-N1.1: native < 5 % and WASM < 25 % of real time (median of 5 runs).
if(EMSCRIPTEN)
  add_test(NAME "AC-N1.1_bench_wasm" COMMAND node $<TARGET_FILE:vdx_bench> --limit 25)
else()
  add_test(NAME "AC-N1.1_bench_native" COMMAND vdx_bench --limit 5)
endif()

if(EMSCRIPTEN)
  set_tests_properties(AC-N1.1_bench_wasm PROPERTIES LABELS "bench")
else()
  set_tests_properties(AC-N1.1_bench_native PROPERTIES LABELS "bench")
endif()
