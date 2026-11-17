import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Controls.impl

Rectangle {
    color: "#141416"

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        Text {
            Layout.margins: 16
            text: qsTr("Queue")
            color: "white"
            font.pixelSize: 18
            font.bold: true
            textFormat: Text.PlainText
        }
        Text {
            Layout.margins: 16
            visible: list.count === 0
            text: qsTr("Nothing queued")
            color: "#a0a0a8"
            textFormat: Text.PlainText
        }
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: player.queue
            onCountChanged: if (player.queueIndex >= 0) positionViewAtIndex(player.queueIndex, ListView.Contain)

            delegate: Item {
                id: row
                width: ListView.view.width
                height: 52
                z: grab.drag.active ? 2 : 1
                property real dragOffset: 0
                transform: Translate { y: row.dragOffset }

                Rectangle {
                    anchors.fill: parent
                    color: isCurrent ? Qt.rgba(1, 1, 1, 0.08) : rowHover.hovered ? Qt.rgba(1, 1, 1, 0.04) : "transparent"
                }
                HoverHandler { id: rowHover }
                MouseArea {
                    anchors.fill: parent
                    onClicked: player.jump(index)
                }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 8
                    IconImage {
                        // Drag handle: reorders by dropping the row where the pointer ends.
                        Layout.preferredWidth: 20
                        Layout.preferredHeight: 20
                        source: "qrc:/icons/drag_indicator.svg"
                        sourceSize: Qt.size(40, 40)
                        color: "#a0a0a8"
                        MouseArea {
                            id: grab
                            anchors.fill: parent
                            anchors.margins: -8
                            preventStealing: true
                            cursorShape: Qt.SizeVerCursor
                            property real startY: 0
                            onPressed: function(mouse) { startY = mapToItem(list, mouse.x, mouse.y).y }
                            onPositionChanged: function(mouse) {
                                if (pressed) row.dragOffset = mapToItem(list, mouse.x, mouse.y).y - startY
                            }
                            onReleased: {
                                const target = Math.max(0, Math.min(list.count - 1, index + Math.round(row.dragOffset / 52)))
                                row.dragOffset = 0
                                if (target !== index) player.moveTrack(index, target)
                            }
                            onCanceled: row.dragOffset = 0
                        }
                    }
                    RoundedImage {
                        Layout.preferredWidth: 38
                        Layout.preferredHeight: 38
                        source: thumb || ""
                        radius: 4
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text {
                            Layout.fillWidth: true
                            text: title
                            color: isCurrent ? theme.accent : "white"
                            elide: Text.ElideRight
                            textFormat: Text.PlainText
                        }
                        Text {
                            Layout.fillWidth: true
                            text: artist
                            color: "#a0a0a8"
                            font.pixelSize: 12
                            elide: Text.ElideRight
                            textFormat: Text.PlainText
                        }
                    }
                    IconButton {
                        name: "close"
                        size: 18
                        tint: "#a0a0a8"
                        tip: qsTr("Remove")
                        opacity: rowHover.hovered ? 1 : 0
                        enabled: rowHover.hovered
                        onClicked: player.removeAt(index)
                    }
                }
            }
        }
    }
}
