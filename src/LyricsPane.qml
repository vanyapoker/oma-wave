import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Rectangle {
    color: "#141416"

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        Text {
            Layout.margins: 16
            text: qsTr("Lyrics")
            color: "white"
            font.pixelSize: 18
            font.bold: true
            textFormat: Text.PlainText
        }
        Text {
            Layout.margins: 16
            Layout.fillWidth: true
            visible: lyrics.state !== "synced" && lyrics.state !== "plain"
            wrapMode: Text.Wrap
            color: "#a0a0a8"
            textFormat: Text.PlainText
            text: lyrics.state === "loading" ? qsTr("Loading lyrics…")
                : lyrics.state === "missing" ? qsTr("No lyrics found")
                : lyrics.state === "error" ? qsTr("Could not load lyrics")
                : qsTr("Nothing playing")
        }
        ListView {
            id: view
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: lyrics.state === "synced" || lyrics.state === "plain"
            clip: true
            model: lyrics.lines
            delegate: Text {
                readonly property bool active: lyrics.state === "synced" && index === lyrics.currentIndex
                width: view.width - 32
                x: 16
                topPadding: 6
                bottomPadding: 6
                wrapMode: Text.Wrap
                horizontalAlignment: lyrics.state === "synced" ? Text.AlignHCenter : Text.AlignLeft
                text: modelData.text
                color: active ? theme.accent : lyrics.state === "synced" ? "#8a8a92" : "white"
                font.pixelSize: active ? 18 : 15
                font.bold: active
                textFormat: Text.PlainText
            }
            Connections {
                target: lyrics
                function onCurrentIndexChanged() {
                    if (lyrics.currentIndex >= 0)
                        view.positionViewAtIndex(lyrics.currentIndex, ListView.Center)
                }
            }
        }
    }
}
