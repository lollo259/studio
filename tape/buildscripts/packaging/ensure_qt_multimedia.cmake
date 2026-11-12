# Tape (Laurence Who Studio) — the Video window needs Qt Multimedia at run time:
# its FFmpeg backend plugin (multimedia/ffmpegmediaplugin.dll), Qt's own FFmpeg DLLs
# (avcodec/avformat/avutil/swresample/swscale) and the QtMultimedia QML module.
# windeployqt normally ships all of them; this makes sure, and copies whatever it left out.
#
# Expects STUDIO_QT_BINS, STUDIO_QT_PLUGINS and STUDIO_QT_QML (set by install(CODE) before this script).

set(_root "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}")
if(NOT EXISTS "${_root}")
    message(WARNING "ensure_qt_multimedia: install root not found: ${_root}")
    return()
endif()

# where the deployed app keeps its executable, Qt plugins and QML modules
file(GLOB_RECURSE _qwindows LIST_DIRECTORIES FALSE "${_root}/*qwindows.dll")
file(GLOB_RECURSE _quickqmldir LIST_DIRECTORIES FALSE "${_root}/*/QtQuick/qmldir")
set(_bin "${_root}/bin")
if(_qwindows)
    list(GET _qwindows 0 _first)
    get_filename_component(_platforms "${_first}" DIRECTORY)
    get_filename_component(_plugins "${_platforms}" DIRECTORY)
else()
    set(_plugins "${_bin}")
endif()
if(_quickqmldir)
    list(GET _quickqmldir 0 _first)
    get_filename_component(_quick "${_first}" DIRECTORY)
    get_filename_component(_qml "${_quick}" DIRECTORY)
else()
    set(_qml "${_root}/qml")
endif()

# Qt Multimedia libraries and Qt's FFmpeg DLLs, next to the executable
file(GLOB _dlls LIST_DIRECTORIES FALSE
    "${STUDIO_QT_BINS}/Qt6Multimedia.dll"
    "${STUDIO_QT_BINS}/Qt6MultimediaQuick.dll"
    "${STUDIO_QT_BINS}/avcodec-*.dll"
    "${STUDIO_QT_BINS}/avformat-*.dll"
    "${STUDIO_QT_BINS}/avutil-*.dll"
    "${STUDIO_QT_BINS}/swresample-*.dll"
    "${STUDIO_QT_BINS}/swscale-*.dll"
)
foreach(_dll IN LISTS _dlls)
    get_filename_component(_name "${_dll}" NAME)
    if(NOT EXISTS "${_bin}/${_name}")
        message(STATUS "ensure_qt_multimedia: adding ${_name}")
        file(COPY "${_dll}" DESTINATION "${_bin}")
    endif()
endforeach()

# the FFmpeg media backend
file(GLOB_RECURSE _have_backend LIST_DIRECTORIES FALSE "${_root}/*ffmpegmediaplugin.dll")
if(NOT _have_backend AND EXISTS "${STUDIO_QT_PLUGINS}/multimedia/ffmpegmediaplugin.dll")
    message(STATUS "ensure_qt_multimedia: adding multimedia/ffmpegmediaplugin.dll to ${_plugins}")
    file(COPY "${STUDIO_QT_PLUGINS}/multimedia/ffmpegmediaplugin.dll" DESTINATION "${_plugins}/multimedia")
endif()

# the QtMultimedia QML module (VideoOutput)
if(NOT EXISTS "${_qml}/QtMultimedia/qmldir" AND EXISTS "${STUDIO_QT_QML}/QtMultimedia")
    message(STATUS "ensure_qt_multimedia: adding QML module QtMultimedia to ${_qml}")
    file(COPY "${STUDIO_QT_QML}/QtMultimedia" DESTINATION "${_qml}"
        PATTERN "*.qmltypes" EXCLUDE
        PATTERN "*d.dll" EXCLUDE
        PATTERN "*.pdb" EXCLUDE)
endif()

file(GLOB_RECURSE _report LIST_DIRECTORIES FALSE
    "${_root}/*ffmpegmediaplugin.dll"
    "${_root}/*avcodec-*.dll"
    "${_root}/*avformat-*.dll"
    "${_root}/*Qt6Multimedia*.dll"
    "${_root}/*/QtMultimedia/qmldir")
message(STATUS "ensure_qt_multimedia: shipped: ${_report}")
