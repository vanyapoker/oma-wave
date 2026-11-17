import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Page {
    id: root
    background: null
    readonly property var results: catalog.searchResults

    function focusSearch() { field.forceActiveFocus(); field.selectAll() }
    StackView.onActivated: focusSearch()

    Timer {
        id: debounce
        interval: 300
        onTriggered: catalog.search(field.text)
    }

    header: Item {
        height: 64
        TextField {
            id: field
            anchors.fill: parent
            anchors.margins: 12
            placeholderText: qsTr("Search songs, albums, artists")
            onTextChanged: debounce.restart()
            onAccepted: { debounce.stop(); catalog.search(text) }
            Keys.onDownPressed: songs.forceActiveFocus()
        }
    }

    Flickable {
        anchors.fill: parent
        anchors.leftMargin: 20
        anchors.rightMargin: 20
        contentHeight: column.height + 20
        clip: true

        Column {
            id: column
            width: parent.width
            spacing: 18

            Column {
                visible: !!root.results.top
                spacing: 8
                Text { text: qsTr("Top result"); color: "white"; font.pixelSize: 18; font.bold: true; textFormat: Text.PlainText }
                CardTile { item: root.results.top || ({}) }
            }

            Column {
                id: songs
                width: parent.width
                visible: (root.results.songs || []).length > 0
                Text { text: qsTr("Songs"); color: "white"; font.pixelSize: 18; font.bold: true; textFormat: Text.PlainText }
                Repeater {
                    model: root.results.songs || []
                    delegate: TrackRow {
                        width: column.width
                        track: modelData
                        radio: true
                    }
                }
            }

            CardRow { width: column.width; title: qsTr("Albums"); items: root.results.albums }
            CardRow { width: column.width; title: qsTr("Artists"); items: root.results.artists }
            CardRow { width: column.width; title: qsTr("Playlists"); items: root.results.playlists }
        }
    }

    BusyIndicator {
        anchors.centerIn: parent
        running: catalog.busy && !root.results.songs
    }
}
