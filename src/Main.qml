import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 1100
    height: 720
    minimumWidth: 760
    minimumHeight: 520
    visible: true
    title: "omawave"
    color: "#0e0e10"
    readonly property bool narrow: width < 900

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    property bool helpVisible: false
    // "", "queue" or "lyrics": which pane is open on the right.
    property string pane: ""
    // Single-letter shortcuts must not fire while the user types in a field.
    readonly property bool typing: activeFocusItem !== null && activeFocusItem.cursorPosition !== undefined
    readonly property string banner: player.error !== "" ? player.error
        : catalog.error !== "" ? catalog.error
        : lyrics.state === "error" ? qsTr("Could not load lyrics") : ""

    function present() { show(); raise(); requestActivate() }

    function navigate(name) {
        stack.replace(null, name === "search" ? searchPage : name === "library" ? libraryPage : homePage)
        sidebar.current = name
    }
    function openCollection(kind, id) {
        catalog.openDetail(kind, id)
        stack.push(collectionPage)
    }
    function focusSearch() {
        navigate("search")
        Qt.callLater(function() { if (stack.currentItem && stack.currentItem.focusSearch) stack.currentItem.focusSearch() })
    }
    function togglePane(name) { pane = pane === name ? "" : name }
    function goBack() { if (stack.depth > 1) stack.pop() }
    function openNowPlaying() {
        if (!(stack.currentItem && stack.currentItem.isNowPlaying))
            stack.push(nowPlayingPage)
    }

    // Quitting closes every window first, so the hide-on-close below must step aside.
    property bool quitting: false
    function quitApp() { quitting = true; Qt.quit() }

    onClosing: function(close) {
        if (quitting) return
        close.accepted = false
        hide()
    }

    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: player.togglePause() }
    Shortcut { sequence: "Ctrl+Right"; context: Qt.ApplicationShortcut; onActivated: player.next() }
    Shortcut { sequence: "Ctrl+Left"; context: Qt.ApplicationShortcut; onActivated: player.previous() }
    Shortcut { sequence: "Right"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: player.seek(Math.min(player.durationMs, player.positionMs + 10000)) }
    Shortcut { sequence: "Left"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: player.seek(Math.max(0, player.positionMs - 10000)) }
    Shortcut { sequences: ["+", "="]; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: player.setVolume(player.volume + 5) }
    Shortcut { sequences: ["-", "\u2212"]; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: player.setVolume(player.volume - 5) }
    Shortcut { sequence: "1"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: win.navigate("home") }
    Shortcut { sequence: "2"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: win.navigate("search") }
    Shortcut { sequence: "3"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: win.navigate("library") }
    Shortcut { sequence: "/"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: win.focusSearch() }
    Shortcut { sequence: "Ctrl+F"; context: Qt.ApplicationShortcut; onActivated: win.focusSearch() }
    Shortcut { sequence: "U"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: win.togglePane("queue") }
    Shortcut { sequence: "L"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: win.togglePane("lyrics") }
    Shortcut { sequence: "W"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: wave.start() }
    Shortcut { sequence: "F"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: wave.toggleLike() }
    Shortcut { sequence: "D"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: wave.dislike() }
    Shortcut { sequence: "N"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: win.openNowPlaying() }
    Shortcut {
        sequence: "Esc"
        context: Qt.ApplicationShortcut
        onActivated: {
            if (win.helpVisible) win.helpVisible = false
            else if (win.typing) win.contentItem.forceActiveFocus()
            else if (stack.depth > 1) stack.pop()
        }
    }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: win.hide() }
    Shortcut { sequence: "Ctrl+Q"; context: Qt.ApplicationShortcut; onActivated: win.quitApp() }

    Component { id: homePage; HomePage {} }
    Component { id: searchPage; SearchPage {} }
    Component { id: libraryPage; LibraryPage { onSignInRequested: signIn.open() } }
    Component { id: collectionPage; CollectionPage {} }
    Component { id: nowPlayingPage; NowPlayingPage {} }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        RowLayout {
            anchors.fill: parent
            anchors.bottomMargin: 72
            spacing: 0

            Sidebar {
                id: sidebar
                Layout.fillHeight: true
                compact: win.narrow
                Layout.preferredWidth: win.narrow ? 64 : 200
                onGo: function(name) { win.navigate(name) }
                onSignInRequested: signIn.open()
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? 32 : 0
                    visible: win.banner !== ""
                    color: Material.color(Material.Red, Material.Shade900)
                    Text {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 40
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                        color: "white"
                        text: win.banner
                        textFormat: Text.PlainText
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: { player.clearError(); catalog.clearError(); lyrics.clearError() }
                    }
                    IconButton {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        width: 32
                        height: 32
                        padding: 0
                        name: "close"
                        size: 18
                        tip: qsTr("Dismiss")
                        onClicked: { player.clearError(); catalog.clearError(); lyrics.clearError() }
                    }
                }

                StackView {
                    id: stack
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    initialItem: homePage
                }
            }

            Loader {
                Layout.fillHeight: true
                Layout.preferredWidth: win.pane === "" ? 0 : 340
                visible: win.pane !== ""
                sourceComponent: win.pane === "queue" ? queuePane : win.pane === "lyrics" ? lyricsPane : null
            }
        }

        NowPlayingBar {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 72
            pane: win.pane
            onNowPlayingRequested: win.openNowPlaying()
            onTogglePane: function(name) { win.togglePane(name) }
        }
    }

    Component { id: queuePane; QueuePane {} }
    Component { id: lyricsPane; LyricsPane {} }

    SignInDialog { id: signIn }
    ShortcutsOverlay { visible: win.helpVisible; onDismissed: win.helpVisible = false }
}
