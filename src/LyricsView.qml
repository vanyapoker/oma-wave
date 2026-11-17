import QtQuick

// Scrolling lyrics list that keeps the current line centred.
ListView {
    id: view
    property int fontSize: 15
    clip: true
    model: lyrics.lines
    delegate: Text {
        readonly property bool active: lyrics.state === "synced" && index === lyrics.currentIndex
        width: view.width - 32
        x: 16
        topPadding: 6
        bottomPadding: 6
        wrapMode: Text.Wrap
        horizontalAlignment: lyrics.state === "synced" ? Text.AlignHCenter : Text.AlignLeft
        text: modelData.text
        color: active ? theme.accent : lyrics.state === "synced" ? "#8a8a92" : "white"
        font.pixelSize: active ? view.fontSize + 3 : view.fontSize
        font.bold: active
        textFormat: Text.PlainText
    }
    Connections {
        target: lyrics
        function onCurrentIndexChanged() {
            if (lyrics.currentIndex >= 0)
                view.positionViewAtIndex(lyrics.currentIndex, ListView.Center)
        }
    }
}
