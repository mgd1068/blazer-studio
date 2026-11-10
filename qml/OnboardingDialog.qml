import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Dialog {
    id: intro
    objectName: "onboardingDialog"
    property var service: null
    property bool dark: false
    property int step: 0
    signal practiceRequested()
    focus: true
    title: "Willkommen bei Blazer Studio"
    modal: true; anchors.centerIn: parent
    width: Math.min(parent.width-48,740)
    height: Math.min(parent.height-48,540)
    onOpened: step=0
    readonly property color ink: dark ? "#e2eaf0" : "#263b4a"
    background: Rectangle { radius: 9; color: intro.dark ? "#202e39" : "white"; border.color: "#708290" }
    contentItem: ColumnLayout {
        spacing: 18
        Text { text: "Schritt "+(intro.step+1)+" von 3"; color: intro.ink; font.pixelSize: 12 }
        Text { text: ["Zuerst am Bildschirm ausprobieren","Dein erster Auftrag","Hilfe und Rückmeldungen"][intro.step]; color: intro.ink; font.pixelSize: 23; font.bold: true; Layout.fillWidth: true; wrapMode: Text.WordWrap }
        Text {
            objectName: "onboardingText"
            text: intro.step===0 ? "Blazer Studio startet im Simulator. Du kannst dort Motive gestalten und Abläufe prüfen, ohne dass sich ein echter Laser bewegt.\n\nDies ist ein Beta-Testkandidat für Linux. Die vollständige Geräte- und Materialabnahme am Blaze X M3 steht noch aus."
                : intro.step===1 ? "1. Motiv laden oder mit einem Übungsprojekt beginnen.\n2. Laseraufsatz und Material auswählen. Profilwerte und Quelle prüfen.\n3. Größe und Position in Millimetern einstellen.\n4. Vorschau kontrollieren und am Bildschirm testen.\n5. Dein Projekt als .blazer-Datei speichern.\n\nHerstellerwerte sind Ausgangswerte und müssen am eigenen Material geprüft werden."
                : "Unter Hilfe / Beta kannst du diese Einführung erneut öffnen und einen technischen Diagnosebericht speichern.\n\nDer Bericht enthält Versions-, System- und Zustandsdaten. Projektinhalte, Bilder, Dateipfade, Materialnotizen und Geräteprotokolle werden nicht aufgenommen. Du entscheidest selbst, ob du ihn an einen Fehlerbericht anhängst."
            Layout.fillWidth: true; Layout.fillHeight: true; wrapMode: Text.WordWrap; color: intro.ink; font.pixelSize: 15; lineHeight: 1.35
        }
        Text { text: intro.service ? intro.service.message : ""; visible: text.length>0; color: "#b87529"; Layout.fillWidth: true; wrapMode: Text.WordWrap }
    }
    footer: Item {
        implicitHeight: 56
        RowLayout {
            anchors.fill: parent; anchors.margins: 10; spacing: 8
        ActionButton { text: "Später"; dark: intro.dark; hint: "Schließt die Einführung. Solange sie nicht abgeschlossen ist, erscheint sie beim nächsten normalen Start wieder."; onClicked: intro.reject() }
        ActionButton { text: "Zurück"; dark: intro.dark; visible: intro.step>0; hint: "Zeigt die vorherige Hilfeseite. Das Projekt bleibt unverändert."; onClicked: --intro.step }
        Item { Layout.fillWidth: true }
        ActionButton { objectName: "onboardingNext"; text: "Weiter"; primary: true; visible: intro.step<2; hint: "Zeigt die nächste Hilfeseite. Es wird kein Gerätebefehl gesendet."; onClicked: ++intro.step }
        ActionButton { objectName: "onboardingFinish"; text: "Einführung abschließen"; primary: true; visible: intro.step===2; hint: "Merkt sich den abgeschlossenen Erststart. Du kannst die Einführung jederzeit über Hilfe / Beta öffnen."; onClicked: { if(!intro.service || intro.service.finishOnboarding())intro.close() } }
        ActionButton { objectName: "onboardingPractice"; text: "Übungsprojekt im Simulator"; visible: intro.step===2; dark: intro.dark; hint: "Wechselt zum Simulator und lädt ein Übungsprojekt. Bei ungespeicherten Änderungen erfolgt zuvor eine Rückfrage."; onClicked: { if(!intro.service || intro.service.finishOnboarding()) {intro.close();intro.practiceRequested()} } }
        }
    }
}
