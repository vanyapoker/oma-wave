import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Dialog {
    id: dialog
    modal: true
    anchors.centerIn: Overlay.overlay
    width: 420
    title: qsTr("Sign in to YouTube Music")
    standardButtons: Dialog.Close
    property bool working: false

    onOpened: { catalog.clearError(); working = false }

    Connections {
        target: catalog
        function onSignedInChanged() {
            if (catalog.signedIn && dialog.opened) {
                settings.browser = browser.currentText
                dialog.close()
                catalog.loadLibrary()
            }
        }
        function onBusyChanged() { if (!catalog.busy) dialog.working = false }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12
        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: "#a0a0a8"
            text: qsTr("omawave reuses the session of a browser where you are already signed in at music.youtube.com.")
            textFormat: Text.PlainText
        }
        ComboBox {
            id: browser
            Layout.fillWidth: true
            model: ["chromium", "chrome", "brave", "firefox", "vivaldi", "edge", "opera"]
            Component.onCompleted: currentIndex = Math.max(0, model.indexOf(settings.browser))
        }
        RowLayout {
            Layout.fillWidth: true
            BusyIndicator { running: dialog.working; Layout.preferredHeight: 32; Layout.preferredWidth: 32 }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: catalog.error !== "" ? "#ff6b6b" : "#a0a0a8"
                text: catalog.error !== "" ? catalog.error : dialog.working ? qsTr("Signing in…") : ""
                textFormat: Text.PlainText
            }
        }
        Button {
            text: qsTr("Sign in")
            highlighted: true
            enabled: !dialog.working
            Layout.alignment: Qt.AlignRight
            onClicked: { catalog.clearError(); dialog.working = true; catalog.signIn(browser.currentText) }
        }
    }
}
