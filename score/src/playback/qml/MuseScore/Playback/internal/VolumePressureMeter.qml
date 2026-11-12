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
 
import QtQuick

import Muse.UiComponents
import Muse.Ui

Canvas {
    id: root

    property real currentVolumePressure: -60.0
    property real minDisplayedVolumePressure: -60.0
    property real maxDisplayedVolumePressure: 0.0

    property bool showRuler: false

    property bool isClipping: currentVolumePressure >= maxDisplayedVolumePressure

    width: root.showRuler ? prv.indicatorWidth + 20 : prv.indicatorWidth
    height: prv.indicatorHeight + (prv.overloadHeight * 2)

    QtObject {
        id: prv

        property var gradient: null
        readonly property int overloadHeight: 4

        readonly property real indicatorHeight: 140
        readonly property real indicatorWidth: 6

        // value ranges
        readonly property int fullValueRangeLength: root.maxDisplayedVolumePressure - root.minDisplayedVolumePressure
        readonly property real divisionPixels: (prv.indicatorHeight - prv.overloadHeight) / fullValueRangeLength

        readonly property real unitsTextWidth: 12
        readonly property color unitTextColor: Utils.colorWithAlpha(ui.theme.fontPrimaryColor, 0.8)
        readonly property string unitTextFont: {
            var pxSize = String('8px')
            var family = String('\'' + ui.theme.bodyFont.family + '\'')

            return pxSize + ' ' + family
        }

        onUnitTextColorChanged: { prv.rulerNeedsPaint = true; root.requestPaint() }
        onUnitTextFontChanged: { prv.rulerNeedsPaint = true; root.requestPaint() }

        // strokes
        readonly property real strokeHorizontalMargin: 2
        readonly property real longStrokeHeight: 1
        readonly property real longStrokeWidth: 5
        readonly property color longStrokeColor: Utils.colorWithAlpha(ui.theme.fontPrimaryColor, 0.5)
        readonly property real shortStrokeHeight: 1
        readonly property real shortStrokeWidth: 2
        readonly property color shortStrokeColor: Utils.colorWithAlpha(ui.theme.fontPrimaryColor, 0.3)

        onLongStrokeColorChanged: { prv.rulerNeedsPaint = true; root.requestPaint() }
        onShortStrokeColorChanged: { prv.rulerNeedsPaint = true; root.requestPaint() }

        property bool rulerNeedsPaint: true
    }

    function drawRuler(ctx, originVPos, originHPos, fullStep, smallStep, strokeHeight, strokeWidth) {
        ctx.clearRect(0, prv.indicatorWidth, root.height, root.width - prv.indicatorWidth)
        ctx.font = prv.unitTextFont

        var currentStrokeVPos = 0

        for (var i = 0; i <= prv.fullValueRangeLength; i+=smallStep) {
            if (i == 0) {
                currentStrokeVPos = originVPos
            } else {
                currentStrokeVPos += prv.divisionPixels * smallStep
            }

            if (i % fullStep) {
                ctx.fillStyle = prv.shortStrokeColor
                ctx.fillRect(currentStrokeVPos,
                             originHPos,
                             prv.shortStrokeHeight,
                             prv.shortStrokeWidth)

            } else {
                ctx.fillStyle = prv.longStrokeColor
                ctx.fillRect(currentStrokeVPos,
                             originHPos,
                             prv.longStrokeHeight,
                             prv.longStrokeWidth)

                let textHPos = originHPos + prv.longStrokeWidth + prv.strokeHorizontalMargin

                ctx.save()

                ctx.rotate(Math.PI/2)
                ctx.fillStyle = prv.unitTextColor
                ctx.fillText(prv.fullValueRangeLength - i, textHPos, -currentStrokeVPos + 2)

                ctx.restore()
            }
        }

        prv.rulerNeedsPaint = false
    }

    onPaint: {
        var ctx = root.context

        if (!ctx) {
            ctx = getContext("2d")
            ctx.translate(0, root.height)
            ctx.rotate(3 * (Math.PI/2))

            if (ui.currentLanguageLayoutDirection() === Qt.RightToLeft) {
                ctx.textAlign = "end"
            } else {
                ctx.textAlign = "start"
            }
        }

        ctx.clearRect(0, 0, root.height, prv.indicatorWidth)

        // Studio: a TE-style LED meter - black display, white segments turning orange near the top
        ctx.fillStyle = "#121214"
        ctx.fillRect(prv.overloadHeight, 0, prv.indicatorHeight, prv.indicatorWidth)

        ctx.fillStyle = root.isClipping ? "#FF3B1F" : "#2A2A2E"
        ctx.fillRect(prv.indicatorHeight, 0, prv.overloadHeight, prv.indicatorWidth)

        if (!prv.gradient) {
            prv.gradient = ctx.createLinearGradient(prv.overloadHeight, 0, prv.indicatorHeight - prv.overloadHeight, prv.indicatorWidth)
            prv.gradient.addColorStop(0.0, "#E8E8E4")
            prv.gradient.addColorStop(0.70, "#E8E8E4")
            prv.gradient.addColorStop(0.85, "#FF6A13")
        }

        ctx.fillStyle = prv.gradient
        ctx.fillRect(prv.overloadHeight, 0, prv.divisionPixels * (prv.fullValueRangeLength - Math.abs(root.currentVolumePressure)), prv.indicatorWidth)

        // segments
        ctx.fillStyle = "#121214"
        for (var seg = prv.overloadHeight + 3; seg < prv.indicatorHeight; seg += 4) {
            ctx.fillRect(seg, 0, 1, prv.indicatorWidth)
        }

        if (prv.rulerNeedsPaint) {
            var originVPos = prv.overloadHeight
            var originHPos = prv.indicatorWidth + prv.strokeHorizontalMargin

            drawRuler(ctx, originVPos, originHPos, 6/*fullStep*/, 3/*smallStep*/)
        }
    }

    onCurrentVolumePressureChanged: {
        requestPaint()
    }

    Component.onCompleted: {
        prv.rulerNeedsPaint = true
        requestPaint()
    }
}
