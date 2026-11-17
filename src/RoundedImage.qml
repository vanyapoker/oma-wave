import QtQuick
import QtQuick.Effects

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
        anchors.fill: parent
        visible: false
        layer.enabled: true
        source: root.source
        asynchronous: true
        fillMode: Image.PreserveAspectCrop
        sourceSize.width: Math.max(1, Math.round(root.width * 2))
        sourceSize.height: Math.max(1, Math.round(root.height * 2))
    }
    Rectangle {
        id: mask
        anchors.fill: parent
        visible: false
        layer.enabled: true
        radius: root.corner
    }
    MultiEffect {
        anchors.fill: parent
        source: img
        visible: img.status === Image.Ready
        maskEnabled: true
        maskSource: mask
        maskThresholdMin: 0.5
        maskSpreadAtMin: 1.0
    }
}
