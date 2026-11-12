import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    objectName: "historyDialog"
    required property var document
    property bool dark: false
    property var entries: []
    property var revision: 0
    property int currentPosition: 0
    property int selectedPosition: 0
    property bool stale: false
    readonly property color ink: dark ? "#e2eaf0" : "#263b4a"
    title: "Bearbeitungsverlauf"
    anchors.centerIn: parent
    width: Math.min(parent.width-48,760)
    height: Math.min(parent.height-48,560)
    modal: true
    focus: true
    background: Rectangle { radius: 9; color: dialog.dark ? "#202e39" : "white"; border.color: "#708290" }
    function reload() {
        // Keep a deliberate list snapshot. Planning progress must not reset
        // the selection; a changed history token rejects stale actions.
        entries = document.history
        revision = document.historyRevision
        currentPosition = document.historyPosition
        selectedPosition = currentPosition
        stale = false
        Qt.callLater(()=>historyList.positionViewAtIndex(currentPosition,ListView.Center))
    }
    function changeTo(position) {
        let accepted=document.restoreHistory(position,revision)
        reload()
        stale=!accepted
    }
    onAboutToShow: {document.finishMove();reload()}
    Shortcut { sequences: [StandardKey.Undo]; enabled: dialog.visible && dialog.currentPosition>0; onActivated: dialog.changeTo(dialog.currentPosition-1) }
    Shortcut { sequences: [StandardKey.Redo]; enabled: dialog.visible && dialog.currentPosition<dialog.entries.length-1; onActivated: dialog.changeTo(dialog.currentPosition+1) }
    contentItem: ColumnLayout {
        spacing: 10
        Label { text: "Bis zu 50 Bearbeitungsschritte dieser Sitzung. Dateiinhalt markiert den zuletzt gespeicherten Inhalt; die Datei wird beim Wechsel nicht geschrieben."; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: dialog.ink; Layout.fillWidth: true }
        Label { objectName: "historyStaleMessage"; visible: dialog.stale; text: "Der Verlauf hat sich geändert. Die Liste wurde aktualisiert; bitte den gewünschten Stand erneut wählen."; wrapMode: Text.Wrap; color: "#cf645b"; Layout.fillWidth: true }
        ListView {
            id: historyList
            objectName: "historyList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 4
            model: dialog.entries
            ScrollBar.vertical: ScrollBar {}
            delegate: Button {
                id: historyRow
                required property var modelData
                objectName: "historyRow"+modelData.position
                width: historyList.width
                height: 56
                padding: 10
                onClicked: dialog.selectedPosition=modelData.position
                background: Rectangle { radius: 5; color: dialog.selectedPosition===historyRow.modelData.position ? (dialog.dark ? "#315446" : "#e6f3ec") : dialog.dark ? "#192832" : "#f4f7f9"; border.color: historyRow.modelData.current ? "#209b76" : "transparent" }
                contentItem: ColumnLayout {
                    spacing: 3
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: historyRow.modelData.position+" · "+historyRow.modelData.label; textFormat: Text.PlainText; color: dialog.ink; elide: Text.ElideRight; Layout.fillWidth: true }
                        Text { text: historyRow.modelData.current ? "Aktuell" : ""; color: "#209b76" }
                        Text { text: historyRow.modelData.saved ? "Dateiinhalt" : ""; color: dialog.ink }
                    }
                    Text { text: "Objekte: "+historyRow.modelData.objects+" · Blätter: "+historyRow.modelData.pages+" · aktives Blatt: "+(historyRow.modelData.activePage+1); color: dialog.ink; opacity: .7; font.pixelSize: 11 }
                }
            }
        }
    }
    footer: DialogButtonBox {
        ActionButton { objectName: "historyUndo"; text: "Rückgängig"; dark: dialog.dark; enabled: dialog.currentPosition>0; onClicked: dialog.changeTo(dialog.currentPosition-1) }
        ActionButton { objectName: "historyRedo"; text: "Wiederholen"; dark: dialog.dark; enabled: dialog.currentPosition<dialog.entries.length-1; onClicked: dialog.changeTo(dialog.currentPosition+1) }
        ActionButton { objectName: "historyApply"; text: "Zu Stand wechseln"; primary: true; enabled: dialog.selectedPosition!==dialog.currentPosition; onClicked: dialog.changeTo(dialog.selectedPosition) }
        ActionButton { objectName: "historyClose"; text: "Schließen"; dark: dialog.dark; onClicked: dialog.close() }
    }
}
