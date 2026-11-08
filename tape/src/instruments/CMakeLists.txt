#
# Tape (Laurence Who Studio) — instruments: virtual instruments played from a MIDI keyboard
#

# sfizz: the SFZ engine (BSD-2-Clause), built from its self-contained release tarball
include(FetchContent)
# On Windows sfizz goes in its own DLL, so its private copies of wavpack/aiff never clash with Tape's
if (WIN32)
    set(SFIZZ_SHARED ON CACHE BOOL "" FORCE)
else()
    set(SFIZZ_SHARED OFF CACHE BOOL "" FORCE)
endif()
set(SFIZZ_JACK OFF CACHE BOOL "" FORCE)
set(SFIZZ_RENDER OFF CACHE BOOL "" FORCE)
set(SFIZZ_TESTS OFF CACHE BOOL "" FORCE)
set(SFIZZ_DEMOS OFF CACHE BOOL "" FORCE)
set(SFIZZ_DEVTOOLS OFF CACHE BOOL "" FORCE)
set(SFIZZ_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(SFIZZ_USE_SNDFILE OFF CACHE BOOL "" FORCE)
set(PLUGIN_LV2 OFF CACHE BOOL "" FORCE)
set(PLUGIN_LV2_UI OFF CACHE BOOL "" FORCE)
set(PLUGIN_VST3 OFF CACHE BOOL "" FORCE)
set(PLUGIN_AU OFF CACHE BOOL "" FORCE)
set(PLUGIN_PUREDATA OFF CACHE BOOL "" FORCE)
FetchContent_Declare(sfizz
    URL https://github.com/sfztools/sfizz/releases/download/1.2.3/sfizz-1.2.3.tar.gz
    URL_HASH SHA256=a9339eac7620d7f0f6b44bdfe860680fab73e66efad4b5f15b21198dd9436822
)
FetchContent_GetProperties(sfizz)
if (NOT sfizz_POPULATED)
    FetchContent_Populate(sfizz)
    add_subdirectory(${sfizz_SOURCE_DIR} ${sfizz_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

muse_create_module(instruments)

target_sources(instruments PRIVATE
    instrumentsmodule.cpp
    instrumentsmodule.h
    ilivesynth.h
    soundlibrary.cpp
    soundlibrary.h
    internal/livesynth.cpp
    internal/livesynth.h
    internal/instrumentsservice.cpp
    internal/instrumentsservice.h
    internal/instrumentsactions.cpp
    internal/instrumentsactions.h
)

if (WIN32)
    target_link_libraries(instruments PRIVATE sfizz::shared portaudio::portaudio muse::midi)
    install(TARGETS sfizz_shared RUNTIME DESTINATION bin)
else()
    target_link_libraries(instruments PRIVATE sfizz::sfizz portaudio::portaudio muse::midi)
endif()

add_subdirectory(qml/Audacity/Instruments)
