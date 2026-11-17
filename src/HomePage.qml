import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

Page {
    id: root
    background: null
    property int hour: new Date().getHours()
    Component.onCompleted: {
        hour = new Date().getHours()
        if (catalog.home.length === 0) catalog.loadHome()
    }

    header: Item {
        height: 52
        Text {
            x: 20
            anchors.verticalCenter: parent.verticalCenter
            text: root.hour >= 5 && root.hour < 12 ? qsTr("Good morning")
                : root.hour >= 12 && root.hour < 18 ? qsTr("Good afternoon") : qsTr("Good evening")
            color: "white"
            font.pixelSize: 24
            font.bold: true
            textFormat: Text.PlainText
        }
        IconButton {
            anchors.right: parent.right
            anchors.rightMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            name: "refresh"
            tip: qsTr("Refresh")
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
        header: Item {
            width: ListView.view.width
            height: hero.height + 16
            WaveHero {
                id: hero
                width: parent.width
                height: implicitHeight
            }
        }
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
