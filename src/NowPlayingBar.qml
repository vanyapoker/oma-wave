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
    readonly property bool hasTrack: player.queueIndex >= 0
    readonly property bool compact: root.width < 860

    component ThinSlider: Slider {
        id: s
        background: Rectangle {
            x: s.leftPadding
            y: s.topPadding + s.availableHeight / 2 - height / 2
            width: s.availableWidth
            height: 3
            radius: 1.5
            color: Qt.rgba(1, 1, 1, 0.15)
            Rectangle {
                width: s.visualPosition * parent.width
                height: parent.height
                radius: 1.5
                color: theme.accent
            }
        }
        handle: Rectangle {
            x: s.leftPadding + s.visualPosition * (s.availableWidth - width)
            y: s.topPadding + s.availableHeight / 2 - height / 2
            implicitWidth: 12
            implicitHeight: 12
            radius: 6
            color: theme.accent
            visible: s.hovered || s.pressed
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 12
        spacing: 12

        // Left: art (or visualizer bars when there is none), title, rating.
        RowLayout {
            Layout.preferredWidth: root.compact ? Math.round(root.width * 0.38) : 320
            Layout.maximumWidth: root.compact ? Math.round(root.width * 0.38) : 320
            Layout.minimumWidth: 160
            spacing: 12

            Item {
                Layout.preferredWidth: 56
                Layout.preferredHeight: 56
                ToolTip.visible: artHover.hovered && !visualizer.available && !root.track.thumb
                ToolTip.text: qsTr("Visualizer off: cava is not installed")
                HoverHandler { id: artHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: ApplicationWindow.window.openNowPlaying() }
                RoundedImage {
                    anchors.fill: parent
                    source: root.track.thumb || ""
                    radius: 6
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
                Layout.fillWidth: true
                Layout.minimumWidth: 60
                spacing: 0
                Text {
                    Layout.fillWidth: true
                    visible: wave.active
                    elide: Text.ElideRight
                    text: qsTr("My Wave")
                    color: theme.accent
                    font.pixelSize: 10
                    font.bold: true
                    textFormat: Text.PlainText
                }
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

            IconButton {
                visible: !root.compact
                name: "thumb_down"
                filled: wave.rating === "dislike"
                enabled: root.hasTrack
                tip: qsTr("Dislike")
                size: 20
                onClicked: wave.dislike()
            }
            IconButton {
                name: "thumb_up"
                filled: wave.rating === "like"
                tint: wave.rating === "like" ? theme.accent : "white"
                enabled: root.hasTrack
                tip: qsTr("Like")
                size: 20
                onClicked: wave.toggleLike()
            }
        }

        // Centre: transport and seek bar.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 200
            Layout.alignment: Qt.AlignVCenter
            spacing: 0
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 8
                IconButton { name: "skip_previous"; enabled: player.canPrevious; tip: qsTr("Previous"); onClicked: player.previous() }
                Rectangle {
                    Layout.preferredWidth: 40
                    Layout.preferredHeight: 40
                    radius: 20
                    color: theme.accent
                    IconButton {
                        anchors.centerIn: parent
                        width: 40
                        height: 40
                        padding: 0
                        name: player.playing ? "pause" : "play_arrow"
                        filled: true
                        tint: theme.accentForeground
                        onClicked: player.togglePause()
                    }
                }
                IconButton { name: "skip_next"; enabled: player.canNext; tip: qsTr("Next"); onClicked: player.next() }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text { text: ApplicationWindow.window.fmt(player.positionMs); color: "#a0a0a8"; font.pixelSize: 11; textFormat: Text.PlainText }
                ThinSlider {
                    id: seek
                    Layout.fillWidth: true
                    from: 0
                    to: Math.max(1, player.durationMs)
                    onPressedChanged: if (!pressed) player.seek(value)
                    Binding { target: seek; property: "value"; value: player.positionMs; when: !seek.pressed; restoreMode: Binding.RestoreNone }
                }
                Text { text: ApplicationWindow.window.fmt(player.durationMs); color: "#a0a0a8"; font.pixelSize: 11; textFormat: Text.PlainText }
            }
        }

        // Right: volume and panes.
        RowLayout {
            id: right
            property int lastVolume: 70
            Layout.preferredWidth: root.compact ? 128 : 260
            Layout.maximumWidth: root.compact ? 128 : 260
            spacing: 4
            Item { Layout.fillWidth: true }
            IconButton {
                name: player.volume > 0 ? "volume_up" : "volume_off"
                tip: qsTr("Mute")
                onClicked: {
                    if (player.volume > 0) {
                        right.lastVolume = player.volume
                        player.setVolume(0)
                    } else {
                        player.setVolume(right.lastVolume || 70)
                    }
                }
            }
            ThinSlider {
                id: volume
                visible: !root.compact
                Layout.preferredWidth: 100
                from: 0
                to: 100
                onMoved: player.setVolume(Math.round(value))
                Binding { target: volume; property: "value"; value: player.volume; when: !volume.pressed; restoreMode: Binding.RestoreNone }
            }
            IconButton {
                name: "queue_music"
                tint: root.pane === "queue" ? theme.accent : "white"
                tip: qsTr("Queue")
                onClicked: root.togglePane("queue")
            }
            IconButton {
                name: "lyrics"
                tint: root.pane === "lyrics" ? theme.accent : "white"
                tip: qsTr("Lyrics")
                onClicked: root.togglePane("lyrics")
            }
        }
    }
}
