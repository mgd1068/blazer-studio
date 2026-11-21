import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    objectName: "materialTestRecordDialog"
    property var manager: null
    property var ledger: null
    required property var document
    property bool dark: false
    property string bindingAtOpen: ""
    property string parametersAtOpen: ""
    property string error: ""
    property string status: ""
    property real thicknessMm: 0
    readonly property color ink: dark ? "#e2eaf0" : "#263b4a"
    readonly property bool sameContext: manager && bindingAtOpen.length > 0 && manager.bindingKey === bindingAtOpen && manager.parameterHash === parametersAtOpen
    readonly property var matchingRecords: {
        if (!ledger || !manager || ledger.records.length === 0) return []
        return ledger.applicable(bindingAtOpen, parametersAtOpen)
    }
    title: "Materialversuch dokumentieren"
    anchors.centerIn: parent; modal: true; focus: true
    width: Math.min(parent.width - 48, 720)
    height: Math.min(parent.height - 48, 700)
    background: Rectangle { color: dialog.dark ? "#202e39" : "white"; radius: 9; border.color: "#708290" }
    onAboutToShow: {
        bindingAtOpen = manager ? manager.bindingKey : ""
        parametersAtOpen = manager ? manager.parameterHash : ""
        materialInput.text = document.material.name || ""
        thicknessMm = Number(document.material.thickness) || 0
        dateInput.text = Qt.formatDate(new Date(), "yyyy-MM-dd")
        batchInput.text = ""; notesInput.text = ""; resultChoice.currentIndex = 2; error = ""; status = ""
    }
    contentItem: ScrollView {
        id: materialScroll
        clip: true
        contentWidth: availableWidth
        ColumnLayout {
            width: materialScroll.availableWidth; spacing: 10
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: dialog.ink; text: "Hier wird deine Beobachtung gespeichert. Ein alter Eintrag bestätigt weder das jetzt eingelegte Material noch Maschinenreferenz oder Absaugung. Die Startbestätigung bleibt eine eigene Handlung." }
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; textFormat: Text.PlainText; color: dialog.ink; text: dialog.sameContext ? "Gerät: " + dialog.manager.active.name : "Kein unverändertes physisches Gerät ausgewählt. Gerätezuordnung und Parameter prüfen und diesen Dialog erneut öffnen." }
            Label { text: "Material"; color: dialog.ink }
            DialogField { dark: dialog.dark; id: materialInput; objectName: "materialObservationName"; Layout.fillWidth: true; maximumLength: 200; color: dialog.ink }
            RowLayout {
                Layout.fillWidth: true
                Label { text: "Datum"; color: dialog.ink }
                DialogField { dark: dialog.dark; id: dateInput; objectName: "materialObservationDate"; Layout.fillWidth: true; Layout.minimumWidth: 90; maximumLength: 10; placeholderText: "YYYY-MM-DD"; color: dialog.ink }
                Label { text: "Charge"; color: dialog.ink }
                DialogField { dark: dialog.dark; id: batchInput; objectName: "materialObservationBatch"; Layout.fillWidth: true; Layout.minimumWidth: 70; maximumLength: 200; color: dialog.ink }
            }
            NumberField { id: thickness; objectName: "materialObservationThickness"; Layout.fillWidth: true; label: "Dicke"; value: dialog.thicknessMm; unit: "mm"; dark: dialog.dark; onEdited: newValue => dialog.thicknessMm = newValue }
            ComboBox { id: resultChoice; objectName: "materialObservationResult"; Layout.fillWidth: true; model: ["Brauchbares Ergebnis", "Versuch fehlgeschlagen", "Nicht eindeutig / nicht bewertet"] }
            Label { text: "Beobachtung und Ergebnis"; color: dialog.ink }
            ScrollView {
                id: notesScroll; Layout.fillWidth: true; Layout.preferredHeight: 100; clip: true; contentWidth: availableWidth
                TextArea {
                    id: notesInput; objectName: "materialObservationNotes"; width: notesScroll.availableWidth
                    wrapMode: TextEdit.Wrap; selectByMouse: true; color: dialog.ink
                    placeholderText: "Was hast du tatsächlich beobachtet?"; placeholderTextColor: dialog.dark ? "#a2b8c7" : "#667e90"
                    background: Rectangle { color: dialog.dark ? "#25323d" : "#f7f9fb"; border.color: "#708290"; radius: 5 }
                }
            }
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; textFormat: Text.PlainText; color: dialog.error.length ? "#c05242" : dialog.ink; text: dialog.error || dialog.status || (dialog.ledger && !dialog.ledger.writable ? dialog.ledger.message : ""); visible: text.length > 0 }
            Label { objectName: "materialMatchingRecordCount"; text: "Passende historische Einträge: " + dialog.matchingRecords.length; color: dialog.ink }
            Repeater {
                model: dialog.matchingRecords.slice(0, 5)
                delegate: Label {
                    required property var modelData
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; textFormat: Text.PlainText; color: dialog.ink
                    text: modelData.observation.date + " · " + modelData.observation.material + " · " + modelData.observation.thicknessMm + " mm · " + ({usable:"brauchbar",failed:"fehlgeschlagen",inconclusive:"nicht eindeutig"}[modelData.observation.result]) + "\n" + modelData.observation.notes.slice(0, 180)
                }
            }
        }
    }
    footer: RowLayout {
        Item { Layout.fillWidth: true }
        ActionButton {
            objectName: "saveMaterialObservation"; text: "Beobachtung speichern"; primary: true
            enabled: dialog.sameContext && dialog.ledger && dialog.ledger.writable && materialInput.text.trim().length > 0 && notesInput.text.trim().length > 0 && notesInput.text.length <= 4000 && Number.isFinite(thickness.numericValue()) && thickness.numericValue() > 0 && thickness.numericValue() <= 1000
            onClicked: {
                if (!dialog.sameContext) { dialog.error = "Gerät oder Parameter haben sich geändert. Dialog neu öffnen."; return }
                const id = dialog.ledger.record(dialog.bindingAtOpen, dialog.parametersAtOpen,
                    {date:dateInput.text, material:materialInput.text, batch:batchInput.text, thicknessMm:thickness.numericValue(), result:["usable","failed","inconclusive"][resultChoice.currentIndex], notes:notesInput.text})
                if (id.length) { dialog.error = ""; dialog.status = "Beobachtung gespeichert."; notesInput.text = "" }
                else dialog.error = dialog.ledger.message
            }
        }
        ActionButton { text: "Schließen"; dark: dialog.dark; onClicked: dialog.close() }
    }
}
