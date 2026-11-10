import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Dialog {
    id: help
    objectName: "betaHelpDialog"
    property var service: null
    property bool dark: false
    signal introductionRequested()
    signal diagnosticsRequested()
    focus: true
    title: "Hilfe / Beta"
    anchors.centerIn: parent; modal: true
    width: Math.min(parent.width-48,760); height: Math.min(parent.height-48,600)
    readonly property color ink: dark ? "#e2eaf0" : "#263b4a"
    background: Rectangle { radius: 9; color: help.dark ? "#202e39" : "white"; border.color: "#708290" }
    contentItem: ScrollView {
        id: helpScroll
        clip: true; contentWidth: availableWidth
        ColumnLayout {
            width: helpScroll.availableWidth; spacing: 16
            Text { objectName: "betaVersion"; text: "Blazer Studio "+(help.service ? help.service.version+" · "+help.service.revision : "0.9-Testkandidat"); color: help.ink; font.pixelSize: 19; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            Text { text: "Unterstütztes Testsystem: Ubuntu 26.04, x86_64.\nStandard: Simulator. Geräte-/Materialabnahme am M3 und externe Bedienungstests sind noch offen."; color: help.ink; font.pixelSize: 14; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            ActionButton { objectName: "openIntroduction"; text: "Einführung erneut öffnen"; dark: help.dark; hint: "Öffnet die drei Hilfeseiten zum ersten Auftrag. Alle Projekte bleiben unverändert."; onClicked: {help.close();help.introductionRequested()} }
            Text { text: "Bekannte Grenzen\n• Herstellerprofile nennen M3 Pro; die Zuordnung zum M3 ist noch nicht als Materialergebnis bestätigt.\n• STL-/OBJ-Modelle werden derzeit mit bis zu 512 Pixeln rasterisiert. Importierte Bilder und Tiefenkarten behalten bis zu 4096 Pixel.\n• Einzelne Import-, Speicher- und Advanced-Editoraktionen laufen noch synchron.\n• PDF/AI/EPS benötigen das separat installierte Inkscape.\n• Kamera, Rotary und zusätzliche Achsen sind nicht freigegeben."; color: help.ink; font.pixelSize: 14; Layout.fillWidth: true; wrapMode: Text.WordWrap; lineHeight: 1.25 }
            ActionButton { objectName: "exportDiagnosticsButton"; text: "Diagnosebericht speichern …"; dark: help.dark; enabled: !!help.service; hint: "Speichert ausgewählte technische Zustandsdaten als JSON. Es werden keine Projektinhalte oder Geräteprotokolle aufgenommen und keine Daten versendet."; onClicked: help.diagnosticsRequested() }
            Text { text: help.service ? help.service.message : ""; visible: text.length>0; color: help.ink; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            ActionButton { text: "Fehler auf GitHub melden"; dark: help.dark; hint: "Öffnet die Fehlerbericht-Vorlagen im Browser. Du entscheidest, was du dort absendest."; onClicked: Qt.openUrlExternally("https://github.com/mgd1068/blazer-studio/issues/new/choose") }
        }
    }
    footer: DialogButtonBox { ActionButton { text: "Schließen"; dark: help.dark; hint: "Schließt die Hilfe. Dein Projekt bleibt geöffnet."; onClicked: help.close() } }
}
