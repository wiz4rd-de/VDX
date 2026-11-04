# Catch2 suites. The same binary runs natively and under Node (Emscripten).
add_library(vdx_catch2 STATIC ${CMAKE_SOURCE_DIR}/third_party/catch2/catch_amalgamated.cpp)
target_include_directories(vdx_catch2 PUBLIC ${CMAKE_SOURCE_DIR}/third_party/catch2)

file(GLOB_RECURSE VDX_TEST_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/tier1/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/tier2/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/support/*.cpp)

add_executable(vdx_tests ${VDX_TEST_SOURCES})
target_link_libraries(vdx_tests PRIVATE vdx_core_internal vdx_bridge vdx_catch2)
target_include_directories(vdx_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/support)
target_compile_definitions(vdx_tests PRIVATE VDX_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
vdx_set_strict_fp(vdx_tests)

if(EMSCRIPTEN)
  foreach(t vdx_catch2 vdx_tests vdx_core vdx_bridge)
    target_compile_options(${t} PRIVATE -fwasm-exceptions)
  endforeach()
  target_link_options(vdx_tests PRIVATE -fwasm-exceptions -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1
    -sSTACK_SIZE=8MB -sENVIRONMENT=node -sEXIT_RUNTIME=1)
  add_test(NAME vdx_tests COMMAND node $<TARGET_FILE:vdx_tests> --colour-mode none)
else()
  add_test(NAME vdx_tests COMMAND vdx_tests --colour-mode none)
endif()
set_tests_properties(vdx_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Licence and branding checks (R-L3, AC-L1.1, AC-L4.1, AC-L5.1) run with Python.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME licence_check COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/licence_check.py --self-test
           WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()
