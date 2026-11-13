/*
 * Tape (Laurence Who Studio) — Video
 * The project's film, locked to Tape's playhead: it plays, stops and seeks with Tape
 * (and with Score, through the Studio link). A window of its own, so it can sit on another screen.
 * SPDX-License-Identifier: GPL-3.0-only
 */
import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import QtMultimedia

import Muse.Ui
import Muse.UiComponents

import Audacity.Instruments

StyledDialogView {
    id: root

    title: panel.hasVideo ? qsTrc("instruments", "Video · %1").arg(panel.videoName) : qsTrc("instruments", "Video")

    contentWidth: 800
    contentHeight: 540
    resizable: true
    modal: false
    closeOnEscape: false

    readonly property color accent: "#FF5A1F"
    readonly property color screenColor: "#000000"
    readonly property color screenText: "#EDEDE8"
    readonly property color screenDim: "#9A9A96"
    readonly property string monoFamily: Qt.platform.os === "windows" ? "Consolas" : "monospace"
    readonly property bool fullScreen: root.window ? root.window.visibility === Window.FullScreen : false

    function toggleFullScreen() {
        if (!root.window) {
            return;
        }
        if (root.fullScreen) {
            root.window.showNormal();
        } else {
            panel.saveWindow(root.window);
            root.window.showFullScreen();
        }
    }

    VideoPanelModel {
        id: panel
    }

    Component.onCompleted: {
        panel.videoOutput = output;
        panel.load();
    }

    onOpened: panel.restoreWindow(root.window)
    onAboutToClose: panel.saveWindow(root.window)

    Shortcut {
        sequence: "Space"
        context: Qt.WindowShortcut
        onActivated: panel.togglePlay()
    }
    Shortcut {
        sequence: "Ctrl+Shift+V"
        context: Qt.WindowShortcut
        onActivated: root.hide()
    }
    Shortcut {
        sequence: "C"
        context: Qt.WindowShortcut
        onActivated: panel.addHitPoint()
    }
    Shortcut {
        sequence: "Shift+C"
        context: Qt.WindowShortcut
        onActivated: panel.removeHitPoint()
    }
    Shortcut {
        sequence: "F"
        context: Qt.WindowShortcut
        onActivated: root.toggleFullScreen()
    }
    Shortcut {
        sequence: "Escape"
        context: Qt.WindowShortcut
        onActivated: {
            if (root.fullScreen) {
                root.toggleFullScreen();
            } else {
                root.hide();
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── the picture ─────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: root.screenColor

            VideoOutput {
                id: output
                anchors.fill: parent
                fillMode: VideoOutput.PreserveAspectFit
                visible: panel.hasVideo && panel.error === ""
            }

            // outside the film: the held frame is dimmed
            Rectangle {
                anchors.fill: parent
                color: "#000000"
                opacity: 0.6
                visible: output.visible && panel.pictureState !== ""
            }

            StyledTextLabel {
                anchors.top: parent.top
                anchors.topMargin: 14
                anchors.horizontalCenter: parent.horizontalCenter
                visible: output.visible && panel.pictureState !== ""
                text: panel.pictureState
                font.pixelSize: 14
                font.bold: true
                font.letterSpacing: 2
                color: root.accent
            }

            // Studio: the score's wait cues and the cues placed on the video, along the film
            Item {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 10
                visible: output.visible

                Rectangle {
                    anchors.fill: parent
                    color: "#80000000"
                }
                Repeater {
                    model: panel.markers
                    delegate: Rectangle {
                        x: modelData.x * (parent.width - 2)
                        width: 2
                        height: parent.height
                        color: modelData.kind === "wait" ? root.accent : "#c828a0"
                    }
                }
            }

            // Studio: on a cue
            Rectangle {
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.margins: 14
                visible: output.visible && panel.cueFlash !== ""
                width: cueLabel.implicitWidth + 20
                height: cueLabel.implicitHeight + 8
                radius: 4
                color: panel.cueFlash === "CUE" ? root.accent : "#c828a0"

                Text {
                    id: cueLabel
                    anchors.centerIn: parent
                    text: panel.cueFlash
                    font.family: root.monoFamily
                    font.pixelSize: 18
                    font.bold: true
                    color: "#ffffff"
                }
            }

            // timecode of the picture: HH:MM:SS:FF
            Rectangle {
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 20
                anchors.horizontalCenter: parent.horizontalCenter
                visible: panel.hasVideo && panel.error === ""
                width: timecodeLabel.implicitWidth + 24
                height: timecodeLabel.implicitHeight + 10
                radius: 4
                color: "#B3000000"

                Text {
                    id: timecodeLabel
                    anchors.centerIn: parent
                    text: panel.timecode
                    font.family: root.monoFamily
                    font.pixelSize: root.fullScreen ? 40 : 28
                    color: panel.playing ? root.screenText : root.accent
                }
            }

            // nothing to show yet
            ColumnLayout {
                anchors.centerIn: parent
                visible: !panel.hasVideo || panel.error !== ""
                spacing: 14

                StyledTextLabel {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.maximumWidth: root.width - 80
                    wrapMode: Text.WordWrap
                    text: panel.error !== "" ? panel.error : panel.hasProject ? qsTrc("instruments", "No video in this project") : qsTrc("instruments", "Open a project to add its video")
                    font.pixelSize: 18
                    color: panel.error !== "" ? root.accent : root.screenDim
                }
                FlatButton {
                    Layout.alignment: Qt.AlignHCenter
                    visible: panel.hasProject
                    accentButton: true
                    text: qsTrc("instruments", "Import video…")
                    onClicked: panel.chooseVideo()
                }
            }

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                enabled: panel.hasVideo
                onDoubleClicked: root.toggleFullScreen()
            }
        }

        // ── controls: aluminium strip ───────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: controls.implicitHeight + 20
            visible: !root.fullScreen
            color: ui.theme.backgroundPrimaryColor

            Rectangle {
                anchors.top: parent.top
                width: parent.width
                height: 2
                color: root.accent
            }

            RowLayout {
                id: controls
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                anchors.topMargin: 12
                anchors.bottomMargin: 8
                spacing: 8

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    StyledTextLabel {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignLeft
                        elide: Text.ElideMiddle
                        text: panel.hasVideo ? panel.videoName : qsTrc("instruments", "No video")
                        font: ui.theme.bodyBoldFont
                    }
                    StyledTextLabel {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignLeft
                        elide: Text.ElideRight
                        text: panel.videoInfo
                        color: ui.theme.fontSecondaryColor
                        visible: text !== ""
                    }
                }

                StyledTextLabel {
                    text: qsTrc("instruments", "Starts at")
                }
                TextInputField {
                    Layout.preferredWidth: 120
                    enabled: panel.hasVideo
                    currentText: panel.offsetText
                    hint: "00:00:00:00"
                    onTextEditingFinished: function (newTextValue) {
                        panel.setOffsetText(newTextValue);
                    }
                }
                FlatButton {
                    enabled: panel.hasVideo
                    text: qsTrc("instruments", "At playhead")
                    toolTipTitle: qsTrc("instruments", "Start the video at the playhead")
                    onClicked: panel.offsetToPlayhead()
                }
                StyledDropdown {
                    Layout.preferredWidth: 110
                    model: panel.fpsOptions
                    currentIndex: panel.fpsIndex
                    onActivated: function (index, value) {
                        panel.fpsIndex = value;
                    }
                }
                FlatButton {
                    enabled: panel.hasVideo
                    text: panel.hitPointCount > 0 ? qsTrc("instruments", "Cue here (%1)").arg(panel.hitPointCount) : qsTrc("instruments", "Cue here")
                    toolTipTitle: qsTrc("instruments", "A cue at this frame: Score shows it in the bars (C; Shift+C removes the nearest)")
                    onClicked: panel.addHitPoint()
                }
                FlatButton {
                    enabled: panel.hasVideo
                    text: qsTrc("instruments", "Import sound")
                    toolTipTitle: qsTrc("instruments", "Import the video's sound as a track")
                    onClicked: panel.importSound()
                }
                FlatButton {
                    enabled: panel.hasProject
                    text: panel.hasVideo ? qsTrc("instruments", "Change…") : qsTrc("instruments", "Import video…")
                    onClicked: panel.chooseVideo()
                }
                FlatButton {
                    visible: panel.hasVideo
                    text: qsTrc("instruments", "Remove")
                    toolTipTitle: qsTrc("instruments", "Remove the video from this project")
                    onClicked: panel.clearVideo()
                }
                // Studio: Live (OBS) - the film goes there as "Studio video", in step with Tape
                FlatButton {
                    text: panel.liveConnected ? qsTrc("instruments", "● Live") : qsTrc("instruments", "Live")
                    accentButton: panel.liveConnected
                    toolTipTitle: qsTrc("instruments", "Open Live (OBS): the film plays there with Tape")
                    onClicked: panel.openLive()
                }
                FlatButton {
                    visible: panel.liveConnected
                    text: panel.liveStreaming ? qsTrc("instruments", "Stop stream") : qsTrc("instruments", "Stream")
                    accentButton: panel.liveStreaming
                    onClicked: panel.toggleStream()
                }
                FlatButton {
                    visible: panel.liveConnected
                    text: panel.liveRecording ? qsTrc("instruments", "Stop rec") : qsTrc("instruments", "Rec")
                    accentButton: panel.liveRecording
                    onClicked: panel.toggleRecord()
                }
                StyledDropdown {
                    visible: panel.liveConnected && panel.liveScenes.length > 0
                    Layout.preferredWidth: 140
                    model: panel.liveScenes.map(function (n) { return { "text": n, "value": n } })
                    currentIndex: panel.liveScenes.indexOf(panel.liveScene)
                    onActivated: function (index, value) {
                        panel.setLiveScene(value);
                    }
                }
                FlatButton {
                    enabled: panel.hasVideo
                    text: qsTrc("instruments", "Full screen")
                    toolTipTitle: qsTrc("instruments", "Full screen (F, or double-click the picture; Esc to leave)")
                    onClicked: root.toggleFullScreen()
                }
            }
        }
    }
}
