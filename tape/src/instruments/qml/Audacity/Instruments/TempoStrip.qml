/*
 * Tape (Laurence Who Studio) — the score's tempo under the timeline.
 * A step line with the tempo written at each change; drag a point up/down to change it in Score.
 */
import QtQuick

Item {
    id: root

    property var context: null   // the timeline context: timeToPosition()
    property int dragIndex: -1
    property real dragBpm: 0

    visible: model.hasTempo
    clip: true

    TempoStripModel {
        id: model
        onPointsChanged: canvas.requestPaint()
    }

    function bpmRange() {
        let lo = 1e9, hi = 0
        for (let p of model.points) {
            lo = Math.min(lo, p.bpm); hi = Math.max(hi, p.bpm)
        }
        if (root.dragIndex >= 0) {
            lo = Math.min(lo, root.dragBpm); hi = Math.max(hi, root.dragBpm)
        }
        if (hi - lo < 20) {
            const mid = (hi + lo) / 2
            lo = mid - 10; hi = mid + 10
        }
        return [lo, hi]
    }

    function yOf(bpm) {
        const r = bpmRange()
        return 3 + (1 - (bpm - r[0]) / (r[1] - r[0])) * (height - 6)
    }

    function xOf(t) {
        return root.context ? root.context.timeToPosition(t) : 0
    }

    Rectangle {
        anchors.fill: parent
        color: "#141414"
        opacity: 0.55
    }

    Canvas {
        id: canvas
        anchors.fill: parent

        Connections {
            target: root.context
            ignoreUnknownSignals: true
            function onFrameStartTimeChanged() { canvas.requestPaint() }
            function onFrameEndTimeChanged() { canvas.requestPaint() }
            function onZoomChanged() { canvas.requestPaint() }
        }

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const pts = model.points
            if (!pts.length) {
                return
            }
            ctx.strokeStyle = "#ff5a1f"
            ctx.lineWidth = 1.5
            ctx.font = "9px monospace"
            ctx.fillStyle = "#ffffff"
            ctx.beginPath()
            for (let i = 0; i < pts.length; ++i) {
                const bpm = i === root.dragIndex ? root.dragBpm : pts[i].bpm
                const x = root.xOf(pts[i].t)
                const xNext = i + 1 < pts.length ? root.xOf(pts[i + 1].t) : width
                const y = root.yOf(bpm)
                if (i === 0) {
                    ctx.moveTo(Math.max(0, x), y)
                } else {
                    ctx.lineTo(x, y)
                }
                ctx.lineTo(xNext, y)
            }
            ctx.stroke()
            for (let i = 0; i < pts.length; ++i) {
                const bpm = i === root.dragIndex ? root.dragBpm : pts[i].bpm
                const x = root.xOf(pts[i].t)
                if (x < -40 || x > width + 4) {
                    continue
                }
                const y = root.yOf(bpm)
                ctx.fillStyle = "#ff5a1f"
                ctx.fillRect(x - 3, y - 3, 6, 6)
                ctx.fillStyle = "#ffffff"
                ctx.fillText("♩" + Math.round(bpm), x + 5, Math.min(height - 2, Math.max(9, y + 3)))
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        property real pressY: 0
        property real pressBpm: 0

        function nearest(mx) {
            let best = -1, bestD = 8
            for (let i = 0; i < model.points.length; ++i) {
                const d = Math.abs(root.xOf(model.points[i].t) - mx)
                if (d < bestD) {
                    bestD = d; best = i
                }
            }
            return best
        }

        cursorShape: nearest(mouseX) >= 0 ? Qt.SizeVerCursor : Qt.ArrowCursor

        onPressed: function(mouse) {
            const i = nearest(mouse.x)
            if (i < 0) {
                mouse.accepted = false
                return
            }
            root.dragIndex = i
            pressY = mouse.y
            pressBpm = model.points[i].bpm
            root.dragBpm = pressBpm
        }
        onPositionChanged: function(mouse) {
            if (root.dragIndex < 0) {
                return
            }
            root.dragBpm = Math.max(20, Math.min(400, Math.round(pressBpm + (pressY - mouse.y) / 2)))
            canvas.requestPaint()
        }
        onReleased: {
            if (root.dragIndex >= 0) {
                const p = model.points[root.dragIndex]
                if (Math.round(root.dragBpm) !== Math.round(p.bpm)) {
                    model.setTempo(p.tick, root.dragBpm)
                }
            }
            root.dragIndex = -1
            canvas.requestPaint()
        }
    }
}
