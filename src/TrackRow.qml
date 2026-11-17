import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import "Format.js" as Format
import QtQuick.Layouts

// One song in a list. A click plays the song (alone with radio, or as part of its list).
ItemDelegate {
    id: root
    property var track: ({})
    property var tracks: []
    property int position: 0
    property bool radio: false
    readonly property bool current: !!root.track.videoId && root.track.videoId === player.track.videoId
    height: 52
    padding: 0
    leftPadding: 8
    rightPadding: 8
    hoverEnabled: true

    background: Rectangle {
        radius: 6
        color: root.hovered ? Qt.rgba(1, 1, 1, 0.05) : "transparent"
    }

    contentItem: RowLayout {
        spacing: 12
        Item {
            Layout.preferredWidth: 38
            Layout.preferredHeight: 38
            RoundedImage {
                anchors.fill: parent
                source: root.track.thumb || ""
                radius: 4
            }
            Rectangle {
                anchors.fill: parent
                radius: 4
                color: Qt.rgba(0, 0, 0, 0.55)
                visible: root.hovered
                IconButton {
                    anchors.centerIn: parent
                    width: 38
                    height: 38
                    padding: 0
                    enabled: false
                    name: "play_arrow"
                    filled: true
                    size: 22
                    icon.color: "white"
                }
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0
            Text {
                Layout.fillWidth: true
                text: root.track.title || ""
                color: root.current ? theme.accent : "white"
                elide: Text.ElideRight
                textFormat: Text.PlainText
            }
            Text {
                Layout.fillWidth: true
                text: (root.track.artists || []).join(", ")
                color: "#a0a0a8"
                font.pixelSize: 12
                elide: Text.ElideRight
                textFormat: Text.PlainText
            }
        }
        Text {
            Layout.fillWidth: true
            Layout.maximumWidth: 200
            text: root.track.album || ""
            color: "#a0a0a8"
            font.pixelSize: 12
            elide: Text.ElideRight
            visible: root.width > 640
            textFormat: Text.PlainText
        }
        IconButton {
            visible: root.current
            Layout.preferredWidth: 36
            Layout.preferredHeight: 36
            padding: 0
            name: "thumb_up"
            filled: wave.rating === "like"
            tint: wave.rating === "like" ? theme.accent : "#a0a0a8"
            size: 20
            tip: qsTr("Like")
            onClicked: wave.toggleLike()
        }
        IconButton {
            id: more
            Layout.preferredWidth: 36
            Layout.preferredHeight: 36
            padding: 0
            name: "more_vert"
            size: 20
            tint: "#a0a0a8"
            tip: qsTr("More")
            opacity: root.hovered || menu.opened ? 1 : 0
            onClicked: menu.popup(more, 0, more.height)
        }
        Text {
            text: root.track.durationMs > 0 ? Format.time(root.track.durationMs) : ""
            color: "#a0a0a8"
            textFormat: Text.PlainText
        }
    }

    onClicked: {
        if (radio)
            player.playTracks([track], 0, true)
        else
            player.playTracks(tracks, position, false)
    }

    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: menu.popup()
    }

    Menu {
        id: menu
        MenuItem { text: qsTr("Play next"); onTriggered: player.playNext(root.track) }
        MenuItem { text: qsTr("Add to queue"); onTriggered: player.enqueue(root.track) }
        MenuItem {
            text: qsTr("Go to album")
            enabled: !!root.track.albumId
            onTriggered: ApplicationWindow.window.openCollection("album", root.track.albumId)
        }
    }
}
