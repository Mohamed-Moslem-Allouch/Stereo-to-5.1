cmake_minimum_required(VERSION 3.22)
project(Surround51Upmixer VERSION 1.5.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# -----------------------------------------------------------------------------
# Setup:
# 1. Download JUCE from https://juce.com/get-juce/
# 2. Place the JUCE folder next to this CMakeLists.txt
# 3. Build:
#    mkdir build && cd build
#    Windows: cmake .. -G "Visual Studio 18 2026" -A x64
#             cmake --build . --config Release
#    macOS/Linux: cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release
#                 cmake --build . --parallel
# -----------------------------------------------------------------------------

set(JUCE_DIR "${CMAKE_SOURCE_DIR}/JUCE" CACHE PATH "Path to JUCE")
add_subdirectory(${JUCE_DIR} JUCE)

juce_add_gui_app(Surround51Upmixer
    PRODUCT_NAME        "5.1 Surround Upmixer"
    VERSION             "1.5.0"
    COMPANY_NAME        "SlouMa"
    NEEDS_CURL          FALSE
    NEEDS_WEB_BROWSER   FALSE
)

target_sources(Surround51Upmixer PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
)

target_compile_definitions(Surround51Upmixer PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Surround51Upmixer,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Surround51Upmixer,JUCE_VERSION>"
)

juce_generate_juce_header(Surround51Upmixer)

target_link_libraries(Surround51Upmixer PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_utils
    juce::juce_core
    juce::juce_data_structures
    juce::juce_dsp
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

include(CTest)
if (BUILD_TESTING)
    add_executable(Surround51UpmixerSmokeTest
        tests/UpmixEngineSmokeTest.cpp
        Source/MainComponent.cpp
    )

    target_compile_definitions(Surround51UpmixerSmokeTest PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
    )
    target_include_directories(Surround51UpmixerSmokeTest PRIVATE
        "${CMAKE_BINARY_DIR}/Surround51Upmixer_artefacts/JuceLibraryCode"
    )
    add_dependencies(Surround51UpmixerSmokeTest Surround51Upmixer)

    target_link_libraries(Surround51UpmixerSmokeTest PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_data_structures
        juce::juce_dsp
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
    )

    add_test(NAME UpmixEngineSmokeTest COMMAND Surround51UpmixerSmokeTest)
endif()
