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

import Muse.Ui
import Muse.UiComponents
import MuseScore.Playback

MixerPanelSection {
    id: root

    headerTitle: qsTrc("playback", "Name")

    Rectangle {
        id: content

        required property MixerChannelItem channelItem

        width: root.channelItemWidth
        height: 22

        function resolveLabelColor() {
            switch(channelItem.type) {
            case MixerChannelItem.PrimaryInstrument:
            case MixerChannelItem.SecondaryInstrument:
                return ui.theme.accentColor
            case MixerChannelItem.Aux:
                return "#63D47B"
            case MixerChannelItem.Master:
                return "#F2F2F0"
            }

            return ui.theme.accentColor
        }

        readonly property color labelColor: resolveLabelColor()

        // Studio: the name sits on a little black display, with an LED for the kind of channel
        color: "#121214"
        radius: 4

        Rectangle {
            anchors.left: parent.left
            anchors.leftMargin: 7
            anchors.verticalCenter: parent.verticalCenter
            width: 6
            height: 6
            radius: 3
            color: content.labelColor
            opacity: content.channelItem.type === MixerChannelItem.SecondaryInstrument ? 0.5 : 1.0
        }

        StyledTextLabel {
            id: textLabel
            anchors.centerIn: parent

            font.family: "Consolas"
            font.pixelSize: 11
            color: "#F2F2F0"

            readonly property int margin: -16
            width: margin + parent.width + margin

            text: content.channelItem.title.toLowerCase()
        }

        MouseArea {
            id: mouseArea
            anchors.fill: parent

            enabled: parent.enabled
            hoverEnabled: true

            onContainsMouseChanged: {
                if (mouseArea.containsMouse && textLabel.truncated) {
                    ui.tooltip.show(mouseArea, content.channelItem.title)
                } else {
                    ui.tooltip.hide(mouseArea)
                }
            }
        }
    }
}
