import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Page {
    id: root
    background: null
    readonly property var results: catalog.searchResults
    readonly property bool searched: root.results.songs !== undefined
    readonly property bool empty: !root.results.top && ["songs", "albums", "artists", "playlists"].every(k => (root.results[k] || []).length === 0)

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
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            anchors.topMargin: 12
            anchors.bottomMargin: 12
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

    Text {
        anchors.centerIn: parent
        width: parent.width - 80
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        visible: !catalog.busy && (field.text.trim() === "" || (root.searched && root.empty))
        text: field.text.trim() === "" ? qsTr("Find songs, albums, artists and playlists")
            : qsTr("No results for “%1”").arg(field.text.trim())
        color: "#a0a0a8"
        font.pixelSize: 15
        textFormat: Text.PlainText
    }

    BusyIndicator {
        anchors.centerIn: parent
        running: catalog.busy && !root.results.songs
    }
}
