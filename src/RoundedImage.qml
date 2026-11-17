import QtQuick

// Remote art clipped to rounded corners (or a circle), over a placeholder.
Item {
    id: root
    property url source
    property real radius: 8
    property bool circle: false
    readonly property real corner: circle ? Math.min(width, height) / 2 : radius

    Rectangle {
        anchors.fill: parent
        color: "#232326"
        radius: root.corner
    }
    Image {
        id: img
        visible: false
        source: root.source
        asynchronous: true
        sourceSize.width: Math.max(1, Math.round(root.width * 2))
        sourceSize.height: Math.max(1, Math.round(root.height * 2))
        onStatusChanged: canvas.requestPaint()
    }
    Canvas {
        id: canvas
        anchors.fill: parent
        property url loadedSource
        function updateSource() {
            if (loadedSource) unloadImage(loadedSource)
            loadedSource = root.source
            if (loadedSource) loadImage(loadedSource, Qt.size(Math.max(1, Math.round(root.width * 2)), Math.max(1, Math.round(root.height * 2))))
            requestPaint()
        }
        onImageLoaded: requestPaint()
        Component.onCompleted: updateSource()
        onWidthChanged: updateSource()
        onHeightChanged: updateSource()
        Connections {
            target: root
            function onSourceChanged() { canvas.updateSource() }
            function onCornerChanged() { canvas.requestPaint() }
        }
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            if (!loadedSource || !isImageLoaded(loadedSource) || img.status !== Image.Ready || width <= 0 || height <= 0) return
            const size = img.sourceSize
            if (!size.width || !size.height) return
            const scale = Math.max(width / size.width, height / size.height)
            const w = size.width * scale, h = size.height * scale
            const r = Math.min(root.corner, width / 2, height / 2)
            ctx.beginPath()
            ctx.moveTo(r, 0)
            ctx.lineTo(width - r, 0)
            ctx.quadraticCurveTo(width, 0, width, r)
            ctx.lineTo(width, height - r)
            ctx.quadraticCurveTo(width, height, width - r, height)
            ctx.lineTo(r, height)
            ctx.quadraticCurveTo(0, height, 0, height - r)
            ctx.lineTo(0, r)
            ctx.quadraticCurveTo(0, 0, r, 0)
            ctx.closePath()
            ctx.clip()
            ctx.drawImage(img, (width - w) / 2, (height - h) / 2, w, h)
        }
    }
}
