muse_create_qml_module(instruments_qml FOR instruments)

qt_add_qml_module(instruments_qml
    URI Audacity.Instruments
    VERSION 1.0
    SOURCES
        instrumentspanelmodel.cpp
        instrumentspanelmodel.h
        samplerpanelmodel.cpp
        samplerpanelmodel.h
        videopanelmodel.cpp
        videopanelmodel.h
        tempostripmodel.h
    QML_FILES
        InstrumentsDialog.qml
        SamplerDialog.qml
        VideoWindow.qml
        TempoStrip.qml
    IMPORTS
        TARGET muse_ui_qml
        TARGET muse_uicomponents_qml
)

# the Video window: QMediaPlayer (C++) drawing into a QML VideoOutput
find_package(Qt6 REQUIRED COMPONENTS Multimedia WebSockets)
target_link_libraries(instruments_qml PRIVATE Qt6::Multimedia Qt6::WebSockets)

fixup_qml_module_dependencies(instruments_qml)
