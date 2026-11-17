import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// One song in a list. A click plays the song (alone with radio, or as part of its list).
ItemDelegate {
    id: root
    property var track: ({})
    property var tracks: []
    property int position: 0
    property bool radio: false
    height: 52
    padding: 0
    leftPadding: 8
    rightPadding: 8

    contentItem: RowLayout {
        spacing: 12
        Rectangle {
            Layout.preferredWidth: 38
            Layout.preferredHeight: 38
            color: "#232326"
            radius: Math.min(4, theme.radius)
            clip: true
            Image {
                anchors.fill: parent
                source: root.track.thumb || ""
                asynchronous: true
                fillMode: Image.PreserveAspectCrop
                sourceSize.width: 76
                sourceSize.height: 76
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0
            Text {
                Layout.fillWidth: true
                text: root.track.title || ""
                color: "white"
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
        Text {
            text: root.track.durationMs > 0 ? ApplicationWindow.window.fmt(root.track.durationMs) : ""
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
