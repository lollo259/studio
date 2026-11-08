/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2021-2024 MuseScore Limited and others
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

import Muse.Ui
import Muse.UiComponents

import MuseScore.AppShell

StyledDialogView {
    id: root

    title: qsTrc("appshell/about", "About Muse")

    contentHeight: content.implicitHeight
    contentWidth: 400

    margins: 40

    AboutModel {
        id: aboutModel
    }

    ColumnLayout {
        id: content

        anchors.fill: parent
        spacing: 32

        Image {
            id: logo
            Layout.alignment: Qt.AlignHCenter

            source: "resources/mu_logo.svg"
            sourceSize: Qt.size(100, 100)

            MouseArea {
                anchors.fill: parent

                property int clickCount: 0

                onClicked: {
                    clickCount++

                    if (clickCount % 3 == 0) {
                        aboutModel.toggleDevMode()
                    }
                }
            }
        }

        Column {
            spacing: 8
            Layout.fillWidth: true

            StyledTextLabel {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTrc("appshell/about", "Version: %1").arg(aboutModel.museScoreVersion())
                font: ui.theme.bodyBoldFont
            }

            Row {
                spacing: 4
                anchors.horizontalCenter: parent.horizontalCenter

                StyledTextLabel {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTrc("appshell/about", "Revision: %1").arg(aboutModel.museScoreRevisionLink())
                }

                FlatButton {
                    anchors.verticalCenter: parent.verticalCenter
                    icon: IconCode.COPY

                    onClicked: {
                        aboutModel.copyRevisionToClipboard()
                    }
                }
            }
        }

        StyledTextLabel {
            Layout.fillWidth: true

            text: qsTrc("appshell/about", "Build date: %1")
                  .arg(aboutModel.museScoreBuildDateTime())

            wrapMode: Text.WordWrap
            maximumLineCount: 2
        }

        StyledTextLabel {
            Layout.fillWidth: true
            text: qsTrc("appshell/about", "Score by Laurence Who. Amusing scores. Based on <a href=\"%1\">MuseScore Studio</a>. Score is an independent version and is not affiliated with or endorsed by MuseScore Limited.")
                  .arg("https://github.com/musescore/MuseScore")
            wrapMode: Text.WordWrap
            maximumLineCount: 3
        }

        StyledTextLabel {
            Layout.fillWidth: true
            text: qsTrc("appshell/about", "Copyright © 1999-2026 MuseScore Limited and others.\nPublished under the <a href=\"%1\">GNU General Public License version 3</a>.")
                  .arg("https://www.gnu.org/licenses/gpl-3.0.html")
                  .replace("\n", "<br>")

            wrapMode: Text.WordWrap
            maximumLineCount: 3
        }
    }
}
