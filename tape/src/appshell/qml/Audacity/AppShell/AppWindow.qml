/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore BVBA and others
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
import QtQuick 2.15
import QtQuick.Controls 2.15

import Muse.Ui 1.0
import Muse.Shortcuts 1.0

import Audacity.AppShell 1.0
import Muse.Toast 1.0

ApplicationWindow {
    id: root

    default property alias windowContent: windowContentItem.data

    objectName: "ApplicationWindow"

    title: titleProvider.title

    width: 1150
    height: 800

    minimumWidth: 500
    minimumHeight: 500

    visible: false

    color: ui.theme.backgroundPrimaryColor

    Component.onCompleted: {
        ui.rootItem = root.contentItem
        titleProvider.load()
    }

    MainWindowTitleProvider {
        id: titleProvider
    }

    MainWindowBridge {
        id: bridge

        window: root

        //! NOTE These properties of QWindow (of which ApplicationWindow is derived)
        //!      are not available in QML, so we access them via MainWindowBridge
        filePath: titleProvider.filePath
        fileModified: titleProvider.fileModified
    }

    ToolTipProvider {}

    //! NOTE Need only create
    Shortcuts {}

    Item {
        id: windowContentItem
        anchors.fill: parent
    }

    WindowDropArea {
        anchors.fill: parent
    }

    // Studio: little arrows at the edges fold the panels away and bring them back
    //   left   the track list, top  the toolbar, right  the right panel
    Item {
        id: edgeArrows
        anchors.fill: parent
        z: 1000

        PanelsModel {
            id: edgePanels
        }

        Component.onCompleted: edgePanels.init()

        component EdgeArrow: Rectangle {
            id: arrow

            property string group: ""
            property bool vertical: false
            property string glyph: ""

            width: vertical ? 14 : 44
            height: vertical ? 44 : 14
            radius: 4
            color: arrowArea.containsMouse ? ui.theme.accentColor : ui.theme.buttonColor
            opacity: arrowArea.containsMouse ? 1.0 : 0.75
            border.width: 1
            border.color: ui.theme.strokeColor

            Text {
                anchors.centerIn: parent
                text: arrow.glyph
                color: arrowArea.containsMouse ? "#FFFFFF" : ui.theme.fontPrimaryColor
                font.pixelSize: 11
            }

            MouseArea {
                id: arrowArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: edgePanels.toggle(arrow.group)
            }
        }

        EdgeArrow {
            group: "side"
            vertical: true
            glyph: edgePanels.sideOpen ? "‹" : "›"
            x: 0
            y: (parent.height - height) / 2
        }

        EdgeArrow {
            group: "top"
            glyph: edgePanels.topOpen ? "˄" : "˅"
            x: (parent.width - width) / 2
            y: 8 // in the middle of the menu bar, which always stays
        }

        EdgeArrow {
            group: "right"
            vertical: true
            glyph: edgePanels.rightOpen ? "›" : "‹"
            x: parent.width - width
            y: (parent.height - height) / 2
        }
    }

    ToastProvider {}

    function showMinimizedWithSavePreviousState() {
        bridge.showMinimizedWithSavePreviousState()
    }
}
