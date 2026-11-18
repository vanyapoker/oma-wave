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
                && (root.setting("showVisualizer", true)
                    || (root.opened && root.track && !root.track.thumb))
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
    z: 1
    anchors.centerIn: parent
    spacing: Style.space(6)
    Visualizer {
      visible: !!root.setting("showVisualizer", true)
      anchors.verticalCenter: parent.verticalCenter
      bar: root.bar
      levels: backend.levels
      connected: backend.connected
      playing: root.playing
    }
    Item {
      id: scrollClip
      width: visible ? Math.min(Math.max(60, Math.min(400, Number(root.setting("maxLabelWidth", 120)) || 120)), label.implicitWidth) : 0
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
        onTextChanged: if (scrollTimer.running) x = scrollClip.width
        Timer {
          id: scrollTimer
          interval: 100
          repeat: true
          running: scrollClip.visible && label.implicitWidth > scrollClip.width && root.playing && root.visible && root.bar && !root.bar.barHidden && !root.opened && !Style.reduceMotion
          onRunningChanged: label.x = running ? scrollClip.width : 0
          onTriggered: {
            label.x -= 3
            if (label.x <= -label.implicitWidth) label.x = scrollClip.width
          }
        }
      }
    }
    Row {
      id: controls
      anchors.verticalCenter: parent.verticalCenter
      visible: !root.vertical && backend.connected && !!root.track && !!root.setting("showControls", true)
      spacing: 0
      LayoutMirroring.enabled: false
      LayoutMirroring.childrenInherit: true
      readonly property color fg: root.bar ? root.bar.barForeground : "white"
      Button { iconText: root.playing ? "󰏤" : "󰐊"; foreground: controls.fg; tooltipText: strings.t(root.playing ? "pause" : "play"); onClicked: backend.send("playPause") }
      Button { iconText: "󰒭"; foreground: controls.fg; tooltipText: strings.t("next"); enabled: !!backend.state && backend.state.canNext; opacity: enabled ? 1 : 0.4; onClicked: backend.send("next") }
      Button {
        readonly property bool liked: !!backend.state && backend.state.rating === "like"
        iconText: liked ? "󰔓" : "󰔔"
        foreground: liked ? Commons.Color.accent : controls.fg
        tooltipText: strings.t("like")
        onClicked: backend.send("rate", {rating: liked ? "none" : "like"})
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
