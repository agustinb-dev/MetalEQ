cmake_minimum_required(VERSION 3.22)
project(MetalEQ VERSION 3.1.0)

include(CTest)

# Options to speed up JUCE build
set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(JUCE_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
set(JUCE_BUILD_TESTS OFF CACHE BOOL "" FORCE)

# Use FetchContent to get JUCE source
include(FetchContent)
FetchContent_Declare(
  juce
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG        9.0.4   # Match the version we installed via Scoop
)
# For Windows, we might need to set the discriminant to avoid conflicts
FetchContent_GetProperties(juce)
if(NOT juce_Populated)
  FetchContent_Populate(juce)
  # Add the JUCE source to the build
  add_subdirectory(${juce_SOURCE_DIR} ${juce_BINARY_DIR})
endif()

# Set the JUCE module path for the juce_add_plugin function
set(JUCE_MODULE_PATH "${juce_SOURCE_DIR}/modules" CACHE PATH "JUCE modules directory")

# ---------------------------------------------------------------------------
# WebView2 SDK (Windows only) — required for juce::WebBrowserComponent on
# Windows. On Linux/macOS the editor uses the WebKit backend instead, which is
# provided by the system (libwebkit2gtk-4.1-dev), so this whole block is
# skipped.
#
# We download and extract the NuGet package at configure time so that the
# static loader (WebView2LoaderStatic.lib) is available without the nuget CLI.
# ---------------------------------------------------------------------------
set(METALEQ_WEBVIEW2_DIR "")
set(METALEQ_WEBVIEW2_INCLUDE_DIR "")
set(METALEQ_WEBVIEW2_LOADER_LIB "")
if(WIN32)
set(METALEQ_WEBVIEW2_VERSION "1.0.3485.44" CACHE STRING "WebView2 SDK version")
# JUCE's FindWebView2.cmake globs for *Microsoft.Web.WebView2* under this dir and
# expects build/native/{include,lib,x64} inside it (flat nupkg layout).
set(METALEQ_WEBVIEW2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/build/_deps/webview2")
if(NOT EXISTS "${METALEQ_WEBVIEW2_DIR}/build/native/include/WebView2.h")
    message(STATUS "Downloading WebView2 SDK ${METALEQ_WEBVIEW2_VERSION}")
    file(MAKE_DIRECTORY "${METALEQ_WEBVIEW2_DIR}")
    set(_wv2_nupkg "${METALEQ_WEBVIEW2_DIR}/microsoft.web.webview2.${METALEQ_WEBVIEW2_VERSION}.nupkg")
    if(NOT EXISTS "${_wv2_nupkg}")
        file(DOWNLOAD
            "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/${METALEQ_WEBVIEW2_VERSION}"
            "${_wv2_nupkg}"
            STATUS _wv2_dl_status)
        list(GET _wv2_dl_status 0 _wv2_dl_code)
        if(NOT _wv2_dl_code EQUAL 0)
            message(FATAL_ERROR "Failed to download WebView2 SDK: ${_wv2_dl_status}")
        endif()
    endif()
    file(ARCHIVE_EXTRACT INPUT "${_wv2_nupkg}" DESTINATION "${METALEQ_WEBVIEW2_DIR}")
endif()
if(NOT EXISTS "${METALEQ_WEBVIEW2_DIR}/build/native/include/WebView2.h")
    message(FATAL_ERROR "WebView2 SDK not found at ${METALEQ_WEBVIEW2_DIR}. Expected build/native/include/WebView2.h after download/extract.")
endif()
set(JUCE_WEBVIEW2_PACKAGE_LOCATION "${METALEQ_WEBVIEW2_DIR}" CACHE PATH "WebView2 package location for JUCE" FORCE)
set(METALEQ_WEBVIEW2_INCLUDE_DIR "${METALEQ_WEBVIEW2_DIR}/build/native/include")
set(METALEQ_WEBVIEW2_ARCH "x86")
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(METALEQ_WEBVIEW2_ARCH "x64")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM64|aarch64)$")
    set(METALEQ_WEBVIEW2_ARCH "arm64")
endif()
# JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING requires this static loader to be
# linked explicitly; without it the plugin fails with an unresolved
# CreateCoreWebView2EnvironmentWithOptions.
set(METALEQ_WEBVIEW2_LOADER_LIB
    "${METALEQ_WEBVIEW2_DIR}/build/native/${METALEQ_WEBVIEW2_ARCH}/WebView2LoaderStatic.lib")
endif() # WIN32

# ---------------------------------------------------------------------------
# WebView UI assets (React + TypeScript via Vite).
#
# We build the frontend with npm, zip the resulting dist, and embed the zip in
# the plugin with juce_add_binary_data (symbol BinaryData::metaleq_ui_zip).
# The resource provider (Source/ui/MetalEqWebUiResources.cpp) serves files from
# this zip at runtime, so no dev server / CDN / network access is needed.
#
# If npm is unavailable we skip the build and the plugin falls back to an
# inline placeholder page + the native fallback editor (watchdog).
# ---------------------------------------------------------------------------
set(METALEQ_UI_EMBEDDED OFF)

find_program(METALEQ_NPM_EXECUTABLE npm)

if(METALEQ_NPM_EXECUTABLE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Gui/package.json")
    message(STATUS "MetalEQ: building WebView UI assets with npm (${METALEQ_NPM_EXECUTABLE})")

    set(METALEQ_GUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Gui")
    # `npm run build` (Vite) writes to Gui/dist — that is what we package.
    set(METALEQ_UI_DIST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Gui/dist")
    # The zip filename determines the generated BinaryData symbol
    # (metaleq_ui.zip -> BinaryData::metaleq_ui), referenced by MetalEqWebUiResources.cpp.
    set(METALEQ_UI_ZIP "${CMAKE_CURRENT_BINARY_DIR}/metaleq_ui.zip")

    # npm ci (if a lockfile exists) + vite/tsc build → Gui/dist/
    set(METALEQ_UI_BUILD_COMMANDS
        COMMAND "${METALEQ_NPM_EXECUTABLE}" install
        COMMAND "${METALEQ_NPM_EXECUTABLE}" run build
    )

    add_custom_command(
        OUTPUT "${METALEQ_UI_DIST_DIR}/index.html"
        ${METALEQ_UI_BUILD_COMMANDS}
        WORKING_DIRECTORY "${METALEQ_GUI_DIR}"
        COMMENT "MetalEQ: building WebView UI (npm run build)"
        VERBATIM
    )

    add_custom_command(
        OUTPUT "${METALEQ_UI_ZIP}"
        COMMAND "${CMAKE_COMMAND}" -E tar cf "${METALEQ_UI_ZIP}" --format=zip -- .
        WORKING_DIRECTORY "${METALEQ_UI_DIST_DIR}"
        DEPENDS "${METALEQ_UI_DIST_DIR}/index.html"
        COMMENT "MetalEQ: packaging WebView UI into metaleq_ui.zip"
        VERBATIM
    )

    # Run juceaide to turn the zip into BinaryData.cpp/.h. The generated symbol
    # is named after the source filename ("metaleq_ui.zip" -> metaleq_ui), which
    # is what MetalEqWebUiResources.cpp declares.
    # juceaide's "binarydata" mode takes: <namespace> <headerName> <outFolder>
    # <fileList.txt>, where <fileList.txt> is a newline-delimited list of input
    # paths. It emits <outFolder>/BinaryData.h and <outFolder>/BinaryData<N>.cpp.
    #
    # The optional --windows flag only selects CRLF vs LF line endings in the
    # generated C++ (see juceaide/Main.cpp). There is no --linux/--osx
    # equivalent, so on other platforms we simply omit it and get LF.
    set(METALEQ_UI_JUCELIB "${CMAKE_CURRENT_BINARY_DIR}/juce_binarydata_metaleq_ui_zip/JuceLibraryCode")
    set(METALEQ_UI_BINDATA_CPP "${METALEQ_UI_JUCELIB}/BinaryData1.cpp")
    set(METALEQ_UI_BINDATA_H "${METALEQ_UI_JUCELIB}/BinaryData.h")
    set(METALEQ_UI_FILELIST "${CMAKE_CURRENT_BINARY_DIR}/metaleq_ui_filelist.txt")

    set(METALEQ_UI_JUCELIB_LINE_ENDING_FLAG "")
    if(WIN32)
        set(METALEQ_UI_JUCELIB_LINE_ENDING_FLAG --windows)
    endif()

    add_custom_command(
        OUTPUT "${METALEQ_UI_BINDATA_CPP}" "${METALEQ_UI_BINDATA_H}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${METALEQ_UI_JUCELIB}"
        COMMAND "${CMAKE_COMMAND}" -E echo "${METALEQ_UI_ZIP}" > "${METALEQ_UI_FILELIST}"
        COMMAND $<TARGET_FILE:juceaide> binarydata BinaryData BinaryData.h "${METALEQ_UI_JUCELIB}" "${METALEQ_UI_FILELIST}" ${METALEQ_UI_JUCELIB_LINE_ENDING_FLAG}
        DEPENDS "${METALEQ_UI_ZIP}" juceaide
        COMMENT "MetalEQ: generating BinaryData from metaleq_ui.zip"
        VERBATIM
    )

    # One target that (re)builds the UI zip and regenerates BinaryData.
    add_custom_target(metaleq_ui_assets
        DEPENDS "${METALEQ_UI_BINDATA_CPP}" "${METALEQ_UI_BINDATA_H}"
    )

    set(METALEQ_UI_EMBEDDED ON)
else()
    message(STATUS "MetalEQ: npm not found or Gui/package.json missing — WebView UI will use the built-in placeholder page and the native fallback editor.")
endif()

# Now we can use juce_add_plugin
# NEEDS_WEB_BROWSER tells JUCE this target embeds a browser. On Linux that
# makes JUCE define JUCE_WEB_BROWSER=1 *and* link its webkit/gtk pkg-config
# target (JUCE_BROWSER_LINUX_DEPS), which supplies the include dirs for
# webkit2gtk-4.1 and gtk+-x11-3.0. Without it the WebBrowserComponent type
# does not exist at all and the browser headers are never on the include path.
juce_add_plugin(${PROJECT_NAME}
    PLUGIN_MANUFACTURER_CODE "METL"  # Four-character manufacturer code
    PLUGIN_CODE "MTL0"               # Four-character plugin code (exactly one uppercase)
    FORMATS VST3                     # We want to build a VST3 plugin
    PRODUCT_NAME "MetalEQ"
    NEEDS_WEB_BROWSER TRUE
)

# Add source files to the plugin target
target_sources(${PROJECT_NAME}
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/dsp/MetalEqPresets.cpp
        Source/ui/MetalEqUiBridge.cpp
        Source/ui/MetalEqWebUiResources.cpp
        Source/native/MetalEQNativeFallbackEditor.cpp
)

# Ensure the WebView UI assets are built/packaged (and BinaryData generated)
# before compiling the plugin, and compile the generated BinaryData.cpp.
if(TARGET metaleq_ui_assets)
    add_dependencies(${PROJECT_NAME} metaleq_ui_assets)
    target_sources(${PROJECT_NAME} PRIVATE "${METALEQ_UI_BINDATA_CPP}")
    target_include_directories(${PROJECT_NAME} PRIVATE "${METALEQ_UI_JUCELIB}")
endif()

# Add the JUCE root directory to the include path so that JuceHeader.h can be found
target_include_directories(${PROJECT_NAME}
    PRIVATE
        "${juce_SOURCE_DIR}"
)

if(METALEQ_WEBVIEW2_INCLUDE_DIR)
    target_include_directories(${PROJECT_NAME} PRIVATE "${METALEQ_WEBVIEW2_INCLUDE_DIR}")
endif()

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

if(WIN32 AND EXISTS "${METALEQ_WEBVIEW2_LOADER_LIB}")
    target_link_libraries(${PROJECT_NAME} PRIVATE "${METALEQ_WEBVIEW2_LOADER_LIB}")
endif()

# Optional: set some compile definitions to reduce warnings
# NOTE: JUCE_WEB_BROWSER is deliberately not set here. juce_add_plugin
# detects whether a web backend is usable on the target and defines it
# accordingly; defining it here as well would silently override that
# detection (and force the Linux WebKit code path on without the headers).
target_compile_definitions(${PROJECT_NAME}
    PRIVATE
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        METALEQ_UI_EMBEDDED=$<BOOL:${METALEQ_UI_EMBEDDED}>
        $<$<BOOL:${METALEQ_UI_DEV_SERVER}>:METALEQ_UI_DEV_SERVER=1>
)

# The static WebView2 loader is Windows-only; asking JUCE to use it on other
# platforms produces a confusing failure deep inside the WebKit backend.
if(WIN32)
    target_compile_definitions(${PROJECT_NAME}
        PRIVATE JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1)
endif()

# The test executables link JUCE modules directly, so each one compiles its own
# copy of juce_gui_extra.cpp — including the WebKit browser backend on Linux.
# juce_add_plugin's NEEDS_WEB_BROWSER only applies to the plugin target, so the
# browser include dirs have to be attached to the tests too, or they fail with
# "gtk/gtk.h: No such file or directory".
#
# juce::pkgconfig_JUCE_BROWSER_LINUX_DEPS is created by JUCE on Linux/BSD only.
# It is built with NOLINK, so it contributes include dirs and compile options
# but no link libraries — correct, because JUCE dlopen()s webkit at runtime.
function(metaleq_enable_web_browser target)
    if(TARGET juce::pkgconfig_JUCE_BROWSER_LINUX_DEPS)
        target_link_libraries(${target} PRIVATE juce::pkgconfig_JUCE_BROWSER_LINUX_DEPS)
    endif()
endfunction()

# The test executables link the plugin's shared-code library rather than
# recompiling its sources, so they must NOT link the JUCE module targets.
#
# Each JUCE module target contributes its module's .cpp file as a source, so
# linking one makes the test compile its own copy of that module. The plugin's
# shared-code library already contains a compiled copy of every module it uses.
# Because the library is a static lib the linker pulls its copy in to satisfy
# references coming from the plugin's own objects (e.g. PluginEditor.obj ->
# juce::WebBrowserComponent), and the two definitions collide: LNK2005 on MSVC,
# "multiple definition" on GCC/Clang.
#
# The test still needs the module headers, which the shared-code library does
# not re-export (its module links are PRIVATE, so no usage requirements reach
# consumers), so the modules directory is added to the include path directly.
#
# It also inherits JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING from the library,
# which makes juce_gui_extra.h pull in WebView2.h, hence the WebView2 include
# dir and static loader below.
function(metaleq_link_plugin_shared_code target)
    target_include_directories(${target} PRIVATE
        Source
        "${juce_SOURCE_DIR}/modules"
    )

    target_link_libraries(${target} PRIVATE
        ${PROJECT_NAME}
        juce::juce_recommended_config_flags
    )

    # On Linux the shared-code library's browser backend needs webkit2gtk and
    # gtk headers; juce_add_plugin's NEEDS_WEB_BROWSER only covers the plugin
    # target, so the same pkg-config deps have to be attached here too or the
    # test fails with "gtk/gtk.h: No such file or directory".
    metaleq_enable_web_browser(${target})

    if(METALEQ_WEBVIEW2_INCLUDE_DIR)
        target_include_directories(${target} PRIVATE "${METALEQ_WEBVIEW2_INCLUDE_DIR}")
    endif()

    if(WIN32 AND EXISTS "${METALEQ_WEBVIEW2_LOADER_LIB}")
        target_link_libraries(${target} PRIVATE "${METALEQ_WEBVIEW2_LOADER_LIB}")
    endif()
endfunction()

if(BUILD_TESTING)
  add_executable(MetalEQ_DspTests
      Tests/MetalEqTests.cpp
      Source/dsp/MetalEqPresets.cpp
  )
  target_compile_features(MetalEQ_DspTests PRIVATE cxx_std_17)
  target_include_directories(MetalEQ_DspTests PRIVATE Source)
  if(MSVC)
    target_compile_options(MetalEQ_DspTests PRIVATE /W4 /permissive-)
  endif()
  add_test(NAME MetalEQ_DspTests COMMAND MetalEQ_DspTests)

  add_executable(MetalEQ_ResonanceTests Tests/MetalEqResonanceTests.cpp)
  target_compile_features(MetalEQ_ResonanceTests PRIVATE cxx_std_17)
  target_include_directories(MetalEQ_ResonanceTests PRIVATE Source)
  if(MSVC)
    target_compile_options(MetalEQ_ResonanceTests PRIVATE /W4 /permissive-)
  endif()
  add_test(NAME MetalEQ_ResonanceTests COMMAND MetalEQ_ResonanceTests)

  add_executable(MetalEQ_StateTests Tests/MetalEqStateTests.cpp)
  target_compile_features(MetalEQ_StateTests PRIVATE cxx_std_17)
  metaleq_link_plugin_shared_code(MetalEQ_StateTests)
  add_test(NAME MetalEQ_StateTests COMMAND MetalEQ_StateTests)

  # UI bridge tests: validateParameterWrite clamping, the lpOn side effect, and
  # the applyPreset index mapping shared with the native combo box.
  add_executable(MetalEQ_UiBridgeTests Tests/MetalEqUiBridgeTests.cpp)
  target_compile_features(MetalEQ_UiBridgeTests PRIVATE cxx_std_17)

  # The test links the plugin's shared-code lib, so it needs the generated
  # BinaryData object (the embedded UI zip) that the lib references.
  if(TARGET metaleq_ui_assets)
    add_dependencies(MetalEQ_UiBridgeTests metaleq_ui_assets)
    target_sources(MetalEQ_UiBridgeTests PRIVATE "${METALEQ_UI_BINDATA_CPP}")
    target_include_directories(MetalEQ_UiBridgeTests PRIVATE "${METALEQ_UI_JUCELIB}")
  endif()

  if(MSVC)
    target_compile_options(MetalEQ_UiBridgeTests PRIVATE /W4 /permissive-)
  endif()

  metaleq_link_plugin_shared_code(MetalEQ_UiBridgeTests)
  add_test(NAME MetalEQ_UiBridgeTests COMMAND MetalEQ_UiBridgeTests)
endif()
