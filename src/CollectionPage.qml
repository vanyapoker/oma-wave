import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Album, playlist or artist page, filled from catalog.detail.
Page {
    id: root
    background: null
    readonly property var detail: catalog.detail
    readonly property var tracks: detail.tracks || []

    ListView {
        id: list
        anchors.fill: parent
        anchors.leftMargin: 20
        anchors.rightMargin: 20
        clip: true
        model: root.tracks

        header: Column {
            width: list.width
            spacing: 16
            bottomPadding: 16

            RowLayout {
                width: parent.width
                spacing: 20
                Rectangle {
                    Layout.preferredWidth: 160
                    Layout.preferredHeight: 160
                    Layout.topMargin: 20
                    color: "#232326"
                    radius: root.detail.kind === "artist" ? 80 : Math.min(4, theme.radius)
                    clip: true
                    Image {
                        anchors.fill: parent
                        source: root.detail.thumb || ""
                        asynchronous: true
                        fillMode: Image.PreserveAspectCrop
                        sourceSize.width: 320
                        sourceSize.height: 320
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignBottom
                    spacing: 6
                    Text {
                        Layout.fillWidth: true
                        text: root.detail.title || ""
                        color: "white"
                        font.pixelSize: 28
                        font.bold: true
                        wrapMode: Text.Wrap
                        textFormat: Text.PlainText
                    }
                    Text {
                        Layout.fillWidth: true
                        text: root.detail.subtitle || ""
                        color: "#a0a0a8"
                        wrapMode: Text.Wrap
                        textFormat: Text.PlainText
                    }
                    Text {
                        text: qsTr("%n songs", "", root.tracks.length)
                        color: "#a0a0a8"
                        visible: root.tracks.length > 0
                        textFormat: Text.PlainText
                    }
                    Button {
                        text: qsTr("Play all")
                        highlighted: true
                        enabled: root.tracks.length > 0
                        onClicked: player.playTracks(root.tracks, 0, false)
                    }
                }
            }

            CardRow { width: parent.width; title: qsTr("Albums"); items: root.detail.albums }
            CardRow { width: parent.width; title: qsTr("Singles"); items: root.detail.singles }
        }

        delegate: TrackRow {
            width: ListView.view.width
            track: modelData
            tracks: root.tracks
            position: index
        }

        BusyIndicator {
            anchors.centerIn: parent
            running: !!root.detail.loading
        }
    }
}
