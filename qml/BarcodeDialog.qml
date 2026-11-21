import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    objectName: "barcodeDialog"
    required property var document
    property bool dark: false
    property string error: ""
    property real moduleWidth: 0.3
    property real barHeight: 15
    property real left: 10
    property real bottom: 10
    readonly property color ink: dark ? "#e2eaf0" : "#263b4a"
    readonly property bool valid: /^[\x20-\x7E]{1,128}$/.test(code.text) &&
        Number.isFinite(module.numericValue()) && module.numericValue() >= .1 && module.numericValue() <= 2 &&
        Number.isFinite(heightField.numericValue()) && heightField.numericValue() >= 1 && heightField.numericValue() <= 2000 &&
        Number.isFinite(xField.numericValue()) && xField.numericValue() >= 0 &&
        Number.isFinite(yField.numericValue()) && yField.numericValue() >= 0
    title: "Code128-B erstellen"
    anchors.centerIn: parent; modal: true; focus: true
    width: Math.min(parent.width - 48, 600)
    height: Math.min(parent.height - 48, 500)
    background: Rectangle { color: dialog.dark ? "#202e39" : "white"; radius: 9; border.color: "#708290" }
    onAboutToShow: { error = ""; code.forceActiveFocus() }
    contentItem: ColumnLayout {
        spacing: 10
        Label { Layout.fillWidth: true; text: "Code128-B für 1–128 druckbare ASCII-Zeichen. Das Ergebnis ist bearbeitbare Vektorgeometrie. Links und rechts bleiben jeweils zehn Module als freie Ruhezone reserviert."; color: dialog.ink; wrapMode: Text.WordWrap }
        Label { text: "Zu codierender Text (Leerzeichen werden erhalten)"; color: dialog.ink }
        TextField { id: code; objectName: "barcodeText"; Layout.fillWidth: true; text: "CODE128"; selectByMouse: true }
        NumberField { id: module; objectName: "barcodeModuleWidth"; Layout.fillWidth: true; label: "Modulbreite"; value: dialog.moduleWidth; decimals: 3; unit: "mm"; dark: dialog.dark; onEdited: newValue => dialog.moduleWidth = newValue }
        NumberField { id: heightField; objectName: "barcodeHeight"; Layout.fillWidth: true; label: "Balkenhöhe"; value: dialog.barHeight; unit: "mm"; dark: dialog.dark; onEdited: newValue => dialog.barHeight = newValue }
        NumberField { id: xField; objectName: "barcodeLeft"; Layout.fillWidth: true; label: "Links"; value: dialog.left; unit: "mm"; dark: dialog.dark; onEdited: newValue => dialog.left = newValue }
        NumberField { id: yField; objectName: "barcodeBottom"; Layout.fillWidth: true; label: "Unten"; value: dialog.bottom; unit: "mm"; dark: dialog.dark; onEdited: newValue => dialog.bottom = newValue }
        Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: dialog.ink; text: dialog.valid ? "Gesamtbreite mit Ruhezonen: " + ((55 + 11 * code.text.length) * module.numericValue()).toFixed(2) + " mm. Der vollständige Barcode muss auf die Arbeitsfläche passen." : "Nur ASCII-Zeichen verwenden. Modulbreite 0,1–2 mm, Höhe mindestens 1 mm und Position ab 0 mm." }
        Label { objectName: "barcodeError"; visible: dialog.error.length > 0; Layout.fillWidth: true; wrapMode: Text.WordWrap; textFormat: Text.PlainText; color: "#c05242"; text: dialog.error }
        Item { Layout.fillHeight: true }
    }
    footer: RowLayout {
        Item { Layout.fillWidth: true }
        ActionButton {
            objectName: "createBarcodeButton"; text: "Barcode anlegen"; primary: true; enabled: dialog.valid
            onClicked: {
                if (dialog.document.command("barcode", {text:code.text, moduleWidth:module.numericValue(), height:heightField.numericValue(), x:xField.numericValue(), y:yField.numericValue()})) dialog.close()
                else dialog.error = dialog.document.message
            }
        }
        ActionButton { text: "Abbrechen"; dark: dialog.dark; onClicked: dialog.close() }
    }
}
