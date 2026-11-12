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

    headerTitle: qsTrc("playback", "Pan")

    Item {
        id: content

        required property MixerChannelItem channelItem

        height: contentRow.implicitHeight
        width: root.channelItemWidth

        property string accessibleName: (Boolean(root.needReadChannelName) ? channelItem.title + " " : "") + root.headerTitle

        Row {
            id: contentRow

            anchors.horizontalCenter: parent.horizontalCenter

            spacing: 8

            KnobControl {
                id: balanceKnob

                from: content.channelItem.balanceMin
                to: content.channelItem.balanceMax
                value: content.channelItem.balance
                stepSize: 1
                isBalanceKnob: true

                // Studio: OP-1 colours - blue, green, white, orange - picked by the channel's name
                capColor: {
                    if (content.channelItem.type === MixerChannelItem.Master) {
                        return "#F4F4F5"
                    }
                    const colours = ["#3E7BD6", "#49B26A", "#F4F4F5", "#FF6A13"]
                    let h = 0
                    const t = content.channelItem.title
                    for (let i = 0; i < t.length; ++i) {
                        h = (h * 31 + t.charCodeAt(i)) % 9973
                    }
                    return colours[h % colours.length]
                }
                enabled: !content.channelItem.hasBalanceAutomation
                opacity: enabled ? 1.0 : ui.theme.itemOpacityDisabled

                navigation.panel: content.channelItem.panel
                navigation.row: root.navigationRowStart
                navigation.accessible.name: content.accessibleName
                navigation.onActiveChanged: {
                    if (navigation.active) {
                        root.navigateControlIndexChanged({row: navigation.row, column: navigation.column})
                    }
                }

                onNewValueRequested: function(newValue) {
                    content.channelItem.balance = newValue
                }
            }

            TextInputField {
                id: balanceTextInputField

                anchors.verticalCenter: balanceKnob.verticalCenter

                height: 24
                width: 36

                enabled: !content.channelItem.hasBalanceAutomation

                textHorizontalAlignment: Qt.AlignHCenter
                textSidePadding: 0
                background.radius: 2

                navigation.panel: content.channelItem.panel
                navigation.row: root.navigationRowStart + 1
                navigation.accessible.name: content.accessibleName + " " + currentText
                navigation.onActiveChanged: {
                    if (navigation.active) {
                        root.navigateControlIndexChanged({row: navigation.row, column: navigation.column})
                    }
                }

                validator: IntInputValidator {
                    id: intInputValidator
                    top: content.channelItem.balanceMax
                    bottom: content.channelItem.balanceMin
                }

                currentText: content.channelItem.balance

                onTextEdited: function(newTextValue) {
                    if (content.channelItem.balance !== Number(newTextValue)) {
                        content.channelItem.balance = Number(newTextValue)
                    }
                }
            }
        }
    }
}
