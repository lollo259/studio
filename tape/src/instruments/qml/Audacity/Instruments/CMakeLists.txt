muse_create_qml_module(instruments_qml FOR instruments)

qt_add_qml_module(instruments_qml
    URI Audacity.Instruments
    VERSION 1.0
    SOURCES
        instrumentspanelmodel.cpp
        instrumentspanelmodel.h
    QML_FILES
        InstrumentsDialog.qml
    IMPORTS
        TARGET muse_ui_qml
        TARGET muse_uicomponents_qml
)

fixup_qml_module_dependencies(instruments_qml)
