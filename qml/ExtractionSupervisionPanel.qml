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
    readonly property bool deviceCycleSupported: !!modelMachine && typeof modelMachine.selectSupervisedDeviceCycle === "function"
    readonly property bool deviceCycle: state.mode === "operatorSupervisedDeviceCycle"
    readonly property bool jobPaused: modelMachine ? modelMachine.paused : false
    property bool controlsReady: false
    Component.onCompleted: controlsReady = true
    onJobPausedChanged: if (controlsReady) { actionChoice.currentIndex = jobPaused ? 2 : 0; deviceActionChoice.currentIndex = jobPaused ? 2 : 0 }
    readonly property bool idle: modelMachine && !modelMachine.requestPending && !modelMachine.busy && !modelMachine.firing
    readonly property color ink: dark ? "#e2eaf0" : "#263b4a"
    readonly property color muted: dark ? "#adc0ce" : "#5f7687"
    spacing: 9
    component Witness: CheckBox {
        id: witness
        Layout.fillWidth: true
        contentItem: Text { text: witness.text; wrapMode: Text.WordWrap; textFormat: Text.PlainText; color: panel.ink; leftPadding: witness.indicator.width+witness.spacing; verticalAlignment: Text.AlignVCenter }
    }
    SectionLabel { text: panel.deviceCycleSupported ? "GERÄTELÜFTER UND AUFSICHT" : "ABSAUGUNG UND AUFSICHT"; dark: panel.dark }
    Label { objectName: "extractionSupervisionNotice"; Layout.fillWidth: true; wrapMode: Text.WordWrap; textFormat: Text.PlainText; color: panel.ink; text: panel.state.singleClickStart === true ? "Für einen Auftrag bestätigt der bewusste Startklick die eigene Anwesenheit. Die zusätzlichen Einstellungen hier gehören zum erweiterten Gerätebetrieb." : panel.deviceCycleSupported && !panel.attended && !panel.deviceCycle ? "Bedienmodus wählen: beaufsichtigter Gerätebetrieb oder optionale Sensoranlage." : panel.state.notice || "Absaugung noch nicht eingerichtet." }
    Label { objectName: "extractionSupervisionReason"; Layout.fillWidth: true; wrapMode: Text.WordWrap; textFormat: Text.PlainText; color: panel.muted; visible: text.length>0; text: panel.state.reason || "" }
    Witness { id: clearance; objectName: "extractionClearanceWitness"; enabled: !panel.state.activeLease; text: panel.deviceCycleSupported && !panel.attended ? "Der tatsächliche Gerätezustand ist geprüft; eine vorherige Störung ist geklärt." : "Arbeitsbereich ist rauchfrei; der tatsächliche Gerätezustand nach einer Störung ist geklärt." }
    ActionButton { objectName: "confirmExtractionClearance"; text: "Sicheren Zustand bestätigen"; dark: panel.dark; enabled: panel.idle && !panel.state.activeLease && clearance.checked; onClicked: { panel.modelMachine.confirmExtractionClearance(clearance.checked); clearance.checked=false } }
    ActionButton { objectName:"selectSupervisedDeviceCycle"; visible:panel.deviceCycleSupported && !panel.deviceCycle; text:"Beaufsichtigten Gerätebetrieb verwenden"; dark:panel.dark; enabled:panel.setupReady && panel.idle; onClicked:panel.modelMachine.selectSupervisedDeviceCycle() }
    ColumnLayout {
        visible:panel.deviceCycle; Layout.fillWidth:true
        Label { objectName:"deviceEnclosureFanStatus"; Layout.fillWidth:true; wrapMode:Text.WordWrap; textFormat:Text.PlainText; color:panel.ink; text:"Gehäuselüfter: tatsächlicher Lauf wird nicht vom Controller zurückgemeldet. Beim ersten Auftrag am Gerät prüfen." }
        Label { objectName:"deviceLaserFanStatus"; Layout.fillWidth:true; wrapMode:Text.WordWrap; textFormat:Text.PlainText; color:panel.ink; text:"Lasermodullüfter: tatsächlicher Lauf wird nicht vom Controller zurückgemeldet. Beide Lüfter sind nicht als getrennte Ausgänge eingerichtet." }
        Label { Layout.fillWidth:true; wrapMode:Text.WordWrap; color:panel.muted; text:"Der normale Gerätezyklus bleibt während der Bearbeitung und der Leerfahrten zusammenhängend aktiv. Die externe Absaugung wird hier nicht gesteuert oder abgefragt." }
        ToolCombo {
            id:deviceActionChoice; objectName:"deviceCycleActionChoice"; Layout.fillWidth:true; dark:panel.dark
            textRole:"label"; valueRole:"action"
            model:[{label:"Neuen Auftrag starten",action:"start"},{label:"Fokuspunkt prüfen",action:"focus"},{label:"Pausierten Auftrag fortsetzen",action:"resume"}]
            Component.onCompleted:currentIndex=panel.jobPaused ? 2 : 0
            enabled:!panel.modelMachine || !panel.modelMachine.requestPending
        }
        Witness { id:devicePresent; objectName:"deviceOperatorPresentWitness"; text:"Ich bin am Gerät anwesend, beaufsichtige diesen Vorgang und kann sofort am Gerät stoppen." }
        ActionButton {
            objectName:"attestDeviceCycle"; text:"Für diesen Vorgang bereit"; dark:panel.dark
            enabled:devicePresent.checked && !!panel.modelMachine && !panel.modelMachine.requestPending && !panel.modelMachine.firing && (!panel.modelMachine.busy || panel.modelMachine.paused)
            onClicked:{ panel.modelMachine.attestDeviceCycle(deviceActionChoice.currentValue,devicePresent.checked); devicePresent.checked=false }
        }
        Label { objectName:"deviceCycleWitnessStatus"; Layout.fillWidth:true; wrapMode:Text.WordWrap; color:panel.ink; text:panel.state.activeLease ? "Beaufsichtigter Gerätebetrieb aktiv." : panel.state.pendingAction ? "Bereit für "+({start:"Auftrag",focus:"Fokuspunkt",resume:"Fortsetzen"})[panel.state.pendingAction]+" · noch "+Math.max(0,Math.ceil(Number(panel.state.remainingMs)/1000))+" s. Start erfolgt erst mit der zugehörigen Taste." : "Vor Start oder Fortsetzen die Anwesenheit für den konkreten Vorgang bestätigen." }
        ActionButton { objectName:"revokeDeviceAttendance"; text:"■ Aufsicht beenden – STOPP"; destructive:true; dark:panel.dark; onClicked:{ panel.modelMachine.revokeDeviceAttendance();devicePresent.checked=false } }
    }
    ActionButton { objectName:"showExternalExtractionOptions"; visible:panel.deviceCycleSupported && !panel.attended; text:"Optionale externe Sensor-/Absaugungsanlage …"; dark:panel.dark; onClicked:externalOptions.visible=!externalOptions.visible }
    ColumnLayout {
        id:externalOptions; visible:!panel.deviceCycleSupported || panel.attended; Layout.fillWidth:true
        ActionButton { objectName: "selectAttendedExtraction"; visible: !panel.attended; text: "Beaufsichtigte Absaugung ohne Sensor wählen"; dark: panel.dark; enabled: panel.setupReady && panel.idle; onClicked: panel.modelMachine.selectAttendedExtraction() }
    }
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
