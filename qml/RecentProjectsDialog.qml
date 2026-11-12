import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    objectName: "recentProjectsDialog"
    property var service: null
    property bool dark: false
    readonly property color ink: dark ? "#e2eaf0" : "#263b4a"
    signal projectRequested(url file)
    title: "Zuletzt verwendete Projekte"
    modal: true
    focus: true
    anchors.centerIn: parent
    width: Math.min(parent.width - 48, 820)
    height: Math.min(parent.height - 48, 560)
    background: Rectangle { color: dialog.dark ? "#202e39" : "white"; radius: 9; border.color: "#708290" }
    onAboutToShow: if (service) service.refreshRecentProjects()
    contentItem: ColumnLayout {
        spacing: 12
        Label { Layout.fillWidth: true; text: "Die letzten zehn erfolgreich geöffneten oder gespeicherten Projekte. Der Verlauf bleibt auf diesem Computer."; color: dialog.ink; wrapMode: Text.WordWrap }
        ListView {
            id: files
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 4
            model: dialog.service ? dialog.service.recentProjects : []
            ScrollBar.vertical: ScrollBar {}
            delegate: ItemDelegate {
                id: entry
                required property var modelData
                required property int index
                objectName: "recentProject" + index
                width: files.width
                height: 64
                enabled: modelData.available
                Accessible.name: modelData.name + ", " + modelData.path
                contentItem: Column {
                    spacing: 4
                    Text { width: parent.width; text: entry.modelData.name + (entry.modelData.available ? "" : " · nicht gefunden"); textFormat: Text.PlainText; elide: Text.ElideRight; color: dialog.ink; font.bold: true }
                    Text { width: parent.width; text: entry.modelData.path; textFormat: Text.PlainText; elide: Text.ElideMiddle; color: dialog.ink }
                }
                onClicked: { dialog.close(); dialog.projectRequested(modelData.url) }
            }
            Label { anchors.centerIn: parent; visible: files.count === 0; text: "Noch keine Projekte im Verlauf."; color: dialog.ink }
        }
        Label { Layout.fillWidth: true; text: dialog.service ? dialog.service.message : ""; color: dialog.ink; wrapMode: Text.WordWrap; visible: text.length > 0 }
    }
    footer: DialogButtonBox {
        ActionButton { objectName: "clearRecentProjects"; text: "Verlauf leeren"; dark: dialog.dark; enabled: files.count > 0; hint: "Entfernt die gespeicherte Liste. Projektdateien werden nicht gelöscht."; onClicked: dialog.service.clearRecentProjects() }
        ActionButton { objectName: "closeRecentProjects"; text: "Schließen"; dark: dialog.dark; onClicked: dialog.close() }
    }
}
