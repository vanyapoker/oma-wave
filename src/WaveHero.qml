import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// "My Wave": start button, visualizer and the Mood / Activity / Character chips.
Rectangle {
    id: root
    implicitHeight: content.implicitHeight + 40
    radius: 12
    gradient: Gradient {
        orientation: Gradient.Horizontal
        GradientStop { position: 0.0; color: Qt.alpha(theme.accent, 0.35) }
        GradientStop { position: 1.0; color: Qt.alpha(theme.accent, 0.05) }
    }

    readonly property var moods: [
        { value: "any", label: qsTr("Any") }, { value: "energetic", label: qsTr("Energetic") },
        { value: "happy", label: qsTr("Happy") }, { value: "calm", label: qsTr("Calm") },
        { value: "sad", label: qsTr("Sad") }]
    readonly property var activities: [
        { value: "any", label: qsTr("Any") }, { value: "wakeup", label: qsTr("Wake up") },
        { value: "commute", label: qsTr("Commute") }, { value: "work", label: qsTr("Work") },
        { value: "workout", label: qsTr("Workout") }, { value: "sleep", label: qsTr("Sleep") }]
    readonly property var characters: [
        { value: "favourite", label: qsTr("Favourite") }, { value: "discover", label: qsTr("Discover") },
        { value: "popular", label: qsTr("Popular") }]

    function labelOf(options, value) {
        for (const o of options)
            if (o.value === value) return o.label
        return ""
    }
    readonly property string summary: [labelOf(moods, wave.mood), labelOf(activities, wave.activity),
                                       labelOf(characters, wave.character)].join(" · ")

    component Chip: Button {
        id: chip
        property bool on: false
        implicitHeight: 32
        horizontalPadding: 14
        flat: true
        background: Rectangle {
            radius: 16
            color: chip.on ? theme.accent : Qt.rgba(1, 1, 1, chip.hovered ? 0.14 : 0.08)
        }
        contentItem: Text {
            text: chip.text
            color: chip.on ? theme.accentForeground : "white"
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font.pixelSize: 13
            textFormat: Text.PlainText
        }
    }

    component ChipRow: RowLayout {
        id: chips
        property string label
        property var options: []
        property string value
        signal picked(string value)
        Layout.fillWidth: true
        spacing: 8
        Text {
            Layout.preferredWidth: 90
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 7
            text: chips.label
            color: "#a0a0a8"
            font.pixelSize: 13
            textFormat: Text.PlainText
        }
        Flow {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            spacing: 8
            Repeater {
                model: chips.options
                delegate: Chip {
                    text: modelData.label
                    on: chips.value === modelData.value
                    onClicked: chips.picked(modelData.value)
                }
            }
        }
    }

    ColumnLayout {
        id: content
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 16
            Rectangle {
                Layout.preferredWidth: 64
                Layout.preferredHeight: 64
                radius: 32
                color: theme.accent
                IconButton {
                    anchors.centerIn: parent
                    width: 64
                    height: 64
                    padding: 0
                    visible: !wave.loading
                    name: wave.active && player.playing ? "pause" : "play_arrow"
                    filled: true
                    size: 36
                    tint: theme.accentForeground
                    onClicked: wave.active ? player.togglePause() : wave.start()
                }
                BusyIndicator {
                    anchors.centerIn: parent
                    width: 40
                    height: 40
                    running: wave.loading
                    visible: wave.loading
                    Material.accent: theme.accentForeground
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text {
                    text: qsTr("My Wave")
                    color: "white"
                    font.pixelSize: 32
                    font.bold: true
                    textFormat: Text.PlainText
                }
                Text {
                    Layout.fillWidth: true
                    text: root.summary
                    color: "#c8c8d0"
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                }
            }
            Row {
                id: bars
                visible: root.width >= 520
                readonly property bool live: wave.active && player.playing
                readonly property var still: [12, 20, 28, 16, 24, 14, 22, 12]
                Layout.alignment: Qt.AlignVCenter
                Layout.preferredHeight: 56
                spacing: 4
                opacity: live ? 1 : 0.4
                Repeater {
                    model: 8
                    delegate: Rectangle {
                        width: 6
                        radius: 3
                        color: theme.accent
                        anchors.bottom: parent.bottom
                        height: bars.live ? 8 + (visualizer.levels[index] || 0) / 100 * 48 : bars.still[index]
                    }
                }
            }
        }

        ChipRow {
            label: qsTr("Mood")
            options: root.moods
            value: wave.mood
            onPicked: function(v) { wave.mood = v }
        }
        ChipRow {
            label: qsTr("Activity")
            options: root.activities
            value: wave.activity
            onPicked: function(v) { wave.activity = v }
        }
        ChipRow {
            label: qsTr("Character")
            options: root.characters
            value: wave.character
            onPicked: function(v) { wave.character = v }
        }
    }
}
