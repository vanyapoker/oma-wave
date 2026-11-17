import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Album, playlist or artist page, filled from catalog.detail.
Page {
    id: root
    background: null
    header: Item {
        height: 48
        IconButton {
            x: 8
            anchors.verticalCenter: parent.verticalCenter
            name: "arrow_back"
            tip: qsTr("Back")
            onClicked: ApplicationWindow.window.goBack()
        }
    }
    readonly property var detail: catalog.detail
    readonly property var tracks: detail.tracks || []
    function shuffled() {
        const list = root.tracks.slice()
        for (let i = list.length - 1; i > 0; i--) {
            const j = Math.floor(Math.random() * (i + 1))
            const t = list[i]; list[i] = list[j]; list[j] = t
        }
        return list
    }

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
                RoundedImage {
                    Layout.preferredWidth: 160
                    Layout.preferredHeight: 160
                    source: root.detail.thumb || ""
                    radius: 8
                    circle: root.detail.kind === "artist"
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
                    RowLayout {
                        spacing: 12
                        Rectangle {
                            Layout.preferredWidth: 48
                            Layout.preferredHeight: 48
                            radius: 24
                            color: root.tracks.length > 0 ? theme.accent : Qt.rgba(1, 1, 1, 0.15)
                            IconButton {
                                anchors.centerIn: parent
                                width: 48
                                height: 48
                                padding: 0
                                name: "play_arrow"
                                filled: true
                                size: 28
                                tint: theme.accentForeground
                                enabled: root.tracks.length > 0
                                tip: qsTr("Play all")
                                onClicked: player.playTracks(root.tracks, 0, false)
                            }
                        }
                        IconButton {
                            name: "shuffle"
                            size: 24
                            enabled: root.tracks.length > 1
                            tip: qsTr("Shuffle")
                            onClicked: player.playTracks(root.shuffled(), 0, false)
                        }
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
