import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Page {
    id: root
    background: null
    signal signInRequested()
    readonly property var lib: catalog.library

    Component.onCompleted: if (catalog.signedIn) catalog.loadLibrary()

    header: ColumnLayout {
        spacing: 0
        Text {
            Layout.margins: 20
            Layout.bottomMargin: 8
            text: qsTr("Library")
            color: "white"
            font.pixelSize: 24
            font.bold: true
            textFormat: Text.PlainText
        }
        TabBar {
            id: tabs
            Layout.fillWidth: true
            visible: catalog.signedIn
            background: null
            TabButton { text: qsTr("Liked") }
            TabButton { text: qsTr("Playlists") }
            TabButton { text: qsTr("Albums") }
            TabButton { text: qsTr("Artists") }
        }
    }

    ColumnLayout {
        anchors.centerIn: parent
        visible: !catalog.signedIn
        spacing: 12
        Text {
            text: qsTr("Sign in to see your library")
            color: "#a0a0a8"
            Layout.alignment: Qt.AlignHCenter
            textFormat: Text.PlainText
        }
        Button {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Sign in")
            highlighted: true
            onClicked: root.signInRequested()
        }
    }

    StackLayout {
        anchors.fill: parent
        anchors.leftMargin: 20
        anchors.rightMargin: 20
        anchors.topMargin: 12
        visible: catalog.signedIn
        currentIndex: tabs.currentIndex

        ListView {
            clip: true
            model: root.lib.liked || []
            delegate: TrackRow {
                width: ListView.view.width
                track: modelData
                tracks: root.lib.liked
                position: index
            }
            Text {
                anchors.centerIn: parent
                visible: parent.count === 0 && !catalog.busy
                text: qsTr("No liked songs yet")
                color: "#a0a0a8"
                textFormat: Text.PlainText
            }
        }
        GridView {
            clip: true
            cellWidth: Math.floor(width / Math.max(1, Math.floor(width / 172)))
            cellHeight: 226
            model: root.lib.playlists || []
            delegate: CardTile { item: modelData }
            Text {
                anchors.centerIn: parent
                visible: parent.count === 0 && !catalog.busy
                text: qsTr("No playlists")
                color: "#a0a0a8"
                textFormat: Text.PlainText
            }
        }
        GridView {
            clip: true
            cellWidth: Math.floor(width / Math.max(1, Math.floor(width / 172)))
            cellHeight: 226
            model: root.lib.albums || []
            delegate: CardTile { item: modelData }
            Text {
                anchors.centerIn: parent
                visible: parent.count === 0 && !catalog.busy
                text: qsTr("No saved albums")
                color: "#a0a0a8"
                textFormat: Text.PlainText
            }
        }
        GridView {
            clip: true
            cellWidth: Math.floor(width / Math.max(1, Math.floor(width / 172)))
            cellHeight: 226
            model: root.lib.artists || []
            delegate: CardTile { item: modelData }
            Text {
                anchors.centerIn: parent
                visible: parent.count === 0 && !catalog.busy
                text: qsTr("No subscriptions")
                color: "#a0a0a8"
                textFormat: Text.PlainText
            }
        }
    }
}
