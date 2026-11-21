import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    implicitHeight: content.implicitHeight + 32
    radius: theme.radius
    color: "transparent"
    property bool tuningOpen: false
    readonly property var moods: [
        { value: "any", label: qsTr("Any") }, { value: "energetic", label: qsTr("Energetic") },
        { value: "happy", label: qsTr("Happy") }, { value: "calm", label: qsTr("Calm") },
        { value: "sad", label: qsTr("Sad") }]
    readonly property var activities: [
        { value: "any", label: qsTr("Any") }, { value: "wakeup", label: qsTr("Wake up") },
        { value: "commute", label: qsTr("Commute") }, { value: "work", label: qsTr("Work") },
        { value: "workout", label: qsTr("Workout") }, { value: "sleep", label: qsTr("Sleep") }]
    readonly property var characters: [
        { value: "favourite", label: qsTr("Familiar favourites") },
        { value: "discover", label: qsTr("New discoveries") },
        { value: "popular", label: qsTr("Popular picks") }]
    function indexOf(options, value) {
        for (let i = 0; i < options.length; ++i)
            if (options[i].value === value) return i
        return 0
    }
    function page(direction) {
        strip.cancelFlick()
        strip.contentX = Math.max(strip.originX, Math.min(strip.originX + Math.max(0, strip.contentWidth - strip.width),
                                                        strip.contentX + direction * (cardWidth + strip.spacing)))
    }
    readonly property int cardWidth: 220
    Component.onCompleted: wave.prepareMixCards()

    component Action: Button {
        id: button
        background: Rectangle {
            radius: 4
            color: button.highlighted ? theme.accent : "transparent"
            border.width: button.activeFocus ? 2 : 0
            border.color: "white"
            Rectangle {
                visible: button.hovered && !button.highlighted
                color: "white"; height: 1
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            }
        }
    }
    component TuneField: ColumnLayout {
        id: field
        property string label
        property var options
        property string value
        signal picked(string value)
        Layout.fillWidth: true
        spacing: 4
        Text { text: field.label; color: "#c8c8d0"; font.pixelSize: 13 }
        ComboBox {
            Layout.fillWidth: true
            model: field.options
            textRole: "label"
            currentIndex: root.indexOf(field.options, field.value)
            onActivated: field.picked(field.options[index].value)
        }
    }
    ColumnLayout {
        id: content
        x: 16; y: 16
        width: parent.width - 32
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            Text { Layout.fillWidth: true; text: qsTr("My Wave"); color: "white"; font.pixelSize: 24; font.bold: true }
            Action { text: qsTr("Tune"); onClicked: root.tuningOpen = !root.tuningOpen }
            IconButton {
                name: "arrow_back"; tip: qsTr("Previous mixes")
                background: Rectangle { color: "transparent"; border.width: parent.activeFocus ? 1 : 0; border.color: "white" }
                visible: strip.contentWidth > strip.width + 1
                enabled: strip.contentX > strip.originX + 1
                Accessible.name: tip
                onClicked: root.page(-1)
            }
            IconButton {
                name: "arrow_forward"; tip: qsTr("Next mixes")
                background: Rectangle { color: "transparent"; border.width: parent.activeFocus ? 1 : 0; border.color: "white" }
                visible: strip.contentWidth > strip.width + 1
                enabled: strip.contentX < strip.originX + strip.contentWidth - strip.width - 1
                Accessible.name: tip
                onClicked: root.page(1)
            }
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("Choose a mix · Preview of the first songs")
            color: "#a0a0a8"; font.pixelSize: 13; wrapMode: Text.Wrap
        }
        ListView {
            id: strip
            Layout.fillWidth: true
            Layout.preferredHeight: 316
            orientation: ListView.Horizontal
            spacing: 12
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: wave.mixCards
            onWidthChanged: root.page(0)
            onContentWidthChanged: root.page(0)
            ScrollBar.horizontal: ScrollBar {
                policy: strip.contentWidth > strip.width + 1 ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                height: 12
                background: Item {}
                contentItem: Rectangle { radius: 3; color: parent.pressed || parent.hovered ? theme.accent : "#777780"; implicitHeight: 6 }
            }
            delegate: Item {
                id: card
                required property var modelData
                width: root.cardWidth; height: 304
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 5
                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 176
                        RoundedImage {
                            anchors.fill: parent
                            radius: 8
                            source: card.modelData.tracks.length ? (card.modelData.tracks[0].thumb || "") : ""
                        }
                        IconButton {
                            anchors.centerIn: parent
                            width: 56; height: 56
                            name: "graphic_eq"
                            size: 36
                            enabled: false
                            visible: !card.modelData.tracks.length || !card.modelData.tracks[0].thumb
                        }
                        Rectangle {
                            anchors.fill: parent
                            radius: 8
                            color: "transparent"
                            border.width: card.modelData.active ? 2 : 0
                            border.color: theme.accent
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: root.characters[root.indexOf(root.characters, card.modelData.character)].label
                        color: "white"; font.pixelSize: 14; font.bold: true
                        elide: Text.ElideRight
                        textFormat: Text.PlainText
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 1
                        Repeater {
                            model: card.modelData.tracks
                            delegate: Text {
                                required property var modelData
                                Layout.fillWidth: true
                                text: (modelData.title || "") + " · " + ((modelData.artists || []).join(", ") || qsTr("Unknown artist"))
                                textFormat: Text.PlainText
                                color: "#a0a0a8"; font.pixelSize: 12; elide: Text.ElideRight
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: !card.modelData.tracks.length
                            text: card.modelData.error || (card.modelData.loading ? qsTr("Finding songs…") : qsTr("Waiting for preview…"))
                            textFormat: Text.PlainText
                            color: "#a0a0a8"; font.pixelSize: 12; wrapMode: Text.Wrap
                        }
                        Item { Layout.fillHeight: true }
                    }
                    Action {
                        Layout.fillWidth: true
                        highlighted: true
                        text: card.modelData.error ? qsTr("Retry") : card.modelData.active
                              ? (player.playing ? qsTr("Pause") : qsTr("Resume")) : qsTr("Play this mix")
                        enabled: !!card.modelData.error || card.modelData.active || card.modelData.tracks.length > 0
                        onClicked: {
                            if (card.modelData.error) wave.retryMix(card.modelData.character)
                            else if (card.modelData.active) player.togglePause()
                            else wave.playMix(card.modelData.character)
                        }
                    }
                }
            }
        }
        GridLayout {
            Layout.fillWidth: true
            visible: root.tuningOpen
            columns: root.width >= 720 ? 3 : 1
            columnSpacing: 12
            TuneField { label: qsTr("Mood"); options: root.moods; value: wave.mood; onPicked: v => wave.mood = v }
            TuneField { label: qsTr("Activity"); options: root.activities; value: wave.activity; onPicked: v => wave.activity = v }
            TuneField { label: qsTr("Mix"); options: root.characters; value: wave.character; onPicked: v => wave.character = v }
        }
        Text {
            visible: root.tuningOpen
            text: qsTr("Changes start when you play this mix.")
            color: "#a0a0a8"; font.pixelSize: 13
        }
    }
}
