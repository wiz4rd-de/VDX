# plugin/: the JUCE desktop shells — VST3/AU plugin and standalone app (SPEC §9.3).
# The UI is the Vite build of ui/ (ui/dist), embedded into the binary at build time.

find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_program(VDX_NODE_EXECUTABLE NAMES node node.exe)
find_program(VDX_NPM_EXECUTABLE NAMES npm npm.cmd)
if(NOT VDX_NODE_EXECUTABLE OR NOT VDX_NPM_EXECUTABLE)
  message(FATAL_ERROR
    "VDX: Node.js and npm are required to build the user interface (ui/dist) that the plugin and the "
    "standalone app embed. Install Node.js and npm, or configure with -DVDX_BUILD_PLUGIN=OFF.")
endif()

juce_add_plugin(VDX
    PRODUCT_NAME              "VDX"
    PLUGIN_NAME               "VDX"
    VERSION                   0.1.0
    COMPANY_NAME              "Marco Funk"
    COMPANY_COPYRIGHT         "© 2026 Marco Funk"
    BUNDLE_ID                 com.marcofunk.vdx
    PLUGIN_MANUFACTURER_CODE  Mfnk
    PLUGIN_CODE               Vdxf
    DESCRIPTION               "6-operator FM synthesizer"
    FORMATS                   Standalone VST3 AU
    IS_SYNTH                  TRUE
    NEEDS_MIDI_INPUT          TRUE
    NEEDS_MIDI_OUTPUT         TRUE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES           Instrument Synth
    AU_MAIN_TYPE              kAudioUnitType_MusicDevice
    NEEDS_WEBVIEW2            TRUE
    NEEDS_WEB_BROWSER         TRUE
    MICROPHONE_PERMISSION_ENABLED FALSE
    HARDENED_RUNTIME_ENABLED  TRUE
    COPY_PLUGIN_AFTER_BUILD   TRUE)

# ---- embedded UI (tools/embed_ui.py: npm ci/run build -> ui/dist -> C++ arrays) ----
set(VDX_UI_DIR ${CMAKE_SOURCE_DIR}/ui)
file(GLOB_RECURSE VDX_UI_INPUTS CONFIGURE_DEPENDS ${VDX_UI_DIR}/src/* ${VDX_UI_DIR}/public/*)
foreach(f index.html package.json package-lock.json vite.config.ts svelte.config.js tsconfig.json)
  if(EXISTS ${VDX_UI_DIR}/${f})
    list(APPEND VDX_UI_INPUTS ${VDX_UI_DIR}/${f})
  endif()
endforeach()
set(VDX_UI_CPP ${CMAKE_CURRENT_BINARY_DIR}/generated/ui_resources_data.cpp)
add_custom_command(
  OUTPUT ${VDX_UI_CPP}
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/embed_ui.py
          --ui ${VDX_UI_DIR} --out ${VDX_UI_CPP} --npm ${VDX_NPM_EXECUTABLE}
  DEPENDS ${VDX_UI_INPUTS} ${CMAKE_SOURCE_DIR}/tools/embed_ui.py
  COMMENT "VDX: building the UI (Vite) and embedding ui/dist"
  VERBATIM)

target_sources(VDX PRIVATE
  src/processor.cpp
  src/editor.cpp
  src/host_params.cpp
  src/ui_resources.cpp
  shell/persistence.cpp
  ${VDX_UI_CPP})
target_include_directories(VDX PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/shell)

# The custom standalone app (StandalonePluginHolder + DocumentWindow).
target_sources(VDX_Standalone PRIVATE src/standalone_app.cpp)

target_compile_definitions(VDX PUBLIC
  JUCE_WEB_BROWSER=1
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1
  $<$<PLATFORM_ID:Windows>:JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1>)

target_link_libraries(VDX
  PRIVATE
    vdx_core_internal
    vdx_bridge
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_gui_extra
  PUBLIC
    juce::juce_recommended_config_flags)

# ---- tests: the real AudioProcessor, headless (no editor) ----
if(VDX_BUILD_TESTS)
  add_executable(vdx_plugin_tests
    tests/main.cpp
    tests/plugin_tests.cpp
    tests/persistence_tests.cpp
    ${CMAKE_SOURCE_DIR}/third_party/catch2/catch_amalgamated.cpp)
  # Same headers and configuration as the plugin wrappers (JUCE re-exports the
  # shared code's include directories the same way).
  target_include_directories(vdx_plugin_tests PRIVATE
    $<TARGET_PROPERTY:VDX,INCLUDE_DIRECTORIES>
    ${CMAKE_SOURCE_DIR}/third_party/catch2)
  target_compile_definitions(vdx_plugin_tests PRIVATE CATCH_AMALGAMATED_CUSTOM_MAIN)
  target_link_libraries(vdx_plugin_tests PRIVATE VDX vdx_core_internal vdx_bridge)
  add_test(NAME vdx_plugin_tests COMMAND vdx_plugin_tests --colour-mode none)
  set_tests_properties(vdx_plugin_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()
