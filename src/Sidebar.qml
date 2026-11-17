import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Effects
import QtQuick.Layouts

Rectangle {
    id: root
    color: "#141416"
    property string current: "home"
    property bool compact: false
    signal go(string name)
    signal signInRequested()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 4

        Item {
            visible: root.compact
            Layout.preferredHeight: 36
            Layout.bottomMargin: 16
        }

        Text {
            visible: !root.compact
            text: "omawave"
            color: theme.accent
            font.pixelSize: 20
            font.bold: true
            Layout.bottomMargin: 16
            textFormat: Text.PlainText
        }

        Repeater {
            model: [
                { name: "home", icon: "home", label: qsTr("Home") },
                { name: "search", icon: "search", label: qsTr("Search") },
                { name: "library", icon: "library_music", label: qsTr("Library") }
            ]
            delegate: Button {
                id: nav
                readonly property bool active: root.current === modelData.name
                readonly property color tint: active ? theme.accent : "#c8c8d0"
                Layout.fillWidth: true
                implicitHeight: 40
                flat: true
                horizontalPadding: root.compact ? Math.max(0, (nav.width - 22) / 2) : 12
                text: modelData.label
                background: Rectangle {
                    radius: Math.min(6, theme.radius)
                    color: nav.active ? Qt.rgba(1, 1, 1, 0.06) : nav.hovered ? Qt.rgba(1, 1, 1, 0.03) : "transparent"
                    Rectangle {
                        visible: nav.active
                        width: 3
                        height: parent.height - 12
                        anchors.verticalCenter: parent.verticalCenter
                        radius: 1.5
                        color: theme.accent
                    }
                }
                contentItem: Row {
                    spacing: 12
                    leftPadding: root.compact ? 0 : 4
                    Item {
                        width: 22
                        height: 22
                        anchors.verticalCenter: parent.verticalCenter
                        Image {
                            id: glyph
                            anchors.fill: parent
                            visible: false
                            layer.enabled: true
                            source: "qrc:/icons/" + modelData.icon + ".svg"
                            sourceSize.width: 44
                            sourceSize.height: 44
                        }
                        MultiEffect {
                            anchors.fill: parent
                            source: glyph
                            colorization: 1.0
                            colorizationColor: nav.tint
                        }
                    }
                    Text {
                        visible: !root.compact
                        anchors.verticalCenter: parent.verticalCenter
                        text: nav.text
                        color: nav.tint
                        font.pixelSize: 15
                        textFormat: Text.PlainText
                    }
                }
                onClicked: root.go(modelData.name)
            }
        }

        Item { Layout.fillHeight: true }

        RowLayout {
            Layout.alignment: root.compact ? Qt.AlignHCenter : Qt.AlignLeft
            Layout.fillWidth: !root.compact
            spacing: 8
            IconButton {
                name: "account_circle"
                tint: "#a0a0a8"
                size: 28
                tip: catalog.signedIn ? qsTr("Signed in with %1").arg(settings.browser) : qsTr("Sign in")
                onClicked: {
                    if (!catalog.signedIn)
                        root.signInRequested()
                    else if (root.compact)
                        accountMenu.popup()
                }
                Menu {
                    id: accountMenu
                    MenuItem { text: qsTr("Sign out"); onTriggered: catalog.signOut() }
                }
                padding: 0
                Layout.preferredWidth: 28
                Layout.preferredHeight: 28
            }
            ColumnLayout {
                Layout.fillWidth: true
                visible: !root.compact
                spacing: 0
                Text {
                    Layout.fillWidth: true
                    text: catalog.signedIn ? qsTr("Signed in") : qsTr("Not signed in")
                    color: "white"
                    font.pixelSize: 12
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                }
                Text {
                    Layout.fillWidth: true
                    visible: catalog.signedIn
                    text: settings.browser
                    color: "#a0a0a8"
                    font.pixelSize: 11
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                }
            }
            Button {
                visible: !root.compact
                Layout.alignment: Qt.AlignRight
                horizontalPadding: 4
                implicitWidth: implicitContentWidth + leftPadding + rightPadding
                flat: true
                font.pixelSize: 12
                text: catalog.signedIn ? qsTr("Sign out") : qsTr("Sign in")
                onClicked: catalog.signedIn ? catalog.signOut() : root.signInRequested()
            }
        }
    }
}
