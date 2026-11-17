import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// A titled horizontal strip of CardTiles.
Column {
    id: root
    property string title
    property var items: []
    readonly property int tileSize: Math.max(140, Math.min(200, Math.floor((width - 12 * 5) / 6)))
    spacing: 8
    visible: !!root.items && root.items.length > 0

    Text {
        text: root.title
        color: "white"
        font.pixelSize: 18
        font.bold: true
        textFormat: Text.PlainText
    }
    ListView {
        width: root.width
        height: root.tileSize + 54
        orientation: ListView.Horizontal
        spacing: 12
        clip: true
        model: root.items || []
        delegate: CardTile { item: modelData; size: root.tileSize }
    }
}
