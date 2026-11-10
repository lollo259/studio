/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore Limited and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

pragma ComponentBehavior: Bound

import QtQuick

import Muse.UiComponents
import Muse.Ui

import MuseScore.NotationScene
import MuseScore.Playback

Item {
    id: root

    property PlaybackToolBarModel playbackModel: null

    property NavigationPanel navPanel: null
    readonly property int navigationOrderEnd: tempoLoader.navigationOrderEnd

    property bool floating: false

    // Not `+ endSeparator.width`: this way, the separator itself is outside the view,
    // which means that it will be exactly at the position of the KDDockWidgets separator
    // between this toolbar and the undo/redo toolbar.
    width: endSeparator.visible ? endSeparator.x
                                : tempoLoader.x + tempoLoader.width
    height: 30

    ListView {
        id: buttonsListView

        anchors.left: parent.left

        width: contentWidth
        height: contentHeight

        contentHeight: root.height
        spacing: 4

        model: root.playbackModel

        orientation: Qt.Horizontal
        interactive: false

        readonly property int navigationOrderEnd: count

        delegate: FlatButton {
            id: btn

            required property MenuItem item
            required property int index

            width: 30
            height: width

            icon: Boolean(item) ? item.icon : IconCode.NONE

            toolTipTitle: Boolean(item) ? item.title : ""
            toolTipDescription: Boolean(item) ? item.description : ""
            toolTipShortcut: Boolean(item) ? item.shortcuts : ""

            iconFont: ui.theme.toolbarIconsFont

            accentButton: (Boolean(item) && item.checked) || menuLoader.isMenuOpened
            transparent: !accentButton

            navigation.panel: root.navPanel
            navigation.name: toolTipTitle
            navigation.order: index
            accessible.name: (item.checkable ? (item.checked ? item.title + "  " + qsTrc("global", "On") :
                                                               item.title + "  " + qsTrc("global", "Off")) : item.title)

            onClicked: {
                if (menuLoader.isMenuOpened || item.subitems.length) {
                    menuLoader.toggleOpened(item.subitems)
                    return
                }

                Qt.callLater(root.playbackModel.handleMenuItem, item.id)
            }

            StyledMenuLoader {
                id: menuLoader

                onHandleMenuItem: function(itemId) {
                    root.playbackModel.handleMenuItem(itemId)
                }
            }
        }
    }

    SeparatorLine {
        id: buttonsSeparator

        anchors.left: buttonsListView.right
        anchors.leftMargin: 6
        anchors.topMargin: 2
        anchors.bottomMargin: 2

        orientation: Qt.Vertical
        visible: !root.floating
    }

    // Studio (TE): time, bar and tempo sit on a black display
    Rectangle {
        id: studioDisplay

        visible: !root.floating
        anchors.left: timeField.left
        anchors.leftMargin: -10
        anchors.right: tempoLoader.right
        anchors.rightMargin: -6
        anchors.verticalCenter: parent.verticalCenter
        height: Math.max(28, root.height - 8)
        radius: 6
        color: "#0B0B0C"
    }

    TimeInputField {
        id: timeField

        textColor: root.floating ? ui.theme.fontPrimaryColor : "#E8E8E4"

        anchors.left: buttonsSeparator.right
        anchors.leftMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: 1 // for nicer visual alignment

        //! NOTE: explicit width prevents the content from jumping around
        // when a score is being played
        // See: https://github.com/musescore/MuseScore/issues/9633
        width: 64

        maxTime: root.playbackModel.maxPlayTime
        maxMillisecondsNumber: 9
        time: root.playbackModel.playTime

        navigationPanel: root.navPanel
        navigationOrderStart: buttonsListView.navigationOrderEnd + 1

        onTimeEdited: function(newTime) {
            root.playbackModel.playTime = newTime
        }
    }

    // Studio look E: the total length of the piece, next to play
    StyledTextLabel {
        id: totalLabel

        anchors.left: timeField.right
        anchors.leftMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: 1

        readonly property var t: root.playbackModel.maxPlayTime
        text: !t ? "" : "/ " + (t.getHours() > 0 ? Qt.formatTime(t, "h:mm:ss") : Qt.formatTime(t, "m:ss"))
        font: timeField.font
        color: root.floating ? ui.theme.fontPrimaryColor : "#8A8A8E"
        opacity: root.floating ? 0.6 : 1.0
    }

    MeasureAndBeatFields {
        id: measureAndBeatFields

        anchors.left: totalLabel.right
        anchors.leftMargin: 6
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: 1 // for nicer visual alignment

        measureNumber: root.playbackModel.measureNumber
        maxMeasureNumber: root.playbackModel.maxMeasureNumber
        beatNumber: root.playbackModel.beatNumber
        maxBeatNumber: root.playbackModel.maxBeatNumber

        font: timeField.font
        textColor: root.floating ? ui.theme.fontPrimaryColor : "#FF6A13"

        navigationPanel: root.navPanel
        navigationOrderStart: timeField.navigationOrderEnd + 1

        onMeasureNumberEdited: function(newValue) {
            root.playbackModel.measureNumber = newValue
        }

        onBeatNumberEdited: function(newValue) {
            root.playbackModel.beatNumber = newValue
        }
    }

    Loader {
        id: tempoLoader

        anchors.left: measureAndBeatFields.right
        anchors.leftMargin: 6

        readonly property int navigationOrderEnd: item?.navigation?.order ?? measureAndBeatFields.navigationOrderEnd

        // Fixed width prevents items from jumping around; but we
        // scale it according to the font size to prevent clipping
        readonly property real tempoViewWidth: 5 * ui.theme.bodyFont.pixelSize

        sourceComponent: root.floating ? tempoViewComponent : tempoButtonComponent

        Component {
            id: tempoViewComponent

            Item {
                implicitWidth: tempoLoader.tempoViewWidth
                implicitHeight: root.height

                TempoView {
                    id: tempoView
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.verticalCenterOffset: 1 // for nicer visual alignment

                    noteSymbol: root.playbackModel.tempo.noteSymbol
                    tempoValue: root.playbackModel.tempo.value

                    noteSymbolFont.pixelSize: ui.theme.iconsFont.pixelSize
                    tempoValueFont: timeField.font
                }
            }
        }

        Component {
            id: tempoButtonComponent

            PopupButton {
                id: playbackSpeedButton

                implicitWidth: tempoLoader.tempoViewWidth
                implicitHeight: root.height

                transparent: !root.isPopupOpened
                studioKeys: false

                toolTipTitle: qsTrc("playback", "Speed")

                navigation.panel: root.navPanel
                navigation.order: measureAndBeatFields.navigationOrderEnd + 1

                contentItem: TempoView {
                    textColor: "#5B8CFF"
                    anchors.centerIn: parent
                    anchors.verticalCenterOffset: 1 // for nicer visual alignment

                    noteSymbol: root.playbackModel.tempo.noteSymbol
                    tempoValue: root.playbackModel.tempo.value

                    noteSymbolFont.pixelSize: ui.theme.iconsFont.pixelSize
                    tempoValueFont: timeField.font
                }

                property PlaybackToolBarModel playbackModel: root.playbackModel

                popupComponent: PlaybackSpeedPopup {
                    playbackModel: playbackSpeedButton.playbackModel
                }
            }
        }
    }

    SeparatorLine {
        id: endSeparator
        anchors.left: tempoLoader.right
        anchors.leftMargin: 6
        anchors.topMargin: 2
        anchors.bottomMargin: 2

        orientation: Qt.Vertical
        visible: !root.floating
    }
}
