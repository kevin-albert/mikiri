cmake_minimum_required(VERSION 3.22)
project(Mikiri VERSION 0.0.1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(Mikiri
    COMPANY_NAME "Archgoblin Omnimedia"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE Cgob
    PLUGIN_CODE Mkri
    FORMATS AU VST3
    PRODUCT_NAME "Mikiri"
)

juce_add_binary_data(MikiriAssets
    SOURCES
        Assets/ui-bg.png
        Assets/ui-fg.png
)

target_sources(Mikiri
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h
        Source/PluginEditor.cpp
        Source/PluginEditor.h
        Source/Shapes.cpp
        Source/Shapes.h
        Source/Visualizer.h
        Source/Visualizer.cpp
)

target_compile_definitions(Mikiri
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(Mikiri
    PRIVATE
        MikiriAssets
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

if(APPLE)
    set(COMPONENT_NAME "Mikiri.component")
    set(PKG_IDENTIFIER "com.archgoblinomnimedia.mikiri")
    set(AU_PATH "${CMAKE_BINARY_DIR}/Mikiri_artefacts/AU/${COMPONENT_NAME}")
    
    add_custom_target(installer
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${CMAKE_BINARY_DIR}/pkg_root"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/pkg_root"
        COMMAND ${CMAKE_COMMAND} -E copy_directory 
            "${AU_PATH}"
            "${CMAKE_BINARY_DIR}/pkg_root/${COMPONENT_NAME}"
        COMMAND pkgbuild
            --root "${CMAKE_BINARY_DIR}/pkg_root"
            --install-location "/Library/Audio/Plug-Ins/Components"
            --identifier "${PKG_IDENTIFIER}"
            --version "${PROJECT_VERSION}"
            "${CMAKE_BINARY_DIR}/Mikiri Installer.pkg"
        DEPENDS Mikiri_AU
        COMMENT "Creating macOS installer package..."
    )
endif()
