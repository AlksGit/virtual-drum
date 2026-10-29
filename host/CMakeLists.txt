cmake_minimum_required(VERSION 3.22)

project(VirtualDrumHost VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# JUCE is fetched at configure time and pinned to an exact tag so the PC and the Mac
# build against the same sources (see docs/DECISIONS.md).
# To use a local checkout instead (offline work), configure with:
#   -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
include(FetchContent)
set(VDRUM_JUCE_TAG "9.0.4")
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        ${VDRUM_JUCE_TAG}
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

juce_add_gui_app(VirtualDrum
    PRODUCT_NAME      "Virtual Drum"
    COMPANY_NAME      "Virtual Drum"
    BUNDLE_ID         "com.virtualdrum.host"
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL        FALSE)

target_sources(VirtualDrum
    PRIVATE
        src/Main.cpp
        src/MainComponent.cpp
        src/SceneView.cpp)

target_compile_definitions(VirtualDrum
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:VirtualDrum,JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:VirtualDrum,JUCE_VERSION>")

target_link_libraries(VirtualDrum
    PRIVATE
        juce::juce_gui_extra
        juce::juce_opengl
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
