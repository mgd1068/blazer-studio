import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Dialog {
    id: dialog
    objectName: "deviceManagerDialog"
    property var manager: null
    property bool dark: false
    property string chosenId: ""
    readonly property color ink: dark ? "#e2eaf0" : "#263b4a"
    readonly property color muted: dark ? "#adc0ce" : "#5f7687"
    readonly property bool editable: manager && manager.changeBlockReason.length === 0
    readonly property var chosenDetails: {
        if (!manager || manager.instances.length === 0) return ({})
        return manager.details(chosenId)
    }
    readonly property bool dirty: chosenId.length > 0 && (nameInput.text !== (chosenDetails.name || "") || endpointInput.text !== (chosenDetails.endpoint || ""))
    title: "Geräte verwalten"
    anchors.centerIn: parent
    modal: true; focus: true
    width: Math.min(parent.width - 48, 760)
    height: Math.min(parent.height - 48, 650)
    background: Rectangle { color: dialog.dark ? "#202e39" : "white"; radius: 9; border.color: "#708290" }
    function load(id) {
        chosenId = id
        const row = manager ? manager.details(id) : ({})
        nameInput.text = row.name || ""
        endpointInput.text = row.endpoint || ""
    }
    onAboutToShow: { load(manager ? manager.activeId : ""); if (manager) manager.refreshEndpoints() }
    contentItem: ScrollView {
      id: deviceScroll
      clip: true
      contentWidth: availableWidth
      ColumnLayout {
        width: deviceScroll.availableWidth; spacing: 10
        Label {
            Layout.fillWidth: true; color: dialog.ink; wrapMode: Text.WordWrap
            text: "Jeder physische Laser erhält einen eigenen Eintrag – auch zwei gleiche Modelle. Auswählen öffnet keine Verbindung und bestätigt keine Maschinenkalibrierung."
        }
        ListView {
            objectName: "deviceInstanceList"
            Layout.fillWidth: true; Layout.preferredHeight: 140
            clip: true; spacing: 4
            model: dialog.manager ? dialog.manager.instances : []
            ScrollBar.vertical: ScrollBar {}
            delegate: ItemDelegate {
                id: deviceRow
                required property var modelData
                width: ListView.view.width
                highlighted: dialog.chosenId === modelData.id
                text: modelData.name + " · " + modelData.width + " × " + modelData.height + " mm · " + modelData.driver + (dialog.manager.activeId === modelData.id ? " · ausgewählt" : "")
                contentItem: Text { text: deviceRow.text; textFormat: Text.PlainText; color: dialog.ink; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter }
                onClicked: dialog.load(modelData.id)
            }
        }
        Label { visible: dialog.manager && dialog.manager.instances.length === 0; text: "Noch kein physisches Gerät angelegt."; color: dialog.muted }
        Label { text: "Name des einzelnen Geräts"; color: dialog.ink }
        DialogField { dark: dialog.dark; id: nameInput; objectName: "deviceInstanceName"; Layout.fillWidth: true; maximumLength: 100; selectByMouse: true; placeholderText: "z. B. M3 an der linken Werkbank" }
        Label { text: "Lokaler Anschluss (optional für Offline-Projekte)"; color: dialog.ink }
        DialogField { dark: dialog.dark; id: endpointInput; objectName: "deviceInstanceEndpoint"; Layout.fillWidth: true; maximumLength: 512; selectByMouse: true; placeholderText: "z. B. /dev/serial/by-id/…" }
        RowLayout {
            Layout.fillWidth: true
            ToolCombo { dark: dialog.dark; objectName: "discoveredDeviceEndpoints"; Layout.fillWidth: true; model: dialog.manager ? dialog.manager.endpoints : []; displayText: currentIndex >= 0 ? currentText : "Kein Anschluss erkannt"; onActivated: index => endpointInput.text = currentText }
            ActionButton { text: "Suchen"; dark: dialog.dark; enabled: !!dialog.manager; onClicked: dialog.manager.refreshEndpoints() }
        }
        RowLayout {
            Layout.fillWidth: true
            ActionButton {
                objectName: "addCurrentDevice"; text: "Aus Projekt anlegen"; dark: dialog.dark
                enabled: dialog.editable && nameInput.text.trim().length > 0
                hint: "Speichert das aktuelle Projektprofil als neues physisches Gerät. Öffnet keinen Anschluss."
                onClicked: { const id = dialog.manager.addCurrent(nameInput.text, endpointInput.text); if (id.length) dialog.load(id) }
            }
            ActionButton { objectName: "importDeviceProfile"; text: "Profil importieren …"; dark: dialog.dark; enabled: dialog.editable && nameInput.text.trim().length > 0; onClicked: profileFile.open() }
            Item { Layout.fillWidth: true }
        }
        RowLayout {
            Layout.fillWidth: true
            ActionButton { objectName: "saveDeviceInstance"; text: "Änderungen speichern"; dark: dialog.dark; enabled: dialog.editable && dialog.chosenId.length > 0 && nameInput.text.trim().length > 0; onClicked: dialog.manager.update(dialog.chosenId, nameInput.text, endpointInput.text) }
            ActionButton { objectName: "duplicateDeviceInstance"; text: "Duplizieren"; dark: dialog.dark; enabled: dialog.editable && dialog.chosenId.length > 0; onClicked: { const id = dialog.manager.duplicate(dialog.chosenId, nameInput.text.slice(0,94) + " Kopie"); if (id.length) dialog.load(id) } }
            ActionButton { objectName: "removeDeviceInstance"; text: "Entfernen …"; destructive: true; dark: dialog.dark; enabled: dialog.editable && dialog.chosenId.length > 0; onClicked: removeQuestion.open() }
        }
        Label {
            objectName: "deviceManagerMessage"; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: dialog.muted
            textFormat: Text.PlainText
            text: dialog.manager ? (dialog.manager.changeBlockReason || (dialog.dirty ? "Ungespeicherte Änderungen: erst speichern, bevor dieses Gerät ausgewählt wird." : dialog.manager.message)) : "Geräteverwaltung in dieser Ansicht nicht verfügbar."
            Accessible.role: Accessible.StaticText; Accessible.name: text
        }
      }
    }
    footer: RowLayout {
        spacing: 8
        Item { Layout.fillWidth: true }
        ActionButton { objectName: "activateDeviceInstance"; text: "Für dieses Projekt auswählen"; primary: true; enabled: dialog.editable && dialog.chosenId.length > 0 && !dialog.dirty; onClicked: if (dialog.manager.activate(dialog.chosenId)) dialog.close() }
        ActionButton { text: "Schließen"; dark: dialog.dark; onClicked: dialog.close() }
    }
    FileDialog {
        id: profileFile
        objectName: "deviceProfileFileDialog"
        title: "Lokales Geräteprofil importieren"
        nameFilters: ["Geräteprofil (*.json)"]
        onAccepted: { const id = dialog.manager.importProfile(selectedFile, nameInput.text, endpointInput.text); if (id.length) dialog.load(id) }
    }
    Dialog {
        id: removeQuestion
        anchors.centerIn: parent
        title: "Gerätezuordnung entfernen?"
        modal: true
        standardButtons: Dialog.Yes | Dialog.No
        Label { width: 320; wrapMode: Text.WordWrap; text: "Diesen Eintrag samt Anschlusszuordnung entfernen? Projektdateien werden nicht gelöscht." }
        onAccepted: if (dialog.manager.remove(dialog.chosenId)) dialog.load("")
    }
}
