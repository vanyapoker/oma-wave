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
        anchors.margins: 12
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
        }
        GridView {
            clip: true
            cellWidth: 172
            cellHeight: 226
            model: root.lib.playlists || []
            delegate: CardTile { item: modelData }
        }
        GridView {
            clip: true
            cellWidth: 172
            cellHeight: 226
            model: root.lib.albums || []
            delegate: CardTile { item: modelData }
        }
        GridView {
            clip: true
            cellWidth: 172
            cellHeight: 226
            model: root.lib.artists || []
            delegate: CardTile { item: modelData }
        }
    }
}
