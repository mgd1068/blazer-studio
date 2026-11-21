import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Dialog {
    id: dialog
    objectName: "settingsBundleDialog"
    property var service: null
    property bool dark: false
    property var preview: ({})
    property url pendingFile: ""
    property string status: ""
    readonly property color ink: dark ? "#e2eaf0" : "#263b4a"
    title: "Bedieneinstellungen übertragen"
    anchors.centerIn: parent; modal: true; focus: true
    width: Math.min(parent.width - 48, 680)
    height: Math.min(parent.height - 48, 500)
    background: Rectangle { color: dialog.dark ? "#202e39" : "white"; radius: 9; border.color: "#708290" }
    onAboutToShow: { preview = ({}); pendingFile = ""; status = "" }
    function describe(key, value) {
        if (key === "darkTheme") return value ? "Dunkel" : "Hell"
        if (key === "beginnerMode") return value ? "Basic" : "Advanced"
        return String(value)
    }
    function readPreview() {
        preview = service ? service.previewSettingsImport(pendingFile) : ({valid:false,error:"Einstellungen nicht verfügbar."})
        status = preview.valid ? "Vorschau geladen. Erst „Übernehmen“ ändert die Einstellungen." : preview.error
    }
    contentItem: ColumnLayout {
        spacing: 12
        Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: dialog.ink; text: "Übertragen werden Maßeinheit, helles/dunkles Design und Basic/Advanced-Modus. Geräte, Anschlüsse, Zugangsdaten, zuletzt verwendete Dateien, Kalibrierungen und Gerätefreigaben bleiben ausgeschlossen." }
        RowLayout {
            ActionButton { objectName: "exportSettingsBundle"; text: "Einstellungen exportieren …"; dark: dialog.dark; enabled: !!dialog.service; onClicked: exportFile.open() }
            ActionButton { objectName: "chooseSettingsBundle"; text: "Importdatei wählen …"; dark: dialog.dark; enabled: !!dialog.service; onClicked: importFile.open() }
        }
        Label { visible: !!dialog.preview.valid; text: "Vorgesehene Änderungen"; color: dialog.ink; font.bold: true }
        Repeater {
            model: dialog.preview.valid ? dialog.preview.changes : []
            delegate: Label {
                required property var modelData
                Layout.fillWidth: true; wrapMode: Text.WordWrap; textFormat: Text.PlainText; color: dialog.ink
                text: modelData.label + ": " + dialog.describe(modelData.key,modelData.current) + " → " + dialog.describe(modelData.key,modelData.incoming)
            }
        }
        Label { visible: !!dialog.preview.valid && dialog.preview.changes.length === 0; text: "Die drei Bedieneinstellungen stimmen bereits überein."; color: dialog.ink }
        Label { objectName: "settingsBundleStatus"; Layout.fillWidth: true; wrapMode: Text.WordWrap; textFormat: Text.PlainText; color: dialog.ink; text: dialog.status; Accessible.role: Accessible.StaticText; Accessible.name: text }
        Item { Layout.fillHeight: true }
    }
    footer: RowLayout {
        Item { Layout.fillWidth: true }
        ActionButton { text: "Vorschau aktualisieren"; dark: dialog.dark; enabled: dialog.pendingFile.toString().length > 0; onClicked: dialog.readPreview() }
        ActionButton {
            objectName: "applySettingsBundle"; text: "Diese Einstellungen übernehmen"; primary: true
            enabled: !!dialog.preview.valid && dialog.service && dialog.preview.digest.length === 64
            onClicked: {
                if (dialog.service.importSettings(dialog.pendingFile, dialog.preview.digest)) { dialog.status = "Bedieneinstellungen übernommen."; dialog.preview = ({}) }
                else { dialog.status = dialog.service.message; dialog.preview = ({}) }
            }
        }
        ActionButton { text: "Schließen"; dark: dialog.dark; onClicked: dialog.close() }
    }
    FileDialog {
        id: importFile; objectName: "settingsImportFileDialog"; title: "Bedieneinstellungen zur Vorschau öffnen"; nameFilters: ["Blazer-Bedieneinstellungen (*.json)"]
        onAccepted: { dialog.pendingFile = selectedFile; dialog.readPreview() }
    }
    FileDialog {
        id: exportFile; objectName: "settingsExportFileDialog"; title: "Bedieneinstellungen exportieren"; nameFilters: ["Blazer-Bedieneinstellungen (*.json)"]; fileMode: FileDialog.SaveFile; defaultSuffix: "json"
        onAccepted: dialog.status = dialog.service.exportSettings(selectedFile) ? "Bedieneinstellungen exportiert." : dialog.service.message
    }
}
