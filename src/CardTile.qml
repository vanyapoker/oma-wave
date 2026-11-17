import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// A catalogue item (song, album, playlist or artist) as a clickable card.
ItemDelegate {
    id: root
    property var item: ({})
    property int size: 160
    readonly property bool circle: item.kind === "artist"
    width: size
    height: size + 54
    padding: 0
    background: null

    HoverHandler { id: hover }

    function play() {
        if (item.kind === "song") {
            const track = { videoId: item.id, title: item.title, artists: [item.subtitle], artist: item.subtitle,
                            album: "", thumb: item.thumb, durationMs: 0 }
            player.playTracks([track], 0, true)
        } else {
            player.playCollection(item.kind, item.id)
        }
    }

    contentItem: Column {
        spacing: 6
        Item {
            width: root.size
            height: root.size
            RoundedImage {
                anchors.fill: parent
                source: root.item.thumb || ""
                radius: 8
                circle: root.circle
            }
            Rectangle {
                anchors.fill: parent
                radius: root.circle ? width / 2 : 8
                color: Qt.rgba(0, 0, 0, 0.35)
                visible: hover.hovered
            }
            Rectangle {
                visible: hover.hovered
                width: 44
                height: 44
                radius: 22
                color: theme.accent
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 8
                IconButton {
                    anchors.centerIn: parent
                    width: 44
                    height: 44
                    padding: 0
                    name: "play_arrow"
                    filled: true
                    tint: theme.accentForeground
                    onClicked: root.play()
                }
            }
        }
        Text {
            width: root.size
            text: root.item.title || ""
            color: hover.hovered ? theme.accent : "white"
            elide: Text.ElideRight
            font.pixelSize: 13
            font.bold: true
            textFormat: Text.PlainText
        }
        Text {
            width: root.size
            text: root.item.subtitle || ""
            color: "#a0a0a8"
            elide: Text.ElideRight
            font.pixelSize: 12
            textFormat: Text.PlainText
        }
    }

    onClicked: {
        if (item.kind === "song")
            play()
        else
            ApplicationWindow.window.openCollection(item.kind, item.id)
    }
}
