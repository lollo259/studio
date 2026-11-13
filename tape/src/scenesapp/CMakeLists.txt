#
# Scenes (Laurence Who Studio): the live sound switcher as its own small app, Scenes.exe,
# next to Tape in Tape/bin. It plays Tape's sounds with Tape's engines (sfizz + FluidSynth)
# through its own PortAudio stream; no Audacity/MuseScore framework inside.
#

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Multimedia)

set(SCENES_SRC
    main.cpp
    mainwindow.cpp
    mainwindow.h
    audioengine.cpp
    audioengine.h
    midiin.cpp
    midiin.h
    scenesformat.cpp
    scenesformat.h
    scenes.qrc
    ${CMAKE_CURRENT_SOURCE_DIR}/../instruments/soundlibrary.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../instruments/internal/samplerkit.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../instruments/internal/sf2export.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../instruments/internal/engines.cpp
)
if (WIN32)
    list(APPEND SCENES_SRC scenes.rc)
endif()

add_executable(scenes WIN32 ${SCENES_SRC})
set_target_properties(scenes PROPERTIES
    OUTPUT_NAME Scenes
    AUTOMOC ON
    AUTORCC ON
    AUTOUIC OFF
)
target_include_directories(scenes PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# WASAPI exclusive mode needs PortAudio's pa_win_wasapi.h (without it Scenes still plays, shared mode)
if (WIN32 AND TARGET portaudio::portaudio)
    get_target_property(_pa_inc portaudio::portaudio INTERFACE_INCLUDE_DIRECTORIES)
    set(_pa_wasapi OFF)
    foreach(_d IN LISTS _pa_inc)
        if (EXISTS "${_d}/pa_win_wasapi.h")
            set(_pa_wasapi ON)
        endif()
    endforeach()
    message(STATUS "Scenes: PortAudio include ${_pa_inc}, WASAPI exclusive header: ${_pa_wasapi}")
endif()

if (WIN32)
    target_link_libraries(scenes PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Multimedia
        sfizz::shared tape_fluidsynth portaudio::portaudio winmm)
    install(TARGETS scenes RUNTIME DESTINATION bin)
else()
    target_link_libraries(scenes PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Multimedia
        sfizz::sfizz tape_fluidsynth portaudio::portaudio)
endif()
