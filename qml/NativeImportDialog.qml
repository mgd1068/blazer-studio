import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    objectName: "nativeImportReviewDialog"
    required property var document
    property var modelMachine: null
    property bool dark: false
    property url pendingFile: ""
    property var review: ({})
    property string error: ""
    signal imported()
    readonly property color ink: dark ? "#e2eaf0" : "#263b4a"
    readonly property bool canImport: !!review.geometryValidated && !!review.canImportGeometry && review.sourceSha256.length === 64 && (!modelMachine || (!modelMachine.busy && !modelMachine.firing))
    function inspectFile(file) {
        pendingFile = file; allowFonts.checked = false; error = ""
        refresh(); open()
    }
    function refresh() {
        review = document.previewNativeImport(pendingFile,allowFonts.checked)
        error = review.error || ""
    }
    title: "Natives Projekt vor dem Import prüfen"
    anchors.centerIn: parent; modal: true; focus: true
    width: Math.min(parent.width - 48, 760)
    height: Math.min(parent.height - 48, 650)
    background: Rectangle { color: dialog.dark ? "#202e39" : "white"; radius: 9; border.color: "#708290" }
    contentItem: ColumnLayout {
        spacing: 12
        Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; textFormat: Text.PlainText; color: dialog.ink; text: dialog.pendingFile.toString() }
        Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: dialog.ink; text: "Eine projektgetreue Übernahme ist noch nicht verfügbar. Unterstützte Formen werden nur als Vorlagen übernommen. Leistung, Geschwindigkeit, Quellausgabe und Ursprung werden nicht übernommen; die Bearbeitung wählst du danach selbst." }
        ScrollView {
            id: issuesScroll; Layout.fillWidth: true; Layout.fillHeight: true; clip: true; contentWidth: availableWidth
            ColumnLayout {
                width: issuesScroll.availableWidth; spacing: 10
                Repeater {
                    model: dialog.review.issues || []
                    delegate: Label {
                        required property var modelData
                        Layout.fillWidth: true; wrapMode: Text.WordWrap; textFormat: Text.PlainText
                        color: modelData.severity === "error" ? "#c05242" : dialog.ink
                        text: modelData.label + (modelData.count > 1 ? " (" + modelData.count + "×)" : "")
                    }
                }
            }
        }
        CheckBox {
            id: allowFonts; objectName: "allowNativeFontSubstitution"
            text: "Systemschriften ersatzweise verwenden – Darstellung kann abweichen"
            contentItem: Text { text: allowFonts.text; textFormat: Text.PlainText; color: dialog.ink; wrapMode: Text.WordWrap; leftPadding: allowFonts.indicator.width + allowFonts.spacing; verticalAlignment: Text.AlignVCenter }
            Layout.fillWidth: true
            onCheckedChanged: if (dialog.visible) dialog.refresh()
        }
        Label { objectName: "nativeImportReviewError"; Layout.fillWidth: true; wrapMode: Text.WordWrap; textFormat: Text.PlainText; color: "#c05242"; text: dialog.error; visible: text.length > 0; Accessible.role: Accessible.StaticText; Accessible.name: text }
        Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: dialog.ink; text: dialog.canImport ? "Geometrie geprüft. Neue Vorlagen bleiben ohne Ausgabe, bis du eine Bearbeitung zuweist." : "Der Import ist noch nicht möglich. Nicht unterstützte Darstellung oder externe Ressourcen werden nicht übergangen." }
    }
    footer: RowLayout {
        Item { Layout.fillWidth: true }
        ActionButton { objectName: "refreshNativeImportReview"; text: "Erneut prüfen"; dark: dialog.dark; onClicked: dialog.refresh() }
        ActionButton {
            objectName: "confirmNativeGeometryImport"; text: "Als Vorlagen übernehmen"; primary: true; enabled: dialog.canImport
            onClicked: {
                if (!dialog.canImport) return
                if (dialog.document.importNativeGeometry(dialog.pendingFile,dialog.review.sourceSha256,allowFonts.checked)) {
                    dialog.imported(); dialog.close()
                } else { dialog.error = dialog.document.message; dialog.review = ({}) }
            }
        }
        ActionButton { text: "Abbrechen"; dark: dialog.dark; onClicked: dialog.close() }
    }
}
