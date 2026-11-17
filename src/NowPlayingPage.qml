import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Full-window view of the current track: large art and lyrics.
Page {
    id: root
    property bool isNowPlaying: true
    readonly property var track: player.track
    background: null

    Image {
        id: backdrop
        anchors.fill: parent
        opacity: 0.15
        source: root.track.thumb || ""
        asynchronous: true
        fillMode: Image.PreserveAspectCrop
        sourceSize.width: 24
        sourceSize.height: 24
        smooth: true
    }

    IconButton {
        x: 8
        y: 8
        z: 2
        name: "arrow_back"
        tip: qsTr("Back")
        onClicked: root.StackView.view.pop()
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 32
        anchors.topMargin: 56
        spacing: 32

        ColumnLayout {
            id: artColumn
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 1
            spacing: 12
            Item { Layout.fillHeight: true }
            RoundedImage {
                readonly property int edge: Math.max(120, Math.min(420, root.height - 160, artColumn.width))
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: edge
                Layout.preferredHeight: edge
                source: root.track.thumb || ""
                radius: 12
            }
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: root.track.title || qsTr("Nothing playing")
                color: "white"
                font.pixelSize: 26
                font.bold: true
                elide: Text.ElideRight
                textFormat: Text.PlainText
            }
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: root.track.artist || ""
                color: "#a0a0a8"
                font.pixelSize: 15
                elide: Text.ElideRight
                textFormat: Text.PlainText
            }
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                IconButton {
                    name: "thumb_down"
                    filled: wave.rating === "dislike"
                    enabled: player.queueIndex >= 0
                    tip: qsTr("Dislike")
                    onClicked: wave.dislike()
                }
                IconButton {
                    name: "thumb_up"
                    filled: wave.rating === "like"
                    tint: wave.rating === "like" ? theme.accent : "white"
                    enabled: player.queueIndex >= 0
                    tip: qsTr("Like")
                    onClicked: wave.toggleLike()
                }
            }
            Item { Layout.fillHeight: true }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 1
            Text {
                anchors.centerIn: parent
                width: parent.width - 32
                horizontalAlignment: Text.AlignHCenter
                visible: lyrics.state !== "synced" && lyrics.state !== "plain"
                wrapMode: Text.Wrap
                color: "#a0a0a8"
                font.pixelSize: 16
                textFormat: Text.PlainText
                text: lyrics.state === "loading" ? qsTr("Loading lyrics…")
                    : lyrics.state === "missing" ? qsTr("No lyrics found")
                    : lyrics.state === "error" ? qsTr("Could not load lyrics")
                    : qsTr("Nothing playing")
            }
            LyricsView {
                anchors.fill: parent
                fontSize: 20
                visible: lyrics.state === "synced" || lyrics.state === "plain"
            }
        }
    }
}
