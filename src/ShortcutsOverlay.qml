import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Rectangle {
    id: root
    anchors.fill: parent
    color: Qt.rgba(0, 0, 0, 0.8)
    z: 100
    signal dismissed()

    MouseArea { anchors.fill: parent; onClicked: root.dismissed() }

    ColumnLayout {
        anchors.centerIn: parent
        spacing: 6
        Repeater {
            model: [
                ["Space", qsTr("Play / pause")],
                ["Ctrl+Right / Ctrl+Left", qsTr("Next / previous")],
                ["Right / Left", qsTr("Seek ±10 s")],
                ["+ / −", qsTr("Volume")],
                ["1 / 2 / 3", qsTr("Home / Search / Library")],
                ["/  Ctrl+F", qsTr("Search")],
                ["U", qsTr("Queue")],
                ["L", qsTr("Lyrics")],
                ["Esc", qsTr("Back")],
                ["?", qsTr("This help")],
                ["Q", qsTr("Hide window (music continues)")],
                ["Ctrl+Q", qsTr("Quit omawave")]
            ]
            delegate: RowLayout {
                spacing: 32
                Text {
                    Layout.preferredWidth: 220
                    text: modelData[0]
                    color: theme.accent
                    font.pixelSize: 15
                    textFormat: Text.PlainText
                }
                Text {
                    text: modelData[1]
                    color: "white"
                    font.pixelSize: 15
                    textFormat: Text.PlainText
                }
            }
        }
    }
}
