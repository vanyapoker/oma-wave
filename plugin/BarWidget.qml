import QtQuick
import qs.Commons
import qs.Commons as Commons
import qs.Ui
import "Model.js" as Model

BarWidget {
  id: root
  moduleName: "vp.omawave"
  readonly property bool opened: panelLoader.item ? panelLoader.item.opened : false
  readonly property bool popoutSwitchClosing: panelLoader.item ? panelLoader.item.popoutSwitchClosing : false
  readonly property var track: backend.state ? backend.state.track : null
  readonly property bool playing: backend.connected && !!backend.state.playing
  readonly property string artist: track && track.artists ? track.artists.join(", ") : ""
  visible: true
  implicitWidth: row.implicitWidth + Style.space(14)
  implicitHeight: barSize

  function injectPanel() {
    var target = panelLoader.item
    if (!target) return
    target.bar = root.bar
    target.settings = root.settings
    target.anchorItem = root
    target.hostWidget = root
    target.backend = backend
  }
  function open() { if (panelLoader.item) panelLoader.item.openFromHotkey() }
  function close() { if (panelLoader.item) panelLoader.item.close() }
  function closeForPopoutSwitch() { if (panelLoader.item) panelLoader.item.closeForPopoutSwitch() }
  onBarChanged: injectPanel()
  onSettingsChanged: injectPanel()

  Commons.Translations { id: strings; directory: Qt.resolvedUrl("i18n") }
  Backend {
    id: backend
    wantLevels: root.visible && root.bar && !root.bar.barHidden && !Style.reduceMotion
    popupOpen: root.opened
  }
  Loader {
    id: panelLoader
    active: true
    source: Qt.resolvedUrl("Panel.qml")
    visible: false
    onLoaded: { root.injectPanel(); Qt.callLater(root.injectPanel) }
  }
  Row {
    id: row
    anchors.centerIn: parent
    spacing: Style.space(6)
    Visualizer {
      anchors.verticalCenter: parent.verticalCenter
      bar: root.bar
      levels: backend.levels
      connected: backend.connected
      playing: root.playing
    }
    Item {
      id: scrollClip
      width: visible ? Math.min(Math.max(60, Math.min(400, Number(root.setting("maxLabelWidth", 180)) || 180)), label.implicitWidth) : 0
      height: label.implicitHeight
      clip: true
      anchors.verticalCenter: parent.verticalCenter
      visible: !root.vertical && !!root.setting("showTitle", true) && !!root.track
      Text {
        id: label
        textFormat: Text.PlainText
        text: Model.trackLabel(root.track)
        color: root.bar ? root.bar.barForeground : "white"
        font.family: root.bar ? root.bar.fontFamily : Style.font.family
        font.pixelSize: Style.font.body
        anchors.verticalCenter: parent.verticalCenter
        NumberAnimation on x {
          running: label.implicitWidth > scrollClip.width && root.playing && root.visible && root.bar && !root.bar.barHidden && !root.opened && !root.vertical && !Style.reduceMotion
          onRunningChanged: if (!running) label.x = 0
          loops: Animation.Infinite
          duration: Style.duration(Math.max(6000, label.implicitWidth * 25))
          from: scrollClip.width
          to: -label.implicitWidth
          easing.type: Easing.Linear
        }
      }
    }
  }
  MouseArea {
    anchors.fill: parent
    hoverEnabled: true
    acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
    cursorShape: Qt.PointingHandCursor
    onClicked: function(mouse) {
      if (mouse.button === Qt.LeftButton) { if (panelLoader.item) panelLoader.item.toggle() }
      else if (mouse.button === Qt.MiddleButton) backend.send("playPause")
      else if (backend.connected) backend.send("show")
      else if (root.bar) root.bar.run("omawave")
    }
    onWheel: function(wheel) {
      if (wheel.angleDelta.y > 0) backend.send("previous")
      else if (wheel.angleDelta.y < 0) backend.send("next")
    }
    onEntered: if (root.bar) root.bar.showTooltip(root, root.track ? (root.track.title || "") + (root.artist ? " — " + root.artist : "") : strings.t("notRunning"))
    onExited: if (root.bar) root.bar.hideTooltip(root)
  }
}
