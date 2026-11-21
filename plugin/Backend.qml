import QtQuick
import Quickshell
import Quickshell.Io
import "Model.js" as Model

QtObject {
  id: root
  readonly property string path: Quickshell.env("XDG_RUNTIME_DIR") + "/omawave/control.sock"
  property var state: null
  property var levels: [0, 0, 0, 0, 0, 0, 0, 0]
  property bool wantLevels: false
  property bool popupOpen: false
  readonly property bool connected: state !== null && socket && socket.connected
  property real basePosition: 0
  property real receivedAt: 0
  property real positionMs: 0
  property real pendingSeek: -1
  property string pendingTrack: ""

  function seek(value) {
    if (!send("seek", {positionMs: Math.round(value)})) return
    pendingSeek = Math.max(0, Math.round(value))
    pendingTrack = state && state.track ? state.track.videoId : ""
    seekTimeout.restart()
    updatePosition()
  }

  function acceptPosition(value) {
    basePosition = Number(value) || 0
    receivedAt = Date.now()
    if (pendingSeek >= 0 && Math.abs(basePosition - pendingSeek) <= 2000) {
      pendingSeek = -1
      seekTimeout.stop()
    }
    updatePosition()
  }
  property int retryDelay: 1000

  function updatePosition() {
    var value = pendingSeek >= 0 ? pendingSeek : basePosition + (state && state.playing ? Math.max(0, Date.now() - receivedAt) : 0)
    positionMs = Math.max(0, state && state.durationMs > 0 ? Math.min(value, state.durationMs) : value)
  }
  function receive(line) {
    var msg
    try { msg = JSON.parse(line) } catch (e) { return }
    if (!msg || msg.v !== 1) return
    if (msg.type === "state") {
      if (pendingSeek >= 0 && (!msg.track || msg.track.videoId !== pendingTrack ||
          (state && msg.queueIndex !== state.queueIndex))) {
        pendingSeek = -1
        seekTimeout.stop()
      }
      state = msg
      acceptPosition(msg.positionMs)
    } else if (msg.type === "position") {
      acceptPosition(msg.positionMs)
    } else if (msg.type === "levels") {
      var parsed = Model.parseLevels(msg.l)
      if (parsed) levels = parsed
    }
  }
  function send(cmd, args) {
    if (!socket || !socket.connected) return false
    socket.write(JSON.stringify(Object.assign({}, args || {}, {v: 1, cmd: cmd})) + "\n")
    socket.flush()
    return true
  }
  onWantLevelsChanged: send("subscribeLevels", {on: wantLevels})
  onPopupOpenChanged: if (popupOpen) updatePosition()

  property Component socketFactory: Component {
    Socket {
      path: root.path
      connected: true
      parser: SplitParser { onRead: function(line) { root.receive(line) } }
      onConnectedChanged: {
        if (connected) {
          root.retryDelay = 1000
          Qt.callLater(function() { root.send("subscribeLevels", {on: root.wantLevels}) })
        } else {
          root.state = null
          root.levels = [0, 0, 0, 0, 0, 0, 0, 0]
          root.pendingSeek = -1
          root.seekTimeout.stop()
          root.positionMs = 0
        }
      }
    }
  }
  property var socket: socketFactory.createObject(root)
  property Timer reconnect: Timer {
    interval: root.retryDelay
    repeat: true
    running: !root.socket || !root.socket.connected
    onTriggered: {
      var previous = root.socket
      root.socket = root.socketFactory.createObject(root)
      if (previous) previous.destroy()
      root.retryDelay = Math.min(30000, root.retryDelay * 2)
    }
  }
  property Timer seekTimeout: Timer {
    interval: 15000
    onTriggered: { root.pendingSeek = -1; root.updatePosition() }
  }
  property Timer interpolation: Timer {
    interval: 250
    repeat: true
    running: root.popupOpen && root.connected && root.state.playing
    onTriggered: root.updatePosition()
  }
}
