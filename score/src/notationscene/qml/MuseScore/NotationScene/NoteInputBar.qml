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
import MuseScore.NotationScene

Item {
    id: root

    property alias orientation: gridView.orientation

    property bool floating: false

    property int maximumWidth: 0
    property int maximumHeight: 0

    width: gridView.isHorizontal ? childrenRect.width : 76
    height: !gridView.isHorizontal ? childrenRect.height : 40

    property NavigationPanel navigationPanel: NavigationPanel {
        name: "NoteInputBar"
        enabled: root.enabled && root.visible
        accessible.name: qsTrc("notation", "Note input toolbar")
    }

    NoteInputBarModel {
        id: noteInputModel
    }

    QtObject {
        id: prv

        function resolveHorizontalGridViewWidth() {
            if (root.floating) {
                return gridView.contentWidth
            }

            var requiredFreeSpace = gridView.cellWidth * 3 + gridView.rowSpacing * 4

            if (root.maximumWidth - gridView.contentWidth < requiredFreeSpace) {
                return gridView.contentWidth - requiredFreeSpace
            }

            return gridView.contentWidth
        }

        function resolveVerticalGridViewHeight() {
            if (root.floating) {
                return gridView.contentHeight
            }

            var requiredFreeSpace = gridView.cellHeight * 3 + gridView.rowSpacing * 4

            if (root.maximumHeight - gridView.contentHeight < requiredFreeSpace) {
                return gridView.contentHeight - requiredFreeSpace
            }

            return gridView.contentHeight
        }
    }

    GridViewSectional {
        id: gridView

        sectionRole: "section"

        rowSpacing: 4
        columnSpacing: 4

        cellWidth: 32
        cellHeight: cellWidth

        sectionWidth: isHorizontal ? 1 : width
        sectionHeight: isHorizontal ? height : 1

        clip: true

        model: noteInputModel

        sectionDelegate: SeparatorLine {
            required property int itemIndex

            orientation: gridView.isHorizontal ? Qt.Vertical : Qt.Horizontal
            visible: itemIndex !== 0
        }

        itemDelegate: FlatButton {
            id: btn

            required property var itemModel

            readonly property MenuItem item: Boolean(itemModel) ? itemModel.item : null
            readonly property bool hasMenu: Boolean(item) && item.subitems.length !== 0

            width: gridView.cellWidth
            height: gridView.cellWidth

            enabled: noteInputModel.isInputAllowed

            accentButton: (Boolean(item) && item.checked) || menuLoader.isMenuOpened
            transparent: !accentButton

            // Studio: notes, rests, dots, accidentals and articulations drawn with the Laurence font
            readonly property var laurenceGlyphs: ({
                [IconCode.NOTE_WHOLE]: 0xE1D2, [IconCode.NOTE_HALF]: 0xE1D3, [IconCode.NOTE_QUARTER]: 0xE1D5,
                // eighths and shorter: a quarter note with a straight (modern) flag on top, see straightFlags
                [IconCode.NOTE_8TH]: 0xE1D5, [IconCode.NOTE_16TH]: 0xE1D5, [IconCode.NOTE_32ND]: 0xE1D5,
                [IconCode.NOTE_64TH]: 0xE1D5, [IconCode.NOTE_128TH]: 0xE1D5, [IconCode.NOTE_WHOLE_DOUBLE]: 0xE1D0,
                [IconCode.NOTE_DOTTED]: 0xE1E7, [IconCode.REST]: 0xE4E5,
                [IconCode.FLAT]: 0xE260, [IconCode.NATURAL]: 0xE261, [IconCode.SHARP]: 0xE262,
                [IconCode.SHARP_DOUBLE]: 0xE263, [IconCode.FLAT_DOUBLE]: 0xE264,
                [IconCode.ACCENT]: 0xE4A0, [IconCode.STACCATO]: 0xE4A2, [IconCode.TENUTO]: 0xE4A4, [IconCode.MARCATO]: 0xE4AC
            })
            // Studio: straight flags (flag8thUpStraight...), placed at the top of the quarter note's stem
            readonly property var straightFlags: ({
                [IconCode.NOTE_8TH]: 0xF40F, [IconCode.NOTE_16TH]: 0xF412, [IconCode.NOTE_32ND]: 0xF415,
                [IconCode.NOTE_64TH]: 0xF418, [IconCode.NOTE_128TH]: 0xF41B
            })
            readonly property int baseIcon: Boolean(item) ? item.icon : IconCode.NONE
            // notes are tall, accidentals medium, dots and articulations tiny: size each kind to fill the key alike
            readonly property real glyphScale: {
                const g = laurenceGlyphs[baseIcon]
                if (g === 0xE1E7 || g >= 0xE4A0 && g <= 0xE4AC) return 1.6
                if (g >= 0xE260 && g <= 0xE264 || g === 0xE4E5) return 1.05
                if (g === 0xE1D2 || g === 0xE1D0) return 1.25
                return 0.82
            }
            readonly property bool usesLaurence: laurenceGlyphs[baseIcon] !== undefined

            icon: usesLaurence ? IconCode.NONE : baseIcon
            iconFont: ui.theme.toolbarIconsFont

            // a music-font glyph, centred on the key by its real outline (music fonts sit on the staff, not on a text line)
            contentItem: usesLaurence ? laurenceGlyph : null

            Component {
                id: laurenceGlyph

                Item {
                    implicitWidth: btn.width
                    implicitHeight: btn.height

                    TextMetrics {
                        id: nbsp
                        font: glyph.font
                        text: "\u00A0"
                    }

                    TextMetrics {
                        id: tm
                        font: glyph.font
                        text: glyph.text
                    }

                    readonly property bool flagged: btn.straightFlags[btn.baseIcon] !== undefined
                    readonly property real sp: glyph.font.pixelSize / 4 // one staff space (a music font's em is 4 spaces)

                    Text {
                        id: glyph
                        text: String.fromCodePoint(btn.laurenceGlyphs[btn.baseIcon])
                        font.family: "Laurence"
                        font.pixelSize: Math.round(btn.height * btn.glyphScale)
                        color: btn.iconColor
                        // a flagged note is wider on the right: shift it left by half the flag
                        x: (parent.width - tm.tightBoundingRect.width) / 2 - tm.tightBoundingRect.x - (parent.flagged ? parent.sp * 0.5 : 0)
                        y: parent.height / 2 - (tm.tightBoundingRect.y + tm.tightBoundingRect.height / 2) - baselineOffset
                    }

                    Text {
                        visible: parent.flagged
                        // flag glyphs have no advance width and Qt skips a text made of one alone:
                        // a no-break space in front makes it draw (and is taken off x below)
                        text: parent.flagged ? "\u00A0" + String.fromCodePoint(btn.straightFlags[btn.baseIcon]) : ""
                        font: glyph.font
                        color: btn.iconColor
                        // stem: right edge of the notehead (1.005 sp) minus its thickness; top 3.5 sp above the baseline
                        x: glyph.x + parent.sp * (1.005 - 0.12) - nbsp.advanceWidth
                        y: glyph.y + glyph.baselineOffset - parent.sp * 3.5 - baselineOffset
                    }
                }
            }

            toolTipTitle: Boolean(item) ? item.title : ""
            toolTipDescription: Boolean(item) ? item.description : ""
            toolTipShortcut: Boolean(item) ? item.shortcuts : ""

            navigation.panel: root.navigationPanel
            navigation.name: Boolean(item) ? item.id : ""
            navigation.order: Boolean(itemModel) ? itemModel.order : 0
            isClickOnKeyNavTriggered: false
            navigation.onTriggered: {
                if (btn.hasMenu) {
                    toggleMenuOpened()
                } else {
                    handleMenuItem()
                }
            }

            function toggleMenuOpened() {
                menuLoader.toggleOpened(item.subitems)
            }

            function handleMenuItem() {
                Qt.callLater(noteInputModel.handleMenuItem, item.id)
            }

            onClicked: {
                if (btn.hasMenu) {
                    toggleMenuOpened()
                } else {
                    handleMenuItem()
                }
            }

            mouseArea.onPressAndHold: function(event) {
                if (menuLoader.isMenuOpened || !btn.hasMenu) {
                    event.accepted = false // do not suppress the click event
                    return
                }

                btn.toggleMenuOpened()
            }

            StyledMenuLoader {
                id: menuLoader

                onHandleMenuItem: function(itemId) {
                    noteInputModel.handleMenuItem(itemId)
                }
            }
        }
    }

    PopupButton {
        id: customizeButton

        anchors.margins: 4

        width: gridView.cellWidth
        height: gridView.cellHeight

        icon: IconCode.SETTINGS_COG
        iconFont: ui.theme.toolbarIconsFont
        toolTipTitle: qsTrc("notation", "Customize toolbar")
        toolTipDescription: qsTrc("notation", "Show/hide toolbar buttons")
        transparent: true

        enabled: noteInputModel.isInputAllowed

        navigation.panel: root.navigationPanel
        navigation.order: 100
        navigation.accessible.name: qsTrc("notation", "Customize toolbar")

        popupAnchorItem: root.floating ? null : ui.rootItem
        popupComponent: NoteInputBarCustomisePopup {}
    }

    states: [
        State {
            when: gridView.isHorizontal

            PropertyChanges {
                target: gridView
                width: prv.resolveHorizontalGridViewWidth()
                height: root.height
                sectionWidth: 1
                sectionHeight: root.height
                rows: 1
                columns: gridView.noLimit
            }

            AnchorChanges {
                target: customizeButton
                anchors.left: gridView.right
                anchors.verticalCenter: root.verticalCenter
            }
        },
        State {
            when: !gridView.isHorizontal

            PropertyChanges {
                target: gridView
                width: root.width
                height: prv.resolveVerticalGridViewHeight()
                sectionWidth: root.width
                sectionHeight: 1
                rows: gridView.noLimit
                columns: 2
            }

            AnchorChanges {
                target: customizeButton
                anchors.top: gridView.bottom
                anchors.right: parent.right
            }
        }
    ]
}
