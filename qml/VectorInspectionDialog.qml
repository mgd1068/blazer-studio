import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    objectName: "vectorInspectionDialog"
    property var service: null
    property bool dark: false
    readonly property color ink: dark ? "#e2eaf0" : "#263b4a"
    title: "Vektorgeometrie prüfen"
    anchors.centerIn: parent; modal: true; focus: true
    width: Math.min(parent.width - 48, 760)
    height: Math.min(parent.height - 48, 650)
    background: Rectangle { color: dialog.dark ? "#202e39" : "white"; radius: 9; border.color: "#708290" }
    contentItem: ColumnLayout {
        spacing: 12
        Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: dialog.ink; text: "Diese Prüfung untersucht die Zeichnungsgeometrie. Hinweise ändern oder löschen nichts. Ausgabeplatzierung, Material und sichere Maschinenbewegung müssen separat geprüft werden." }
        RowLayout {
            ActionButton { objectName: "inspectAllVectors"; text: "Ausgebende Vektoren prüfen"; dark: dialog.dark; enabled: dialog.service && !dialog.service.busy; onClicked: dialog.service.inspect(false) }
            ActionButton { objectName: "inspectSelectedVectors"; text: "Auswahl prüfen"; dark: dialog.dark; enabled: dialog.service && !dialog.service.busy; onClicked: dialog.service.inspect(true) }
            ActionButton { objectName: "cancelVectorInspection"; text: "Prüfung abbrechen"; dark: dialog.dark; visible: dialog.service && dialog.service.busy; onClicked: dialog.service.cancel() }
        }
        Label { objectName: "vectorInspectionStatus"; Layout.fillWidth: true; wrapMode: Text.WordWrap; textFormat: Text.PlainText; color: dialog.ink; text: dialog.service ? dialog.service.message : "Vektorprüfung in dieser Ansicht nicht verfügbar."; Accessible.role: Accessible.StaticText; Accessible.name: text }
        Label { visible: dialog.service && !dialog.service.busy && Object.keys(dialog.service.report).length > 0; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: dialog.ink; text: dialog.service && dialog.service.report.complete ? "Prüfung abgeschlossen: " + dialog.service.report.inspectedObjects + " Objekte. Gefundene Auffälligkeiten sind Hinweise, keine automatische Sicherheitsbewertung." : "Prüfung nicht vollständig. Aus diesem Ergebnis darf keine Fehlerfreiheit abgeleitet werden." }
        ScrollView {
            id: findingsScroll; Layout.fillWidth: true; Layout.fillHeight: true; clip: true; contentWidth: availableWidth
            ColumnLayout {
                width: findingsScroll.availableWidth; spacing: 12
                Repeater {
                    model: dialog.service ? (dialog.service.report.findings || []) : []
                    delegate: Label {
                        required property var modelData
                        Layout.fillWidth: true; wrapMode: Text.WordWrap; textFormat: Text.PlainText; color: dialog.ink
                        text: modelData.label + "\nPosition in der Zeichnung: X " + Number(modelData.xMm).toFixed(3) + " mm · Y " + Number(modelData.yMm).toFixed(3) + " mm"
                    }
                }
            }
        }
    }
    footer: RowLayout {
        Item { Layout.fillWidth: true }
        ActionButton { text: "Schließen"; dark: dialog.dark; onClicked: dialog.close() }
    }
}
