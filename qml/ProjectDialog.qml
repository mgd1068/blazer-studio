import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    objectName: "projectDetailsDialog"
    required property var document
    property bool dark: false
    property bool failed: false
    readonly property color ink: dark ? "#e2eaf0" : "#263b4a"
    readonly property bool valid: nameEdit.text.trim().length > 0 && nameEdit.text.length <= 1000 && notesEdit.text.length <= 16000
    title: "Projektangaben"
    anchors.centerIn: parent
    modal: true
    focus: true
    width: Math.min(parent.width - 48, 720)
    height: Math.min(parent.height - 48, 560)
    background: Rectangle { color: dialog.dark ? "#202e39" : "white"; radius: 9; border.color: "#708290" }
    onAboutToShow: {
        failed = false
        nameEdit.text = document.title
        notesEdit.text = document.notes
        nameEdit.forceActiveFocus()
    }
    contentItem: ColumnLayout {
        spacing: 12
        Label { text: "Projektname"; color: dialog.ink }
        TextField {
            id: nameEdit
            objectName: "projectNameEditor"
            Layout.fillWidth: true
            maximumLength: 1000
            selectByMouse: true
            Accessible.name: "Projektname"
        }
        Label { text: "Notizen"; color: dialog.ink }
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            TextArea {
                id: notesEdit
                objectName: "projectNotesEditor"
                textFormat: TextEdit.PlainText
                wrapMode: TextEdit.Wrap
                selectByMouse: true
                Accessible.name: "Projektnotizen"
                placeholderText: "Zum Beispiel Material, Dicke, Fokus, Versuch und Ergebnis …"
            }
        }
        Label { text: notesEdit.text.length + " / 16000 Zeichen"; color: dialog.ink }
        Label {
            Layout.fillWidth: true
            text: "Notizen gehören zur Projektdatei. Sie ändern keine Bearbeitungswerte und werden nicht an den Laser ausgegeben."
            wrapMode: Text.WordWrap
            color: dialog.ink
        }
        Label {
            Layout.fillWidth: true
            text: "Neu gespeicherte Projekte benötigen diesen neueren Blazer-Stand. Beta.2 kann das neue Dateiformat nicht öffnen."
            wrapMode: Text.WordWrap
            color: dialog.ink
        }
        Label {
            Layout.fillWidth: true
            visible: dialog.failed
            text: dialog.document.message
            wrapMode: Text.WordWrap
            color: "#c26750"
        }
    }
    footer: DialogButtonBox {
        ActionButton { objectName: "cancelProjectDetails"; text: "Abbrechen"; dark: dialog.dark; onClicked: dialog.reject() }
        ActionButton {
            objectName: "applyProjectDetails"
            text: "Übernehmen"
            dark: dialog.dark
            primary: true
            enabled: dialog.valid
            onClicked: {
                if (dialog.document.setMetadata(nameEdit.text, notesEdit.text)) dialog.accept()
                else dialog.failed = true
            }
        }
    }
}
