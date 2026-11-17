import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Rectangle {
    id: root
    color: "#141416"
    property string current: "home"
    signal go(string name)
    signal signInRequested()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 4

        Text {
            text: "omawave"
            color: theme.accent
            font.pixelSize: 20
            font.bold: true
            Layout.bottomMargin: 16
            textFormat: Text.PlainText
        }

        Repeater {
            model: [
                { name: "home", label: qsTr("Home") },
                { name: "search", label: qsTr("Search") },
                { name: "library", label: qsTr("Library") }
            ]
            delegate: Button {
                Layout.fillWidth: true
                text: modelData.label
                flat: true
                checked: root.current === modelData.name
                highlighted: checked
                contentItem: Text {
                    text: parent.text
                    color: parent.checked ? theme.accent : "white"
                    horizontalAlignment: Text.AlignLeft
                    verticalAlignment: Text.AlignVCenter
                    font.pixelSize: 15
                    textFormat: Text.PlainText
                }
                onClicked: root.go(modelData.name)
            }
        }

        Item { Layout.fillHeight: true }

        Text {
            Layout.fillWidth: true
            text: catalog.signedIn ? qsTr("Signed in") : qsTr("Not signed in")
            color: "#a0a0a8"
            textFormat: Text.PlainText
        }
        Button {
            Layout.fillWidth: true
            flat: true
            text: catalog.signedIn ? qsTr("Sign out") : qsTr("Sign in")
            onClicked: catalog.signedIn ? catalog.signOut() : root.signInRequested()
        }
    }
}
