/*
 * Tape (Laurence Who Studio) — Instruments panel
 * Pick a sound, play it from your keyboard, record takes.
 * SPDX-License-Identifier: GPL-3.0-only
 */
import QtQuick 2.15
import QtQuick.Layouts 1.15

import Muse.Ui 1.0
import Muse.UiComponents

import Audacity.Instruments

StyledDialogView {
    id: root

    title: qsTrc("instruments", "Instruments")

    contentWidth: 860
    contentHeight: 620
    resizable: true

    readonly property color accent: "#FF6A13"

    InstrumentsPanelModel {
        id: panel
    }

    Component.onCompleted: panel.load()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 14

        // ── black display: what is playing ─────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 92
            radius: 10
            color: "#121214"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    StyledTextLabel {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignLeft
                        text: panel.loading ? qsTrc("instruments", "loading…") : panel.currentSound
                        font.pixelSize: 26
                        color: "#EDEDE8"
                    }
                    StyledTextLabel {
                        text: panel.lastNote
                        font.pixelSize: 22
                        color: root.accent
                    }
                }
                StyledTextLabel {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignLeft
                    text: panel.status
                    color: "#9A9A96"
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 16

            // ── sounds ─────────────────────────────────────────────
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 8

                StyledTextLabel {
                    text: qsTrc("instruments", "Sounds")
                    font: ui.theme.bodyBoldFont
                }

                StyledListView {
                    id: list
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: panel.sounds
                    spacing: 2
                    clip: true

                    delegate: FlatButton {
                        width: list.width
                        height: 34
                        transparent: modelData.name !== panel.currentSound
                        accentButton: modelData.name === panel.currentSound
                        text: modelData.family + "   ·   " + modelData.name + "   ·   " + modelData.library + (modelData.soundfont ? "   (sf2: next update)" : "")
                        onClicked: panel.selectSound(modelData.index)
                    }
                }

                StyledTextLabel {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignLeft
                    wrapMode: Text.WordWrap
                    maximumLineCount: 3
                    visible: list.count <= 3
                    text: qsTrc("instruments", "Only the built-in sounds so far. Unzip your libraries (Salamander, VSCO…) inside %1, then press Rescan.").arg(panel.soundsFolder)
                }
            }

            // ── controls ───────────────────────────────────────────
            ColumnLayout {
                Layout.preferredWidth: 280
                Layout.fillHeight: true
                spacing: 12

                StyledTextLabel {
                    text: qsTrc("instruments", "Keyboard")
                    font: ui.theme.bodyBoldFont
                }
                StyledDropdown {
                    Layout.fillWidth: true
                    model: panel.keyboards
                    indeterminateText: qsTrc("instruments", "no MIDI keyboard found")
                    currentIndex: {
                        for (var i = 0; i < panel.keyboards.length; ++i) {
                            if (panel.keyboards[i].value === panel.currentKeyboard) {
                                return i
                            }
                        }
                        return -1
                    }
                    onActivated: function(index, value) {
                        panel.selectKeyboard(value)
                    }
                }

                StyledTextLabel {
                    text: qsTrc("instruments", "Volume")
                    font: ui.theme.bodyBoldFont
                }
                StyledSlider {
                    Layout.fillWidth: true
                    from: 0
                    to: 1.5
                    value: panel.volume
                    onMoved: panel.volume = value
                }

                FlatButton {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 48
                    accentButton: true
                    text: panel.taking ? qsTrc("instruments", "■  Stop take  ") + panel.takeTime
                                       : qsTrc("instruments", "●  Record take")
                    onClicked: panel.toggleTake()
                }
                StyledTextLabel {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignLeft
                    wrapMode: Text.WordWrap
                    maximumLineCount: 3
                    text: panel.taking ? qsTrc("instruments", "Recording what you play (audio + MIDI)…")
                                       : qsTrc("instruments", "When you stop, the take is added to the project as a new track. A MIDI copy is kept for Score.")
                    color: ui.theme.fontSecondaryColor
                }

                Item { Layout.fillHeight: true }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 8
                    rowSpacing: 8
                    FlatButton { Layout.fillWidth: true; text: qsTrc("instruments", "Sounds folder"); onClicked: panel.openSoundsFolder() }
                    FlatButton { Layout.fillWidth: true; text: qsTrc("instruments", "Rescan"); onClicked: panel.rescan() }
                    FlatButton { Layout.fillWidth: true; text: qsTrc("instruments", "Takes folder"); onClicked: panel.openTakesFolder() }
                    FlatButton { Layout.fillWidth: true; text: qsTrc("instruments", "All notes off"); onClicked: panel.allNotesOff() }
                    FlatButton { Layout.fillWidth: true; text: qsTrc("instruments", "New instrument track"); onClicked: panel.newInstrumentTrack() }
                    FlatButton { Layout.fillWidth: true; text: qsTrc("instruments", "Last take in Score"); enabled: panel.lastTake !== ""; onClicked: panel.openLastTakeInScore() }
                    FlatButton { Layout.fillWidth: true; Layout.columnSpan: 2; text: qsTrc("instruments", "Restart audio"); onClicked: panel.restartAudio() }
                }
            }
        }

        // ── test keyboard (no MIDI needed) ─────────────────────────
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 70

            readonly property int firstNote: 48 // C3
            readonly property var whiteSteps: [0, 2, 4, 5, 7, 9, 11]
            readonly property int whiteCount: 22

            Repeater {
                model: parent.whiteCount
                Rectangle {
                    readonly property int note: parent.firstNote + Math.floor(index / 7) * 12 + parent.whiteSteps[index % 7]
                    x: index * (parent.width / parent.whiteCount)
                    width: parent.width / parent.whiteCount - 2
                    height: parent.height
                    radius: 3
                    color: whiteArea.pressed ? root.accent : "#F3F3F1"
                    border.color: "#A9AAAD"
                    MouseArea {
                        id: whiteArea
                        anchors.fill: parent
                        onPressed: panel.previewNote(parent.note, true)
                        onReleased: panel.previewNote(parent.note, false)
                        onCanceled: panel.previewNote(parent.note, false)
                    }
                }
            }
            Repeater {
                model: parent.whiteCount
                Rectangle {
                    readonly property int step: index % 7
                    readonly property bool hasBlack: step !== 2 && step !== 6 && index < parent.whiteCount - 1
                    readonly property int note: parent.firstNote + Math.floor(index / 7) * 12 + parent.whiteSteps[step] + 1
                    visible: hasBlack
                    x: (index + 1) * (parent.width / parent.whiteCount) - width / 2 - 1
                    width: (parent.width / parent.whiteCount) * 0.6
                    height: parent.height * 0.6
                    radius: 2
                    color: blackArea.pressed ? root.accent : "#26262A"
                    MouseArea {
                        id: blackArea
                        anchors.fill: parent
                        onPressed: panel.previewNote(parent.note, true)
                        onReleased: panel.previewNote(parent.note, false)
                        onCanceled: panel.previewNote(parent.note, false)
                    }
                }
            }
        }
    }
}
