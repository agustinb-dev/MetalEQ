cmake_minimum_required(VERSION 3.22)
project(MetalEQ VERSION 0.0.1)

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

# Now we can use juce_add_plugin
juce_add_plugin(${PROJECT_NAME}
    PLUGIN_MANUFACTURER_CODE "METL"  # Four-character manufacturer code
    PLUGIN_CODE "MTL0"               # Four-character plugin code (exactly one uppercase)
    FORMATS VST3                     # We want to build a VST3 plugin
    PRODUCT_NAME "MetalEQ"
)

# Add source files to the plugin target
target_sources(${PROJECT_NAME}
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/dsp/MetalEqPresets.cpp
)

# Add the JUCE root directory to the include path so that JuceHeader.h can be found
target_include_directories(${PROJECT_NAME}
    PRIVATE
        "${juce_SOURCE_DIR}"
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Optional: set some compile definitions to reduce warnings
target_compile_definitions(${PROJECT_NAME}
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

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

  add_executable(MetalEQ_StateTests Tests/MetalEqStateTests.cpp)
  target_compile_features(MetalEQ_StateTests PRIVATE cxx_std_17)
  target_include_directories(MetalEQ_StateTests PRIVATE Source)
  target_link_libraries(MetalEQ_StateTests PRIVATE
      ${PROJECT_NAME}
      juce::juce_audio_utils
      juce::juce_dsp
      juce::juce_recommended_config_flags
  )
  add_test(NAME MetalEQ_StateTests COMMAND MetalEQ_StateTests)
endif()
