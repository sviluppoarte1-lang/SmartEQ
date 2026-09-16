cmake_minimum_required(VERSION 3.22)
project(SpectraCurve VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Fetch JUCE 7.0.12
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 7.0.12
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

set(SMARTEQ_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/DSP/EQProcessor.cpp
    Source/DSP/IntelligentAnalyzer.cpp
    Source/DSP/SongAnalyzer.cpp
    Source/DSP/SpectrumAnalyzer.cpp
    Source/Presets/PresetManager.cpp
    Source/UI/SpectrumComponent.cpp
    Source/UI/ModernLookAndFeel.cpp
)

# Adds one SmartEQ variant.
# IS_FREE=ON  -> SpectraCurve Free: 8 bands, no analyzer.
# IS_FREE=OFF -> SmartEQ Full: 16 bands + analyzer + song-map.
# Public build: both variants are fully unlocked, no licensing.
function(add_smarteq_variant TARGET PRODUCT DISPLAY PLUGINCODE BUNDLE CATS IS_FREE)
    juce_add_plugin(${TARGET}
        COMPANY_NAME "Fear Escape"
        BUNDLE_ID ${BUNDLE}
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        PLUGIN_MANUFACTURER_CODE FeEs
        PLUGIN_CODE ${PLUGINCODE}
        FORMATS VST3 Standalone
        PRODUCT_NAME ${PRODUCT}
        DISPLAY_NAME ${DISPLAY}
        VST3_CATEGORIES ${CATS}
        DESCRIPTION "Intelligent equalizer by Fear Escape"
        VERSION "1.0.0"
    )

    target_sources(${TARGET} PRIVATE ${SMARTEQ_SOURCES})
    target_include_directories(${TARGET} PRIVATE Source)

    target_compile_definitions(${TARGET} PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
    )

    if(IS_FREE)
        target_compile_definitions(${TARGET} PRIVATE SMARTEQ_FREE_VERSION=1)
    endif()

    target_link_libraries(${TARGET} PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra
        juce_recommended_config_flags
        juce_recommended_lto_flags
        juce_recommended_warning_flags
    )

    smarteq_link_linux(${TARGET} TRUE)
endfunction()

# Linux: explicit X11 deps (avoids editor crashes in Bitwig/DAW).
# NEED_AUDIO_DEVICES=ON also links juce_audio_devices (plugins only).
function(smarteq_link_linux TARGET NEED_AUDIO_DEVICES)
    if(UNIX AND NOT APPLE)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(ALSA alsa)
        pkg_check_modules(FREETYPE freetype2)
        pkg_check_modules(X11 REQUIRED x11 xext xcomposite xcursor xinerama xrandr)
        pkg_check_modules(FONTCONFIG fontconfig)

        target_link_libraries(${TARGET} PRIVATE
            ${X11_LIBRARIES}
            ${FONTCONFIG_LIBRARIES}
        )
        if(NEED_AUDIO_DEVICES)
            target_link_libraries(${TARGET} PRIVATE juce::juce_audio_devices)
        endif()
        target_include_directories(${TARGET} PRIVATE ${X11_INCLUDE_DIRS} ${FONTCONFIG_INCLUDE_DIRS})
        target_compile_options(${TARGET} PRIVATE ${X11_CFLAGS_OTHER} ${FONTCONFIG_CFLAGS_OTHER})

        target_compile_definitions(${TARGET} PRIVATE JUCE_USE_X11=1 JUCE_USE_XCURSOR=1 JUCE_USE_XINERAMA=1 JUCE_USE_XRANDR=1)
    endif()
endfunction()

# Full (paid) version
add_smarteq_variant(SmartEQ SmartEQ "SpectraCurve EQ Intelligent Equalizer" SmEq
    "com.fearescape.spectracurve" "Fx|EQ|Mastering" OFF)

# Free version: 8 bands, no analyzer
add_smarteq_variant(SmartEQFree SmartEQFree "SpectraCurve Free" SmFr
    "com.fearescape.spectracurvefree" "Fx|EQ" ON)

# (No keygen in the public build: keys stay private. Full runs a 45-min demo.)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/Source PREFIX Source FILES ${SMARTEQ_SOURCES})
