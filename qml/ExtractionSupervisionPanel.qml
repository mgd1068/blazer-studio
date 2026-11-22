import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: panel
    objectName: "extractionSupervisionPanel"
    property var modelMachine: null
    property bool dark: false
    property bool commissioningComplete: false
    property bool setupReady: commissioningComplete
    readonly property var state: modelMachine && typeof modelMachine.extractionSupervision !== "undefined" ? modelMachine.extractionSupervision : ({mode:"sensorVerified",notice:"Absaugung noch nicht eingerichtet.",reason:"",pendingAction:"",remainingMs:0,activeLease:false})
    readonly property bool attended: state.mode === "operatorAttendedExternal"
    readonly property bool jobPaused: modelMachine ? modelMachine.paused : false
    property bool controlsReady: false
    Component.onCompleted: controlsReady = true
    onJobPausedChanged: if (controlsReady) actionChoice.currentIndex = jobPaused ? 2 : 0
    readonly property bool idle: modelMachine && !modelMachine.requestPending && !modelMachine.busy && !modelMachine.firing
    readonly property color ink: dark ? "#e2eaf0" : "#263b4a"
    readonly property color muted: dark ? "#adc0ce" : "#5f7687"
    spacing: 9
    component Witness: CheckBox {
        id: witness
        Layout.fillWidth: true
        contentItem: Text { text: witness.text; wrapMode: Text.WordWrap; textFormat: Text.PlainText; color: panel.ink; leftPadding: witness.indicator.width+witness.spacing; verticalAlignment: Text.AlignVCenter }
    }
    SectionLabel { text: "ABSAUGUNG UND AUFSICHT"; dark: panel.dark }
    Label { objectName: "extractionSupervisionNotice"; Layout.fillWidth: true; wrapMode: Text.WordWrap; textFormat: Text.PlainText; color: panel.ink; text: panel.state.notice || "Absaugung noch nicht eingerichtet." }
    Label { objectName: "extractionSupervisionReason"; Layout.fillWidth: true; wrapMode: Text.WordWrap; textFormat: Text.PlainText; color: panel.muted; visible: text.length>0; text: panel.state.reason || "" }
    Witness { id: clearance; objectName: "extractionClearanceWitness"; enabled: !panel.state.activeLease; text: "Arbeitsbereich ist rauchfrei; der tatsächliche Gerätezustand nach einer Störung ist geklärt." }
    ActionButton { objectName: "confirmExtractionClearance"; text: "Sicheren Zustand bestätigen"; dark: panel.dark; enabled: panel.idle && !panel.state.activeLease && clearance.checked; onClicked: { panel.modelMachine.confirmExtractionClearance(clearance.checked); clearance.checked=false } }
    ActionButton { objectName: "selectAttendedExtraction"; visible: !panel.attended; text: "Beaufsichtigte Absaugung ohne Sensor wählen"; dark: panel.dark; enabled: panel.setupReady && panel.idle; onClicked: panel.modelMachine.selectAttendedExtraction() }
    Label { visible: panel.attended; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: panel.ink; text: "Absaugung am Gerät unabhängig einschalten und prüfen. Luftstrom wird nicht gemessen. Während der Bearbeitung anwesend bleiben; bei Ausfall sofort STOPP oder den Not-Aus am Gerät verwenden." }
    ColumnLayout {
        Layout.fillWidth: true; visible: panel.attended
        ToolCombo {
            id: actionChoice; objectName: "extractionActionChoice"; Layout.fillWidth: true; dark: panel.dark
            textRole: "label"; valueRole: "action"
            model: [{label:"Neuen Auftrag starten",action:"start"},{label:"Fokuspunkt prüfen",action:"focus"},{label:"Pausierten Auftrag fortsetzen",action:"resume"}]
            Component.onCompleted: currentIndex = panel.jobPaused ? 2 : 0
            enabled: !panel.modelMachine || !panel.modelMachine.requestPending
        }
        Witness { id: running; objectName: "externalExtractionOnWitness"; text: "Die tatsächliche Absaugung läuft und ist am Gerät geprüft." }
        Witness { id: present; objectName: "operatorPresentWitness"; text: "Ich bin am Gerät anwesend und beaufsichtige diesen konkreten Vorgang." }
        Witness { id: workReady; objectName: "attendedWorkAreaWitness"; text: "Material und Fokus passen zum Vorgang; das Gehäuse ist geschlossen und der Arbeitsraum frei." }
        ActionButton {
            objectName: "attestExternalExtraction"; text: "Für diesen Vorgang bestätigen"; dark: panel.dark
            enabled: running.checked && present.checked && workReady.checked && panel.modelMachine && !panel.modelMachine.requestPending && !panel.modelMachine.firing && (!panel.modelMachine.busy || panel.modelMachine.paused)
            onClicked: { panel.modelMachine.attestExternalExtraction(actionChoice.currentValue,running.checked,present.checked); running.checked=false; present.checked=false; workReady.checked=false }
        }
        Label {
            objectName: "extractionWitnessStatus"; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: panel.ink
            text: panel.state.activeLease ? "Beaufsichtigter Vorgang aktiv · Absaugung weiter laufen lassen." : panel.state.pendingAction ? "Bestätigung für " + ({start:"Auftrag",focus:"Fokuspunkt",resume:"Fortsetzen"})[panel.state.pendingAction] + " · noch " + Math.max(0,Math.ceil(Number(panel.state.remainingMs)/1000)) + " s. Die Ausgabe beginnt erst mit der zugehörigen Starttaste." : "Vor Start, Fokus oder Fortsetzen erneut bestätigen. Eine Bestätigung startet nichts."
        }
        ActionButton { objectName: "revokeExternalAttendance"; text: "■ Absaugung oder Aufsicht entfallen – STOPP"; destructive: true; dark: panel.dark; onClicked: { panel.modelMachine.revokeExternalAttendance(); running.checked=false; present.checked=false; workReady.checked=false } }
    }
}
