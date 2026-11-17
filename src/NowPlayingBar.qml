import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Rectangle {
    id: root
    color: "#141416"
    property string pane: ""
    signal togglePane(string name)
    readonly property var track: player.track

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 12
        spacing: 12

        // Art, or the visualizer bars when the track has none.
        Rectangle {
            Layout.preferredWidth: 56
            Layout.preferredHeight: 56
            color: "#232326"
            radius: Math.min(4, theme.radius)
            clip: true
            ToolTip.visible: artHover.hovered && !visualizer.available
            ToolTip.text: qsTr("Visualizer off: cava is not installed")
            HoverHandler { id: artHover }
            Image {
                anchors.fill: parent
                source: root.track.thumb || ""
                asynchronous: true
                fillMode: Image.PreserveAspectCrop
                sourceSize.width: 112
                sourceSize.height: 112
            }
            Row {
                anchors.centerIn: parent
                visible: !root.track.thumb
                spacing: 3
                Repeater {
                    model: visualizer.levels
                    delegate: Rectangle {
                        width: 4
                        height: 4 + modelData / 100 * 32
                        anchors.bottom: parent.bottom
                        color: theme.accent
                    }
                }
            }
        }

        ColumnLayout {
            Layout.preferredWidth: 220
            spacing: 0
            Text {
                Layout.fillWidth: true
                text: root.track.title || qsTr("Nothing playing")
                color: "white"
                font.bold: true
                elide: Text.ElideRight
                textFormat: Text.PlainText
            }
            Text {
                Layout.fillWidth: true
                text: root.track.artist || ""
                color: "#a0a0a8"
                font.pixelSize: 12
                elide: Text.ElideRight
                textFormat: Text.PlainText
            }
        }

        RowLayout {
            spacing: 0
            ToolButton { text: "\u23EE"; enabled: player.canPrevious; onClicked: player.previous() }
            ToolButton {
                text: player.playing ? "\u23F8" : "\u25B6"
                font.pixelSize: 20
                onClicked: player.togglePause()
            }
            ToolButton { text: "\u23ED"; enabled: player.canNext; onClicked: player.next() }
        }

        Text { text: ApplicationWindow.window.fmt(player.positionMs); color: "#a0a0a8"; textFormat: Text.PlainText }
        Slider {
            id: seek
            Layout.fillWidth: true
            from: 0
            to: Math.max(1, player.durationMs)
            onPressedChanged: if (!pressed) player.seek(value)
            Binding { target: seek; property: "value"; value: player.positionMs; when: !seek.pressed; restoreMode: Binding.RestoreNone }
        }
        Text { text: ApplicationWindow.window.fmt(player.durationMs); color: "#a0a0a8"; textFormat: Text.PlainText }

        Slider {
            id: volume
            Layout.preferredWidth: 90
            from: 0
            to: 100
            onMoved: player.setVolume(Math.round(value))
            Binding { target: volume; property: "value"; value: player.volume; when: !volume.pressed; restoreMode: Binding.RestoreNone }
        }
        Button { text: qsTr("Queue"); flat: true; checkable: true; checked: root.pane === "queue"; onClicked: root.togglePane("queue") }
        Button { text: qsTr("Lyrics"); flat: true; checkable: true; checked: root.pane === "lyrics"; onClicked: root.togglePane("lyrics") }
    }
}
