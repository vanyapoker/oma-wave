import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// A titled horizontal strip of CardTiles.
Column {
    id: root
    property string title
    property var items: []
    readonly property int tileSize: Math.max(140, Math.min(200, Math.floor((width - 12 * 5) / 6)))
    spacing: 8
    visible: !!root.items && root.items.length > 0
    readonly property real endX: strip.originX + Math.max(0, strip.contentWidth - strip.width)
    readonly property bool overflow: strip.contentWidth > strip.width + 1
    function clamp() {
        strip.contentX = Math.max(strip.originX, Math.min(endX, strip.contentX))
    }
    function page(direction) {
        const stride = tileSize + strip.spacing
        const count = Math.max(1, Math.floor(strip.width / stride))
        strip.cancelFlick()
        strip.contentX = Math.max(strip.originX, Math.min(endX, strip.contentX + direction * count * stride))
    }

    component NavButton: IconButton {
        id: nav
        Layout.preferredWidth: 32
        Layout.preferredHeight: 32
        size: 20
        Accessible.name: tip
        background: Rectangle {
            color: "transparent"
            border.width: nav.activeFocus ? 1 : 0
            border.color: "white"
            Rectangle {
                visible: nav.hovered && nav.enabled
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                height: 1
                color: "white"
            }
        }
    }

    RowLayout {
        width: root.width
        spacing: 4
        Text {
            Layout.fillWidth: true
            text: root.title
            color: "white"
            font.pixelSize: 18
            font.bold: true
            elide: Text.ElideRight
            textFormat: Text.PlainText
        }
        NavButton {
            name: "arrow_back"
            tip: qsTr("Previous items")
            visible: root.overflow
            enabled: strip.contentX > strip.originX + 1
            onClicked: root.page(-1)
        }
        NavButton {
            name: "arrow_forward"
            tip: qsTr("Next items")
            visible: root.overflow
            enabled: strip.contentX < root.endX - 1
            onClicked: root.page(1)
        }
    }
    ListView {
        id: strip
        width: root.width
        height: root.tileSize + 54 + (root.overflow ? 12 : 0)
        orientation: ListView.Horizontal
        spacing: 12
        clip: true
        model: root.items || []
        delegate: CardTile { item: modelData; size: root.tileSize }
        onWidthChanged: root.clamp()
        onContentWidthChanged: root.clamp()
        ScrollBar.horizontal: ScrollBar {
            policy: root.overflow ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
            height: 12
            background: Item {}
            contentItem: Rectangle {
                radius: 3
                color: parent.pressed || parent.hovered ? theme.accent : "#777780"
                implicitHeight: 6
            }
        }
    }
}
