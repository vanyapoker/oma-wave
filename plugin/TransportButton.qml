import QtQuick
import qs.Commons
import qs.Ui

Button {
  id: root
  color: "transparent"
  borderSpec: Border.none()
  tooltipText: ""

  Rectangle {
    anchors.horizontalCenter: parent.horizontalCenter
    anchors.bottom: parent.bottom
    width: Math.min(parent.width, root.iconSize)
    height: Math.max(1, Style.space(2))
    color: root.foreground
    visible: root.enabled && root.hot
  }
}
