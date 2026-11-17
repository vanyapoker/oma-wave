import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

Page {
    id: root
    background: null
    Component.onCompleted: if (catalog.home.length === 0) catalog.loadHome()

    header: Item {
        height: 52
        Text {
            x: 20
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Home")
            color: "white"
            font.pixelSize: 24
            font.bold: true
            textFormat: Text.PlainText
        }
        Button {
            anchors.right: parent.right
            anchors.rightMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            flat: true
            text: qsTr("Refresh")
            enabled: !catalog.busy
            onClicked: catalog.loadHome()
        }
    }

    ListView {
        anchors.fill: parent
        anchors.leftMargin: 20
        anchors.rightMargin: 20
        spacing: 20
        clip: true
        model: catalog.home
        delegate: CardRow {
            width: ListView.view.width
            title: modelData.title
            items: modelData.items
        }
        BusyIndicator {
            anchors.centerIn: parent
            running: catalog.busy && catalog.home.length === 0
        }
    }
}
