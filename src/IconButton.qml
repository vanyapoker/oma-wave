import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// A flat button showing one Material Symbols icon from src/icons.
ToolButton {
    id: root
    property string name
    property bool filled: false
    property color tint: "white"
    property int size: 24
    implicitWidth: size + 16
    implicitHeight: size + 16
    property string tip

    icon.source: "qrc:/icons/" + name + (filled ? "_fill" : "") + ".svg"
    icon.color: enabled ? tint : Qt.rgba(1, 1, 1, 0.3)
    icon.width: size
    icon.height: size
    display: AbstractButton.IconOnly
    ToolTip.text: tip
    ToolTip.visible: hovered && tip !== ""
    ToolTip.delay: 500
}
