/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
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
import QtQuick.Layouts

import Muse.Ui 
import Muse.UiComponents

/** APIDOC
 * General button
 * @class FlatButton
 * @hideconstructor
 * @example
 * import MuseApi.Controls
 *
 * ExtensionBlank {
 *      FlatButton {
 *          icon: IconCode.STAR
 *          text: "Click me"
 *          onClicked: {
 *              api.interactive.info("Test", "Clicked on button")
 *          }
 *      }
 * }
*/
FocusScope {
    id: root

    /** APIDOC
     * @member {Qml.IconCode}
     */
    property int icon: IconCode.NONE
    /** APIDOC
     * @member {String}
     */
    property string text: ""
    property int textFormat: Text.AutoText
    property int maximumLineCount: 1

    //!NOTE: used to sort buttons inside a button box
    property int buttonId: 0
    property int buttonRole: 0
    property bool isLeftSide: false

    property string toolTipTitle: ""
    property string toolTipDescription: ""
    property string toolTipShortcut: ""
    //!NOTE: Needed to avoid showing a tooltip when tour is shown, see tours provider
    property bool toolTipShowLocked: false

    property font iconFont: ui.theme.iconsFont
    property color iconColor: studioKeys && accentButton ? accentColor : ui.theme.fontPrimaryColor
    property font textFont: ui.theme.bodyFont

    property bool transparent: false
    property bool accentButton: false

    // Studio (TE): buttons are light keys; "on" keys keep the light body with an orange edge and lamp
    property bool studioKeys: true
    property color normalColor:
        transparent && !accentButton ? "transparent" : (accentButton && !studioKeys) ? accentColor : ui.theme.buttonColor
    property color hoverHitColor: (accentButton && !studioKeys) ? accentColor : ui.theme.buttonColor
    property color accentColor: ui.theme.accentColor

    property bool isNarrow: buttonType === FlatButton.Horizontal
    property real margins: isNarrow ? 12 : 16
    property real minWidth: isNarrow ? 24 : 132

    property real backgroundRadius: studioKeys ? 6 : 3

    property bool drawFocusBorderInsideRect: false

    property int orientation: Qt.Vertical
    readonly property bool isVertical: root.orientation === Qt.Vertical

    property alias navigation: navCtrl
    property alias accessible: navCtrl.accessible

    property alias mouseArea: mouseArea

    property bool isClickOnKeyNavTriggered: true

    property Component contentItem: null
    property Component backgroundItem: defaultBackgroundComponent

    enum ButtonType {
        TextOnly,
        IconOnly,
        Horizontal,
        Vertical,
        Custom
    }

    // Can be overridden, for buttons that have a custom content component
    // but should be sized as one of the default types
    property int buttonType: {
        if (contentItem) {
            return FlatButton.Custom
        }

        if (icon !== IconCode.NONE) {
            if (Boolean(text)) {
                return isVertical ? FlatButton.Vertical : FlatButton.Horizontal
            }

            return FlatButton.IconOnly
        }

        return FlatButton.TextOnly
    }

    /** APIDOC
     * @method
     * @signal
     */
    signal clicked(var mouse)
    // The `pressAndHold` signal is intentionally not "forwarded" here from the MouseArea for performance reasons.
    // Most buttons don't use it and Qt has optimizations if no signal is attached. If a component needs it,
    // it can hook to it directly (the mouse area is exposed via the `mouseArea` alias property).

    objectName: root.text

    implicitWidth: contentLoader.itemImplicitWidth + 2 * margins
    implicitHeight: Math.max(contentLoader.itemImplicitHeight, ui.theme.defaultButtonSize)

    opacity: root.enabled ? 1.0 : ui.theme.itemOpacityDisabled

    function doClicked(mouse) {
        Qt.callLater(root.clicked, mouse)
    }

    NavigationControl {
        id: navCtrl
        name: root.objectName !== "" ? root.objectName : "FlatButton"
        enabled: root.enabled && root.visible

        accessible.role: MUAccessible.Button
        accessible.name: Boolean(root.text) ? root.text : root.toolTipTitle
        accessible.description: root.toolTipDescription
        accessible.visualItem: root
        accessible.enabled: navCtrl.enabled

        onTriggered: {
            if (navCtrl.enabled && root.isClickOnKeyNavTriggered) {
                root.doClicked(null)
                navCtrl.notifyAboutControlWasTriggered()
            }
        }
    }

    Loader {
        anchors.fill: parent

        sourceComponent: root.backgroundItem
    }

    Component {
        id: defaultBackgroundComponent

        Item {
            id: keyRoot

            readonly property bool isKey: root.studioKeys && (!root.transparent || root.accentButton || mouseArea.containsMouse)
            readonly property bool down: mouseArea.pressed

            // the key's shadow / edge underneath (TE: keys sit on the body)
            Rectangle {
                visible: keyRoot.isKey
                anchors.fill: parent
                anchors.topMargin: keyRoot.down ? 1 : 2
                anchors.bottomMargin: keyRoot.down ? -1 : -2
                radius: root.backgroundRadius
                color: Qt.darker(ui.theme.backgroundPrimaryColor, 1.28)
            }

            Rectangle {
                id: background

                anchors.fill: parent
                anchors.topMargin: keyRoot.isKey && keyRoot.down ? 1 : 0
                anchors.bottomMargin: keyRoot.isKey && keyRoot.down ? -1 : 0

                color: root.normalColor
                opacity: keyRoot.isKey ? 1.0 : ui.theme.buttonOpacityNormal

                gradient: keyRoot.isKey ? keyGradient : null
                Gradient {
                    id: keyGradient
                    GradientStop { position: 0.0; color: mouseArea.containsMouse && !keyRoot.down ? Qt.lighter(root.normalColor, 1.06) : Qt.lighter(root.normalColor, 1.03) }
                    GradientStop { position: 1.0; color: Qt.darker(root.normalColor, 1.04) }
                }

                radius: root.backgroundRadius
                border.width: root.studioKeys && root.accentButton ? 1.5 : ui.theme.borderWidth
                border.color: root.studioKeys && root.accentButton ? root.accentColor : ui.theme.strokeColor

                NavigationFocusBorder {
                    navigationCtrl: navCtrl
                    drawOutsideParent: !root.drawFocusBorderInsideRect
                }

                // lamp: lit while the key is on
                Rectangle {
                    visible: root.studioKeys && root.accentButton && root.width >= 22 && root.height >= 20
                    width: 5
                    height: 5
                    radius: 2.5
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.margins: 4
                    color: root.accentColor
                }

                states: [
                    State {
                        name: "PRESSED"
                        when: mouseArea.pressed && !keyRoot.isKey

                        PropertyChanges {
                            target: background
                            color: root.hoverHitColor
                            opacity: ui.theme.buttonOpacityHit
                        }
                    },

                    State {
                        name: "HOVERED"
                        when: mouseArea.containsMouse && !mouseArea.pressed && !keyRoot.isKey

                        PropertyChanges {
                            target: background
                            color: root.hoverHitColor
                            opacity: ui.theme.buttonOpacityHover
                        }
                    }
                ]
            }
        }
    }

    Loader {
        id: contentLoader

        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
        anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined

        readonly property real itemImplicitWidth: (item as Item)?.implicitWidth ?? 0
        readonly property real itemImplicitHeight: (item as Item)?.implicitHeight ?? 0

        sourceComponent: root.contentItem ? root.contentItem : defaultContentComponent
        readonly property Component defaultContentComponent: root.isVertical ? verticalContentComponent : horizontalContentComponent
    }

    Component {
        id: verticalContentComponent

        ColumnLayout {
            width: Math.min(implicitWidth, root.width)
            spacing: 4

            StyledIconLabel {
                Layout.alignment: Qt.AlignHCenter
                iconCode: root.icon
                font: root.iconFont
                color: root.iconColor
                visible: !isEmpty && root.buttonType != FlatButton.TextOnly
            }

            StyledTextLabel {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignHCenter
                text: root.text
                font: root.textFont
                textFormat: root.textFormat
                wrapMode: Text.Wrap
                maximumLineCount: root.maximumLineCount
                visible: !isEmpty && root.buttonType != FlatButton.IconOnly
            }
        }
    }

    Component {
        id: horizontalContentComponent

        RowLayout {
            width: Math.min(implicitWidth, root.width)
            spacing: 8

            StyledIconLabel {
                Layout.alignment: Qt.AlignVCenter
                iconCode: root.icon
                font: root.iconFont
                color: root.iconColor
                visible: !isEmpty
            }

            StyledTextLabel {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                text: root.text
                font: root.textFont
                textFormat: root.textFormat
                wrapMode: Text.Wrap
                maximumLineCount: root.maximumLineCount
                visible: !isEmpty
            }
        }
    }

    states: [
        State {
            name: "ICON_ONLY"
            when: root.buttonType === FlatButton.IconOnly

            PropertyChanges {
                target: root
                implicitWidth: ui.theme.defaultButtonSize
                implicitHeight: ui.theme.defaultButtonSize
            }
        },

        State {
            name: "TEXT_ONLY"
            when: root.buttonType === FlatButton.TextOnly

            PropertyChanges {
                target: root
                implicitWidth: Math.max(contentLoader.itemImplicitWidth + 2 * root.margins,
                                        root.minWidth)
                implicitHeight: ui.theme.defaultButtonSize
            }
        },

        State {
            name: "HORIZONTAL"
            when: root.buttonType === FlatButton.Horizontal

            PropertyChanges {
                target: root
                implicitHeight: ui.theme.defaultButtonSize
            }
        },

        State {
            name: "VERTICAL"
            when: root.buttonType === FlatButton.Vertical

            PropertyChanges {
                target: root
                implicitHeight: 48
            }

        }
    ]

    MouseArea {
        id: mouseArea
        anchors.fill: parent

        enabled: root.enabled
        hoverEnabled: true

        onClicked: function(mouse) {
            root.navigation.requestActiveByInteraction()
            root.navigation.notifyAboutControlWasTriggered()

            root.doClicked(mouse)
        }

        onPressed: {
            ui.tooltip.hide(root, true)
        }

        onContainsMouseChanged: {
            if (!Boolean(root.toolTipTitle) || root.toolTipShowLocked) {
                return
            }

            if (mouseArea.containsMouse) {
                ui.tooltip.show(root, root.toolTipTitle, root.toolTipDescription, root.toolTipShortcut)
            } else {
                ui.tooltip.hide(root)
            }
        }
    }
}
