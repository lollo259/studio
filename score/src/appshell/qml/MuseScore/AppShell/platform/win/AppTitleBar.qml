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
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Window

import Muse.Ui
import Muse.UiComponents
import MuseScore.AppShell

import "../"

Rectangle {
    id: root

    color: ui.theme.backgroundPrimaryColor

    property alias title: titleTextmetrics.text
    property rect titleMoveAreaRect: Qt.rect(titleMoveArea.x, titleMoveArea.y, titleMoveArea.width, titleMoveArea.height)

    property int windowVisibility: Window.Windowed

    property alias appWindow: menu.appWindow

    signal showWindowMinimizedRequested()
    signal toggleWindowMaximizedRequested()
    signal closeWindowRequested()

    height: content.childrenRect.height

    RowLayout {
        id: content

        anchors.left: parent.left
        anchors.right: parent.right

        spacing: 8

        // Studio: the menu sits in the middle of the title bar
        Item {
            id: leftSpacer
            Layout.fillHeight: true
            Layout.preferredWidth: menu.truncated ? 0 : Math.max(0, (root.width - menu.width) / 2 - content.spacing)
        }

        AppMenuBar {
            id: menu

            Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
            Layout.fillWidth: menu.truncated ? true : false
            Layout.preferredWidth: implicitWidth
            Layout.preferredHeight: implicitHeight

            availableWidth: root.width - (content.spacing * 4 + titleLabel.minDistanceFromMenu + systemButtons.width + panelToggles.width)
        }

        StyledTextLabel {
            id: titleLabel

            readonly property int minDistanceFromMenu: 24

            Layout.fillWidth: !menu.truncated ? true : false
            Layout.fillHeight: true
            Layout.minimumWidth: minDistanceFromMenu

            horizontalAlignment: Text.AlignLeft
            verticalAlignment: Text.AlignVCenter
            leftPadding: 0

            text: "" // Studio: no window title text, the menu is centred
            textFormat: Text.RichText
            font: ui.theme.bodyFont

            visible: root.windowVisibility !== Window.FullScreen

            TextMetrics {
                id: titleTextmetrics

                text: qsTrc("appshell", "Score")
                font: titleLabel.font
                elide: Qt.ElideRight
                elideWidth: titleLabel.width
            }
        }

        // Studio: fold the panels away / bring them back. The menu bar always stays.
        Row {
            id: panelToggles

            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
            spacing: 2

            PanelsModel {
                id: panels
            }

            Component.onCompleted: panels.init()

            Repeater {
                model: [
                    { group: "side", tip: qsTrc("appshell", "Palettes and side panels") },
                    { group: "top", tip: qsTrc("appshell", "Toolbars") },
                    { group: "bottom", tip: qsTrc("appshell", "Mixer, piano and timeline") }
                ]

                Item {
                    id: toggle

                    readonly property bool isOpen: modelData.group === "side" ? panels.sideOpen
                                                  : modelData.group === "top" ? panels.topOpen : panels.bottomOpen

                    width: 30
                    height: 26

                    Rectangle {
                        anchors.fill: parent
                        radius: 4
                        color: area.containsMouse ? ui.theme.buttonColor : "transparent"
                    }

                    // a little window: the strip that folds is filled while it is open
                    Rectangle {
                        id: frame
                        anchors.centerIn: parent
                        width: 16
                        height: 12
                        radius: 2
                        color: "transparent"
                        border.width: 1.2
                        border.color: ui.theme.fontPrimaryColor

                        Rectangle {
                            visible: toggle.isOpen
                            color: ui.theme.fontPrimaryColor
                            x: modelData.group === "side" ? 0 : 0
                            y: modelData.group === "bottom" ? parent.height - height : 0
                            width: modelData.group === "side" ? 5 : parent.width
                            height: modelData.group === "side" ? parent.height : 4
                            radius: 1
                        }
                    }

                    MouseArea {
                        id: area
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: panels.toggle(modelData.group)
                    }

                    ToolTip.visible: area.containsMouse
                    ToolTip.delay: 600
                    ToolTip.text: modelData.tip
                }
            }
        }

        AppSystemButtons {
            id: systemButtons

            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
            Layout.preferredWidth: width
            Layout.preferredHeight: height
            Layout.minimumWidth: width

            windowIsMiximized: root.windowVisibility === Window.Maximized

            visible: root.windowVisibility !== Window.FullScreen && !panels.embedded

            onShowWindowMinimizedRequested: {
                root.showWindowMinimizedRequested()
            }

            onToggleWindowMaximizedRequested: {
                root.toggleWindowMaximizedRequested()
            }

            onCloseWindowRequested: {
                root.closeWindowRequested()
            }
        }
    }

    Item {
        id: titleMoveArea

        x: titleLabel.x
        y: titleLabel.y
        width: titleLabel.visible ? titleLabel.width : 0
        height: titleLabel.visible ? titleLabel.height : 0
    }
}
