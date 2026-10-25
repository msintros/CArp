cmake_minimum_required(VERSION 3.22)
project(CArp VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

juce_add_gui_app(CArp
    PRODUCT_NAME "CArp"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Assets/carp_icon.png")

juce_add_binary_data(CArpAssets SOURCES Assets/carp_header.png)

target_sources(CArp PRIVATE
    Source/Main.cpp
    ../seq/Engine.cpp)

target_include_directories(CArp PRIVATE Source ../seq)

target_compile_definitions(CArp PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:CArp,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:CArp,JUCE_VERSION>")

target_link_libraries(CArp PRIVATE
    CArpAssets
    juce::juce_gui_basics
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
