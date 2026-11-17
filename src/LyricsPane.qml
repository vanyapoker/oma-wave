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
        LyricsView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: lyrics.state === "synced" || lyrics.state === "plain"
        }
    }
}
