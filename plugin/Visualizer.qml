import QtQuick
import qs.Commons

Row {
  id: root
  property var bar: null
  property var levels: [0, 0, 0, 0, 0, 0, 0, 0]
  property bool playing: false
  property bool connected: false
  readonly property var idleHeights: [4, 7, 10, 6, 9, 5, 8, 4]
  spacing: bar && bar.vertical ? 1 : 2
  height: 14
  opacity: !connected || !playing ? 0.5 : 1

  Repeater {
    model: 8
    Rectangle {
      required property int index
      width: root.bar && root.bar.vertical ? 2 : 3
      height: !root.connected || !root.playing || Style.reduceMotion
              ? root.idleHeights[index] : 2 + Math.max(0, Math.min(100, root.levels[index] || 0)) * 0.12
      anchors.bottom: parent.bottom
      color: root.bar ? root.bar.barForeground : "white"
      Behavior on height { NumberAnimation { duration: Style.duration(60); easing.type: Easing.Linear } }
    }
  }
}
