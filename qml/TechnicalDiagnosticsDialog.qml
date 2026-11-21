import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    objectName: "technicalDiagnosticsPreviewDialog"
    property var service: null
    property bool dark: false
    property var preview: ({})
    readonly property color ink: dark ? "#e2eaf0" : "#263b4a"
    signal saveRequested(string digest)
    function refresh() { preview = service ? service.previewTechnicalDiagnostics(controllerContext.checked) : ({}) }
    title: "Diagnosebericht vor dem Speichern prüfen"
    anchors.centerIn: parent; modal: true; focus: true
    width: Math.min(parent.width - 48, 820)
    height: Math.min(parent.height - 48, 680)
    onAboutToShow: { controllerContext.checked = false; refresh() }
    onClosed: { if (service) service.clearTechnicalDiagnosticsPreview(); preview = ({}) }
    background: Rectangle { color: dialog.dark ? "#202e39" : "white"; radius: 9; border.color: "#708290" }
    contentItem: ColumnLayout {
        spacing: 12
        Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: dialog.ink; text: "Der Bericht wird als lokale Datei gespeichert. Es gibt keinen automatischen Versand. Hier siehst du den Inhalt, den du speichern wirst." }
        CheckBox {
            id: controllerContext; objectName: "includeControllerDiagnostics"
            text: "Technischen Controllerkontext zusätzlich aufnehmen"
            Layout.fillWidth: true
            contentItem: Text { text: controllerContext.text; textFormat: Text.PlainText; color: dialog.ink; wrapMode: Text.WordWrap; leftPadding: controllerContext.indicator.width + controllerContext.spacing; verticalAlignment: Text.AlignVCenter }
            onCheckedChanged: if (dialog.visible) dialog.refresh()
        }
        Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: dialog.ink; text: controllerContext.checked ? "Zusätzlich können einzelne Koordinaten, Versätze, numerische Einstellungen und eine begrenzte Befehlschronologie enthalten sein. Quittierungen beweisen keine tatsächliche Bewegung oder Laserleistung." : "Die Standarddiagnose enthält keine Controllerchronologie, Werkzeugwege, Anschlussnamen oder Projektinhalte." }
        ScrollView {
            Layout.fillWidth: true; Layout.fillHeight: true; clip: true
            TextArea {
                objectName: "technicalDiagnosticsPreviewText"
                text: dialog.preview.json || "Keine Vorschau verfügbar."
                textFormat: TextEdit.PlainText; readOnly: true; selectByMouse: true
                wrapMode: TextEdit.NoWrap; font.family: "monospace"; font.pixelSize: 11
                color: dialog.ink
                background: Rectangle { color: dialog.dark ? "#16232c" : "#f1f5f7"; radius: 4 }
            }
        }
        Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; textFormat: Text.PlainText; color: dialog.ink; text: dialog.service ? dialog.service.message : "" }
    }
    footer: RowLayout {
        ActionButton { objectName: "refreshTechnicalDiagnostics"; text: "Vorschau aktualisieren"; dark: dialog.dark; enabled: !!dialog.service; onClicked: dialog.refresh() }
        Item { Layout.fillWidth: true }
        ActionButton { objectName: "saveTechnicalDiagnostics"; text: "Diese Vorschau speichern …"; primary: true; enabled: !!dialog.preview.digest && dialog.preview.digest.length === 64; onClicked: dialog.saveRequested(dialog.preview.digest) }
        ActionButton { objectName: "closeTechnicalDiagnostics"; text: "Schließen"; dark: dialog.dark; onClicked: dialog.close() }
    }
}
