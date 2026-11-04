# Deterministic floating point for bit-exact goldens (SPEC §14): no FMA
# contraction, no fast-math, on every compiler.
function(vdx_set_strict_fp target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /fp:precise /W4 /wd4100 /wd4244 /wd4267 /wd4389 /wd4456 /wd4457 /wd4458 /wd4459 /wd4127 /utf-8)
  else()
    target_compile_options(${target} PRIVATE -ffp-contract=off -fno-fast-math -Wall -Wextra -Wno-unused-parameter)
  endif()
endfunction()
