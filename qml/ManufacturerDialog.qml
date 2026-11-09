import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    objectName: "manufacturerDialog"
    property var catalog: ({tables: []})
    property bool dark: false
    readonly property color ink: dark ? "#e2eaf0" : "#263b4a"
    readonly property color muted: dark ? "#9bafbd" : "#708290"
    readonly property var selectedTable: catalog.tables && catalog.tables.length > selector.currentIndex && selector.currentIndex >= 0 ? catalog.tables[selector.currentIndex] : null
    title: "Herstellerreferenz · M3 Pro"
    anchors.centerIn: parent
    width: Math.min(parent.width - 48, 760)
    height: Math.min(parent.height - 48, 590)
    modal: true
    standardButtons: Dialog.Close
    onOpened: standardButton(Dialog.Close).text = "Schließen"
    background: Rectangle { radius: 9; color: dialog.dark ? "#202e39" : "white"; border.color: dialog.dark ? "#44545f" : "#dce4e9" }
    contentItem: ColumnLayout {
        spacing: 12
        Text {
            Layout.fillWidth: true
            text: "M3 Pro ist die Modellangabe der Quelle. Die Zuordnung zum vorhandenen M3 ist noch offen. Diese Ansicht übernimmt keine Einstellungen."
            wrapMode: Text.WordWrap; color: dialog.muted; font.pixelSize: 11
        }
        ComboBox {
            id: selector
            objectName: "manufacturerTableSelector"
            Layout.fillWidth: true
            model: dialog.catalog.tables ? dialog.catalog.tables.map(t => t.sourceTitle) : []
            onCurrentIndexChanged: if (rows) rows.positionViewAtBeginning()
        }
        RowLayout {
            Layout.fillWidth: true; spacing: 8
            Text { text: "Material"; color: dialog.muted; font.pixelSize: 10; Layout.fillWidth: true }
            Text { text: "Leistung"; color: dialog.muted; font.pixelSize: 10; Layout.preferredWidth: 66 }
            Text { text: "mm/min"; color: dialog.muted; font.pixelSize: 10; Layout.preferredWidth: 92 }
            Text { text: dialog.selectedTable && dialog.selectedTable.operation === "engrave" ? "Intervall / mm" : "Durchgänge"; color: dialog.muted; font.pixelSize: 10; Layout.preferredWidth: 96 }
        }
        ListView {
            id: rows
            objectName: "manufacturerRows"
            Layout.fillWidth: true; Layout.fillHeight: true
            model: dialog.selectedTable ? dialog.selectedTable.entries : []
            clip: true; spacing: 1
            ScrollBar.vertical: ScrollBar { }
            delegate: Rectangle {
                required property var modelData
                required property int index
                width: rows.width; height: 33
                color: index % 2 === 0 ? (dialog.dark ? "#253541" : "#f4f7f9") : "transparent"
                radius: 3
                RowLayout {
                    anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8; spacing: 8
                    Text { text: modelData.material; color: dialog.ink; font.pixelSize: 11; elide: Text.ElideRight; Layout.fillWidth: true }
                    Text { text: modelData.powerPercent + " %"; color: dialog.ink; font.pixelSize: 11; Layout.preferredWidth: 66 }
                    Text { text: modelData.speedMmMin; color: dialog.ink; font.pixelSize: 11; Layout.preferredWidth: 92 }
                    Text { text: dialog.selectedTable.operation === "engrave" ? modelData.intervalMm.toFixed(2) : modelData.passes; color: dialog.ink; font.pixelSize: 11; Layout.preferredWidth: 88 }
                }
            }
        }
        Text {
            Layout.fillWidth: true
            text: dialog.selectedTable && dialog.selectedTable.modulePowerW === 1 ? "1 W: Laut Hersteller nicht zum Schneiden empfohlen." : "Herstellervorgaben · keine eigenen Materialtests. Gravur-Durchgänge werden in der Quelle nicht angegeben."
            wrapMode: Text.WordWrap; color: dialog.muted; font.pixelSize: 10
        }
        Text {
            Layout.fillWidth: true
            text: "Quelle: <a href='https://de.blazexlaser.com/pages/maschineneinstellung'>BlazeX · Maschineneinstellung</a> · Stand " + (dialog.catalog.retrievedOn || "")
            color: dialog.muted; linkColor: "#209b76"; font.pixelSize: 10
            onLinkActivated: link => Qt.openUrlExternally(link)
        }
    }
}
