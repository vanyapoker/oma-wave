import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// A titled horizontal strip of CardTiles.
Column {
    id: root
    property string title
    property var items: []
    spacing: 8
    visible: items && items.length > 0

    Text {
        text: root.title
        color: "white"
        font.pixelSize: 18
        font.bold: true
        textFormat: Text.PlainText
    }
    ListView {
        width: root.width
        height: 214
        orientation: ListView.Horizontal
        spacing: 12
        clip: true
        model: root.items
        delegate: CardTile { item: modelData }
    }
}
