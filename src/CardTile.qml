import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// A catalogue item (song, album, playlist or artist) as a clickable card.
ItemDelegate {
    id: root
    property var item: ({})
    width: 160
    height: 214
    padding: 0

    contentItem: Column {
        spacing: 6
        Rectangle {
            width: 160
            height: 160
            color: "#232326"
            radius: root.item.kind === "artist" ? 80 : Math.min(4, theme.radius)
            clip: true
            Image {
                anchors.fill: parent
                source: root.item.thumb || ""
                asynchronous: true
                fillMode: Image.PreserveAspectCrop
                sourceSize.width: 320
                sourceSize.height: 320
            }
        }
        Text {
            width: 160
            text: root.item.title || ""
            color: "white"
            elide: Text.ElideRight
            font.pixelSize: 13
            font.bold: true
            textFormat: Text.PlainText
        }
        Text {
            width: 160
            text: root.item.subtitle || ""
            color: "#a0a0a8"
            elide: Text.ElideRight
            font.pixelSize: 12
            textFormat: Text.PlainText
        }
    }

    onClicked: {
        if (item.kind === "song") {
            const track = { videoId: item.id, title: item.title, artists: [item.subtitle], artist: item.subtitle,
                            album: "", thumb: item.thumb, durationMs: 0 }
            player.playTracks([track], 0, true)
        } else {
            ApplicationWindow.window.openCollection(item.kind, item.id)
        }
    }
}
