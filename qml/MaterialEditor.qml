import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: editor
    objectName: "materialEditor"
    property var library
    property bool dark: false
    property var draft: ({})
    readonly property color ink: dark ? "#e2eaf0" : "#263b4a"
    readonly property color muted: dark ? "#9bafbd" : "#708290"
    signal saved(string profileId)
    title: "Materialprofil bearbeiten"
    anchors.centerIn: parent
    width: Math.min(parent.width - 48, 740)
    height: Math.min(parent.height - 48, 690)
    modal: true
    function edit(profile, asNew) {
        draft = JSON.parse(JSON.stringify(profile))
        if (asNew) { draft.id = ""; draft.name = profile.name + " · Kopie" }
        nameField.text = draft.name
        modelField.text = draft.model
        notesField.text = draft.notes
        powerField.value = draft.moduleW
        thicknessField.value = draft.thicknessMm
        engraving.enabledValue = draft.engrave.enabled
        engraving.speedValue = draft.engrave.speed
        engraving.powerValue = draft.engrave.power
        engraving.passesValue = draft.engrave.passes
        engraving.intervalValue = draft.engrave.interval
        cutting.enabledValue = draft.cut.enabled
        cutting.speedValue = draft.cut.speed
        cutting.powerValue = draft.cut.power
        cutting.passesValue = draft.cut.passes
        cutting.intervalValue = draft.cut.interval
        powerField.resetInput(); thicknessField.resetInput()
        engraving.resetInputs(); cutting.resetInputs()
        errorText.text = ""
        open()
    }
    function commit() {
        let profile = JSON.parse(JSON.stringify(draft))
        profile.name = nameField.text
        profile.model = modelField.text
        profile.notes = notesField.text
        profile.moduleW = powerField.numericValue()
        profile.thicknessMm = thicknessField.numericValue()
        profile.engrave = engraving.values()
        profile.cut = cutting.values()
        // Invalid numeric text is rejected before QVariant conversion can turn NaN into null/zero.
        if (!isFinite(profile.moduleW) || !isFinite(profile.thicknessMm) || !engraving.validInput() || !cutting.validInput()) {
            errorText.text = "Bitte alle Zahlenfelder gültig ausfüllen."
            return
        }
        let profileId = library.saveProfile(profile)
        if (!profileId) { errorText.text = library.message; return }
        saved(profileId)
        close()
    }
    background: Rectangle { radius: 9; color: editor.dark ? "#202e39" : "white"; border.color: editor.dark ? "#44545f" : "#dce4e9" }
    component OperationFields: ColumnLayout {
        id: fields
        property string label
        property bool engravingMode: false
        property bool enabledValue: true
        property real speedValue: 3000
        property real powerValue: 20
        property real passesValue: 1
        property real intervalValue: .1
        function resetInputs() { speedInput.resetInput(); powerInput.resetInput(); passesInput.resetInput(); intervalInput.resetInput() }
        function values() { return { enabled: outputSwitch.checked, speed: speedInput.numericValue(), power: powerInput.numericValue(), passes: passesInput.numericValue(), interval: intervalInput.numericValue() } }
        function validInput() { let v = values(); return isFinite(v.speed) && isFinite(v.power) && isFinite(v.passes) && isFinite(v.interval) }
        spacing: 9
        RowLayout {
            Text { text: fields.label; color: editor.ink; font.pixelSize: 13; font.bold: true; Layout.fillWidth: true }
            Switch { id: outputSwitch; checked: fields.enabledValue; onToggled: fields.enabledValue = checked }
        }
        NumberField { resetOnFinish: false; id: speedInput; objectName: fields.engravingMode ? "materialEngraveSpeed" : "materialCutSpeed"; label: "v"; value: fields.speedValue; onEdited: n => fields.speedValue = n; unit: "mm/min"; decimals: 0; dark: editor.dark; Layout.fillWidth: true }
        NumberField { resetOnFinish: false; id: powerInput; objectName: fields.engravingMode ? "materialEngravePower" : "materialCutPower"; label: "P"; value: fields.powerValue; onEdited: n => fields.powerValue = n; unit: "%"; dark: editor.dark; Layout.fillWidth: true }
        NumberField { resetOnFinish: false; id: passesInput; label: "×"; value: fields.passesValue; onEdited: n => fields.passesValue = n; unit: "Mal"; decimals: 0; dark: editor.dark; Layout.fillWidth: true }
        NumberField { resetOnFinish: false; id: intervalInput; visible: fields.engravingMode; label: "↕"; value: fields.intervalValue; onEdited: n => fields.intervalValue = n; unit: "mm"; decimals: 3; dark: editor.dark; Layout.fillWidth: true }
    }
    contentItem: ScrollView {
        id: scroller
        clip: true; contentWidth: availableWidth
        ColumnLayout {
            width: scroller.availableWidth
            spacing: 14
            Text { text: "Eigene Bibliothek · Speichern übernimmt den Profilstand ins aktuelle Projekt. Die Herstellerreferenz bleibt unverändert."; wrapMode: Text.WordWrap; color: editor.muted; font.pixelSize: 11; Layout.fillWidth: true }
            TextField { id: nameField; objectName: "materialNameInput"; placeholderText: "Materialname"; Layout.fillWidth: true; selectByMouse: true }
            RowLayout {
                Layout.fillWidth: true; spacing: 12
                TextField { id: modelField; placeholderText: "Maschinenmodell"; Layout.fillWidth: true; selectByMouse: true }
                NumberField { resetOnFinish: false; id: powerField; onEdited: n => value = n; label: ""; unit: "W"; value: 5; dark: editor.dark; Layout.preferredWidth: 130 }
                NumberField { resetOnFinish: false; id: thicknessField; onEdited: n => value = n; label: ""; unit: "mm"; value: 3; dark: editor.dark; Layout.preferredWidth: 130 }
            }
            Text { text: "Modell / Modul / Materialstärke"; color: editor.muted; font.pixelSize: 10 }
            RowLayout {
                Layout.fillWidth: true; spacing: 26
                OperationFields { id: engraving; label: "Gravieren"; engravingMode: true; Layout.fillWidth: true; Layout.alignment: Qt.AlignTop }
                OperationFields { id: cutting; label: "Schneiden"; Layout.fillWidth: true; Layout.alignment: Qt.AlignTop }
            }
            TextArea { id: notesField; placeholderText: "Notizen, Fokus, Materialtest und Besonderheiten"; Layout.fillWidth: true; Layout.preferredHeight: 92; wrapMode: TextEdit.Wrap }
            Text { text: "Quelle: " + (editor.draft.source || ""); color: editor.muted; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Text { id: errorText; objectName: "materialEditorError"; color: "#cf645b"; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        }
    }
    footer: DialogButtonBox {
        ActionButton { text: "Abbrechen"; dark: editor.dark; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole; onClicked: editor.close() }
        ActionButton { objectName: "saveMaterialButton"; text: "Profil speichern & übernehmen"; primary: true; onClicked: editor.commit() }
    }
}
