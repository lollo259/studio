/*
 * Tape (Laurence Who Studio) — Sampler
 * 16 pads: load sounds, trim them and play them from the pads or your keyboard.
 * The kit plays through the live synth as a generated SFZ (Documents/Studio/Sampler).
 * SPDX-License-Identifier: GPL-3.0-only
 */
import QtQuick 2.15
import QtQuick.Layouts 1.15

import Muse.Ui 1.0
import Muse.UiComponents

import Audacity.Instruments

StyledDialogView {
    id: root

    title: qsTrc("instruments", "Sampler")

    contentWidth: 660
    contentHeight: 800
    resizable: true

    readonly property color accent: "#FF6A13"
    readonly property color screenColor: "#121214"
    readonly property color screenText: "#EDEDE8"
    readonly property color screenDim: "#9A9A96"

    readonly property var modes: [
        { id: "one-shot", text: qsTrc("instruments", "One-shot"), short: "1-SHOT" },
        { id: "gate", text: qsTrc("instruments", "Gate"), short: "GATE" },
        { id: "loop", text: qsTrc("instruments", "Loop"), short: "LOOP" },
        { id: "keys", text: qsTrc("instruments", "Keys"), short: "KEYS" }
    ]

    function modeShort(id) {
        for (var i = 0; i < modes.length; ++i) {
            if (modes[i].id === id) {
                return modes[i].short
            }
        }
        return ""
    }

    SamplerPanelModel {
        id: panel
    }

    Component.onCompleted: panel.load()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12

        // ── kit ─────────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            StyledTextLabel {
                text: qsTrc("instruments", "Kit")
                font: ui.theme.bodyBoldFont
            }
            TextInputField {
                Layout.fillWidth: true
                currentText: panel.kitName
                hint: qsTrc("instruments", "Kit name")
                onTextEditingFinished: function(newTextValue) {
                    panel.kitName = newTextValue
                }
            }
            StyledDropdown {
                Layout.preferredWidth: 170
                model: panel.kits.map(function(k) { return { text: k, value: k } })
                currentIndex: panel.kits.indexOf(panel.kitName)
                indeterminateText: qsTrc("instruments", "Open kit")
                onActivated: function(index, value) {
                    panel.openKit(value)
                }
            }
            FlatButton {
                text: qsTrc("instruments", "New kit")
                onClicked: panel.newKit()
            }
            FlatButton {
                text: qsTrc("instruments", "Kits folder")
                onClicked: panel.openFolder()
            }
        }

        // ── black screen: waveform of the selected pad ──────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 200
            radius: 10
            color: root.screenColor
            border.width: screenDrop.containsDrag ? 2 : 0
            border.color: root.accent

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    StyledTextLabel {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignLeft
                        text: panel.padLoaded ? panel.padName : qsTrc("instruments", "Pad %1 · empty").arg(panel.selectedPad + 1)
                        font.pixelSize: 22
                        color: panel.padLoaded ? root.screenText : root.screenDim
                    }
                    StyledTextLabel {
                        text: panel.padLoaded ? root.modeShort(panel.padMode) + (panel.padChoke ? "  ·  CHOKE" : "") : ""
                        font.pixelSize: 14
                        color: root.accent
                    }
                }

                Item {
                    id: wave
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    readonly property bool hasPeaks: panel.peaks.length > 1

                    Canvas {
                        id: canvas
                        anchors.fill: parent

                        property var peaks: panel.peaks
                        property real start: panel.padStart
                        property real end: panel.padEnd
                        onPeaksChanged: requestPaint()
                        onStartChanged: requestPaint()
                        onEndChanged: requestPaint()
                        onWidthChanged: requestPaint()
                        onHeightChanged: requestPaint()

                        onPaint: {
                            var ctx = getContext("2d")
                            ctx.reset()
                            var w = width
                            var h = height
                            var mid = h / 2
                            ctx.lineWidth = 1
                            if (!wave.hasPeaks) {
                                if (panel.padLoaded) {
                                    ctx.strokeStyle = "#55555A"
                                    ctx.beginPath()
                                    ctx.moveTo(0, mid)
                                    ctx.lineTo(w, mid)
                                    ctx.stroke()
                                }
                                return
                            }
                            var n = peaks.length / 2
                            for (var i = 0; i < n; ++i) {
                                var pos = i / n
                                var x = Math.floor(pos * w) + 0.5
                                var lo = peaks[2 * i]
                                var hi = peaks[2 * i + 1]
                                var inside = pos >= start && pos <= end
                                ctx.strokeStyle = inside ? "#EDEDE8" : "#4A4A4F"
                                ctx.beginPath()
                                ctx.moveTo(x, mid - hi * mid * 0.95)
                                ctx.lineTo(x, mid - lo * mid * 0.95 + 1)
                                ctx.stroke()
                            }
                        }
                    }

                    StyledTextLabel {
                        anchors.centerIn: parent
                        visible: !panel.padLoaded
                        text: qsTrc("instruments", "Drop a sound here or on a pad")
                        color: root.screenDim
                    }

                    StyledTextLabel {
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        visible: panel.padLoaded && !wave.hasPeaks
                        text: qsTrc("instruments", "waveform and trim: WAV files only")
                        color: root.screenDim
                    }

                    // start / end markers
                    Repeater {
                        model: panel.padCanTrim ? 2 : 0

                        Item {
                            id: marker
                            readonly property bool isStart: index === 0
                            readonly property real value: isStart ? panel.padStart : panel.padEnd

                            x: value * wave.width - width / 2
                            width: 18
                            height: wave.height

                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: 2
                                height: parent.height
                                color: root.accent
                            }
                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                y: marker.isStart ? 0 : parent.height - height
                                width: 12
                                height: 12
                                radius: 2
                                color: root.accent
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.SizeHorCursor
                                preventStealing: true
                                onPositionChanged: function(mouse) {
                                    if (!pressed) {
                                        return
                                    }
                                    var v = marker.mapToItem(wave, mouse.x, 0).x / Math.max(1, wave.width)
                                    v = Math.max(0, Math.min(1, v))
                                    if (marker.isStart) {
                                        panel.padStart = v
                                    } else {
                                        panel.padEnd = v
                                    }
                                }
                            }
                        }
                    }
                }
            }

            DropArea {
                id: screenDrop
                anchors.fill: parent
                onEntered: function(drag) {
                    drag.accepted = drag.hasUrls
                }
                onDropped: function(drop) {
                    if (panel.dropOnPad(panel.selectedPad, drop.urls)) {
                        drop.acceptProposedAction()
                    }
                }
            }
        }

        StyledTextLabel {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            elide: Text.ElideMiddle
            text: panel.padInfo
            color: ui.theme.fontSecondaryColor
        }

        // ── 16 pads (pad 1 bottom left, like most pad controllers) ──
        GridLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            columns: 4
            rowSpacing: 8
            columnSpacing: 8

            Repeater {
                model: 16

                Rectangle {
                    id: padItem

                    readonly property int padIndex: (3 - Math.floor(index / 4)) * 4 + (index % 4)
                    readonly property var info: panel.pads.length > padIndex ? panel.pads[padIndex] : null
                    readonly property bool loaded: info ? info.loaded : false
                    readonly property bool lit: (panel.litMask & (1 << padIndex)) !== 0 || padMouse.pressed
                    readonly property bool selected: panel.selectedPad === padIndex

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 64
                    radius: 8
                    color: lit ? root.accent : (loaded ? "#2A2A2E" : "#1C1C1F")
                    border.width: (selected || padDrop.containsDrag) ? 2 : 1
                    border.color: (selected || padDrop.containsDrag) ? root.accent : "#3A3A3E"

                    StyledTextLabel {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.margins: 8
                        text: (padItem.padIndex + 1) + "  " + (padItem.info ? padItem.info.key : "")
                        font.pixelSize: 10
                        color: padItem.lit ? root.screenColor : root.screenDim
                    }

                    StyledTextLabel {
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 8
                        visible: padItem.loaded
                        text: padItem.info ? root.modeShort(padItem.info.mode) + (padItem.info.choke ? " · C" : "") : ""
                        font.pixelSize: 10
                        color: padItem.lit ? root.screenColor : root.accent
                    }

                    StyledTextLabel {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: 8
                        horizontalAlignment: Text.AlignLeft
                        elide: Text.ElideRight
                        text: padItem.loaded ? padItem.info.name : "+"
                        font.pixelSize: 13
                        color: padItem.lit ? root.screenColor : (padItem.loaded ? root.screenText : root.screenDim)
                    }

                    MouseArea {
                        id: padMouse
                        anchors.fill: parent
                        onPressed: panel.previewPad(padItem.padIndex, true)
                        onReleased: panel.previewPad(padItem.padIndex, false)
                        onCanceled: panel.previewPad(padItem.padIndex, false)
                        onDoubleClicked: {
                            if (!padItem.loaded) {
                                panel.chooseSound(padItem.padIndex)
                            }
                        }
                    }

                    DropArea {
                        id: padDrop
                        anchors.fill: parent
                        onEntered: function(drag) {
                            drag.accepted = drag.hasUrls
                        }
                        onDropped: function(drop) {
                            if (panel.dropOnPad(padItem.padIndex, drop.urls)) {
                                drop.acceptProposedAction()
                            }
                        }
                    }
                }
            }
        }

        // ── mode keys for the selected pad ──────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Repeater {
                model: root.modes

                FlatButton {
                    Layout.fillWidth: true
                    enabled: panel.padLoaded
                    accentButton: panel.padMode === modelData.id
                    text: modelData.text
                    onClicked: panel.padMode = modelData.id
                }
            }

            FlatButton {
                Layout.fillWidth: true
                enabled: panel.padLoaded
                accentButton: panel.padChoke
                text: qsTrc("instruments", "Choke")
                toolTipTitle: qsTrc("instruments", "Choke group")
                toolTipDescription: qsTrc("instruments", "Pads in the choke group cut each other off, like open and closed hi-hats")
                onClicked: panel.padChoke = !panel.padChoke
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            FlatButton {
                Layout.fillWidth: true
                accentButton: !panel.padLoaded
                text: qsTrc("instruments", "Load sound…")
                onClicked: panel.chooseSound(panel.selectedPad)
            }
            TextInputField {
                Layout.fillWidth: true
                enabled: panel.padLoaded
                currentText: panel.padName
                hint: qsTrc("instruments", "Pad name")
                onTextEditingFinished: function(newTextValue) {
                    panel.padName = newTextValue
                }
            }
            FlatButton {
                text: qsTrc("instruments", "Reset trim")
                enabled: panel.padCanTrim
                onClicked: panel.resetTrim()
            }
            FlatButton {
                text: qsTrc("instruments", "Clear pad")
                enabled: panel.padLoaded
                onClicked: panel.clearPad()
            }
            FlatButton {
                text: qsTrc("instruments", "All notes off")
                onClicked: panel.allNotesOff()
            }
        }

        StyledTextLabel {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.WordWrap
            maximumLineCount: 3
            text: panel.status
        }
        StyledTextLabel {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.WordWrap
            maximumLineCount: 3
            color: ui.theme.fontSecondaryColor
            text: qsTrc("instruments", "Keys mode: the pad also plays chromatically from E3 up (C4 = its own pitch). Takes and instrument tracks record the sampler like any other sound.")
        }
    }
}
