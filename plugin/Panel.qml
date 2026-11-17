import QtQuick
import qs.Commons
import qs.Commons as Commons
import qs.Ui
import "Model.js" as Model

Panel {
  id: root
  moduleName: "vp.omawave"
  ipcTarget: "vp.omawave"
  manageIpc: false
  property var anchorItem: null
  property var hostWidget: null
  property var backend: null
  readonly property var barIdentity: hostWidget || root
  property bool openedFromHotkey: false
  property bool showLyrics: false
  property int cursor: 0
  readonly property var playerState: backend && backend.connected ? backend.state : null
  readonly property var track: playerState ? playerState.track : null
  readonly property var queue: playerState && playerState.queue ? playerState.queue : []
  readonly property var lyricData: playerState && playerState.lyrics ? playerState.lyrics : ({state: "missing", lines: []})
  readonly property var lines: lyricData.lines || []
  readonly property int currentLyric: lyricData.state === "synced" ? Model.lyricIndex(lines, backend ? backend.positionMs : 0) : -1

  function setCenterHoverRevealSuppressed(value) {
    if (bar && typeof bar.setCenterHoverRevealSuppressed === "function") bar.setCenterHoverRevealSuppressed(value)
    else if (bar && "centerHoverRevealSuppressed" in bar) bar.centerHoverRevealSuppressed = value
  }
  function open() {
    openedFromHotkey = false
    setCenterHoverRevealSuppressed(false)
    controller.show()
    cursor = Math.max(0, Math.min(queue.length - 1, playerState ? playerState.queueIndex - playerState.queueOffset : 0))
  }
  function openFromHotkey() {
    openedFromHotkey = true
    controller.show()
    cursor = Math.max(0, Math.min(queue.length - 1, playerState ? playerState.queueIndex - playerState.queueOffset : 0))
    Qt.callLater(function() { if (root.opened) root.setCenterHoverRevealSuppressed(true) })
  }
  function close() { setCenterHoverRevealSuppressed(false); controller.hide() }
  function toggle() { if (opened) close(); else openFromHotkey() }
  function switchPanel(direction) {
    if (bar && typeof bar.switchPanelFrom === "function") return bar.switchPanelFrom(barIdentity, direction)
    return false
  }
  function openApp() {
    if (backend && backend.connected) backend.send("show")
    else if (bar) bar.run("omawave")
    close()
  }
  function selectedIndex() { return (playerState ? playerState.queueOffset : 0) + cursor }
  onQueueChanged: cursor = Math.max(0, Math.min(cursor, queue.length - 1))

  Commons.Translations { id: strings; directory: Qt.resolvedUrl("i18n") }
  KeyboardPanel {
    id: panel
    anchorItem: root.anchorItem
    owner: root.barIdentity
    bar: root.bar
    open: root.opened
    centerOnBar: false
    focusTarget: panel.loadedContent
    contentWidth: panel.fittedContentWidth(Style.space(380))
    contentHeight: panel.fittedContentHeight(panel.loadedContentHeight)
    content: PanelKeyCatcher {
      id: catcher
      anchors.fill: parent
      implicitHeight: column.implicitHeight
      property bool returnHandled: false
      onCloseRequested: root.close()
      onTabRequested: function(direction) { root.switchPanel(direction) }
      onReturnRequested: returnHandled = true
      onActivateRequested: {
        var enter = returnHandled
        returnHandled = false
        if (!root.backend || !root.backend.connected) return
        if (enter) { if (!root.showLyrics && root.queue.length) root.backend.send("jump", {index: root.selectedIndex()}) }
        else root.backend.send("playPause")
      }
      onDeleteRequested: if (root.backend && root.backend.connected && !root.showLyrics && root.queue.length) root.backend.send("remove", {index: root.selectedIndex()})
      onMoveRequested: function(dx, dy) {
        if (!root.backend || !root.backend.connected) return
        if (dx) root.backend.send(dx < 0 ? "previous" : "next")
        else if (!root.showLyrics && root.queue.length) {
          root.cursor = Math.max(0, Math.min(root.queue.length - 1, root.cursor + dy))
          queueList.positionViewAtIndex(root.cursor, ListView.Contain)
        }
      }
      onTextKey: function(text, modifiers) {
        if (text.toLowerCase() === "y") root.showLyrics = !root.showLyrics
        else if (text.toLowerCase() === "o") root.openApp()
      }

      Column {
        id: column
        width: parent.width
        spacing: Style.space(10)
        Column {
          visible: !root.playerState
          width: parent.width
          spacing: Style.space(10)
          Text {
            textFormat: Text.PlainText
            text: strings.t("notRunning")
            color: root.bar ? root.bar.foreground : Commons.Color.foreground
            font.family: root.bar ? root.bar.fontFamily : Style.font.family
            font.pixelSize: Style.font.body
          }
          Button {
            text: strings.t("start")
            foreground: root.bar ? root.bar.foreground : Commons.Color.foreground
            onClicked: if (root.bar) root.bar.run("systemctl --user start omawave.service || omawave --background")
          }
        }
        Column {
          visible: !!root.playerState
          width: parent.width
          spacing: Style.space(10)
          Row {
            width: parent.width
            spacing: Style.space(10)
            Item {
              width: Style.space(72); height: width
              SizedImage {
                anchors.fill: parent
                crop: true
                url: root.track && root.track.thumb ? root.track.thumb : ""
                visible: url !== ""
              }
              Visualizer {
                anchors.centerIn: parent
                visible: !root.track || !root.track.thumb
                bar: root.bar
                levels: root.backend ? root.backend.levels : []
                connected: !!root.playerState
                playing: root.playerState && root.playerState.playing
              }
            }
            Column {
              width: parent.width - Style.space(82)
              spacing: Style.space(4)
              Text {
                width: parent.width
                textFormat: Text.PlainText
                text: root.track && root.track.title ? root.track.title : strings.t("nothingPlaying")
                color: root.bar ? root.bar.foreground : Commons.Color.foreground
                font.family: root.bar ? root.bar.fontFamily : Style.font.family
                font.pixelSize: Style.font.subtitle
                font.bold: true
                elide: Text.ElideRight
              }
              Text {
                width: parent.width
                textFormat: Text.PlainText
                text: root.track && root.track.artists ? root.track.artists.join(", ") : ""
                color: root.bar ? root.bar.foreground : Commons.Color.foreground
                font.family: root.bar ? root.bar.fontFamily : Style.font.family
                font.pixelSize: Style.font.bodySmall
                elide: Text.ElideRight
              }
              Text {
                width: parent.width
                textFormat: Text.PlainText
                text: root.track && root.track.album ? root.track.album : ""
                color: root.bar ? root.bar.foreground : Commons.Color.foreground
                font.family: root.bar ? root.bar.fontFamily : Style.font.family
                font.pixelSize: Style.font.caption
                elide: Text.ElideRight
              }
            }
          }
          PanelSlider {
            id: seek
            width: parent.width
            bar: root.bar
            maximum: root.playerState ? Math.max(1, root.playerState.durationMs || 0) : 1
            value: root.backend ? root.backend.positionMs : 0
            integer: true
            enabled: !!root.track
            onReleased: function(value) { if (root.backend) root.backend.send("seek", {positionMs: Math.round(value)}) }
          }
          Item {
            width: parent.width; height: timeLeft.implicitHeight
            Text {
              id: timeLeft
              anchors.left: parent.left
              textFormat: Text.PlainText
              text: Model.formatTime(seek.dragging ? seek.liveValue : (root.backend ? root.backend.positionMs : 0))
              color: root.bar ? root.bar.foreground : Commons.Color.foreground
              font.pixelSize: Style.font.caption
            }
            Text {
              anchors.right: parent.right
              textFormat: Text.PlainText
              text: Model.formatTime(root.playerState ? root.playerState.durationMs : 0)
              color: root.bar ? root.bar.foreground : Commons.Color.foreground
              font.pixelSize: Style.font.caption
            }
          }
          Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: Style.space(6)
            LayoutMirroring.enabled: false
            LayoutMirroring.childrenInherit: true
            Button { iconText: "󰒮"; foreground: root.bar ? root.bar.foreground : Commons.Color.foreground; enabled: !!root.playerState && root.playerState.canPrevious; opacity: enabled ? 1 : 0.4; onClicked: root.backend.send("previous") }
            Button { iconText: root.playerState && root.playerState.playing ? "󰏤" : "󰐊"; foreground: root.bar ? root.bar.foreground : Commons.Color.foreground; onClicked: root.backend.send("playPause") }
            Button { iconText: "󰒭"; foreground: root.bar ? root.bar.foreground : Commons.Color.foreground; enabled: !!root.playerState && root.playerState.canNext; opacity: enabled ? 1 : 0.4; onClicked: root.backend.send("next") }
          }
          Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: Style.space(4)
            Button { text: strings.t("queue"); selected: !root.showLyrics; foreground: root.bar ? root.bar.foreground : Commons.Color.foreground; onClicked: root.showLyrics = false }
            Button { text: strings.t("lyrics"); selected: root.showLyrics; foreground: root.bar ? root.bar.foreground : Commons.Color.foreground; onClicked: root.showLyrics = true }
          }
          Item {
            width: parent.width
            height: Style.space(240)
            ListView {
              id: queueList
              anchors.fill: parent
              visible: !root.showLyrics
              clip: true
              model: root.queue
              delegate: Button {
                required property var modelData
                required property int index
                width: queueList.width
                leftAlign: true
                text: (modelData.title || "") + (modelData.artist ? " · " + modelData.artist : "")
                foreground: root.bar ? root.bar.foreground : Commons.Color.foreground
                selected: root.playerState && root.playerState.queueOffset + index === root.playerState.queueIndex
                hasCursor: root.cursor === index
                onHovered: function(hot) { if (hot) root.cursor = index }
                onClicked: { root.cursor = index; root.backend.send("jump", {index: root.selectedIndex()}) }
              }
            }
            ListView {
              id: lyricList
              anchors.fill: parent
              visible: root.showLyrics && (root.lyricData.state === "synced" || root.lyricData.state === "plain")
              clip: true
              model: root.lines
              delegate: Text {
                required property var modelData
                required property int index
                width: lyricList.width
                textFormat: Text.PlainText
                text: modelData.text || ""
                color: root.currentLyric === index ? Commons.Color.accent : (root.bar ? root.bar.foreground : Commons.Color.foreground)
                font.family: root.bar ? root.bar.fontFamily : Style.font.family
                font.pixelSize: Style.font.body
                font.bold: root.currentLyric === index
                wrapMode: Text.Wrap
                padding: Style.space(4)
              }
              Connections {
                target: root
                function onCurrentLyricChanged() { if (root.currentLyric >= 0) lyricList.positionViewAtIndex(root.currentLyric, ListView.Center) }
                function onShowLyricsChanged() { if (root.showLyrics && root.currentLyric >= 0) Qt.callLater(function() { lyricList.positionViewAtIndex(root.currentLyric, ListView.Center) }) }
              }
            }
            Text {
              anchors.centerIn: parent
              visible: root.showLyrics && root.lyricData.state === "missing"
              textFormat: Text.PlainText
              text: strings.t("noLyrics")
              color: root.bar ? root.bar.foreground : Commons.Color.foreground
              font.family: root.bar ? root.bar.fontFamily : Style.font.family
              font.pixelSize: Style.font.body
            }
          }
        }
        Button {
          text: strings.t("open")
          foreground: root.bar ? root.bar.foreground : Commons.Color.foreground
          onClicked: root.openApp()
        }
      }
    }
  }
}
