import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    objectName: "manualStartDialog"
    property var modelMachine: null
    property var modelProject: null
    property bool dark: false
    readonly property bool supported: !!modelMachine && typeof modelMachine.beginManualPlacement === "function"
    readonly property var state: modelMachine && typeof modelMachine.operatorLocalFrame !== "undefined" ? modelMachine.operatorLocalFrame : ({phase:"inactive",reason:"Manueller Start ist in dieser Ansicht nicht verfügbar."})
    readonly property string phase: state.phase || "inactive"
    readonly property bool idle: supported && !modelMachine.requestPending && !modelMachine.busy && !modelMachine.firing
    readonly property bool planning: !!modelProject && !!modelProject.planningBusy
    readonly property bool controllerPreparing: !!modelMachine && typeof modelMachine.jobPreparing !== "undefined" && modelMachine.jobPreparing
    readonly property bool positioned: !!state.localPositionKnown
    readonly property color ink: dark ? "#e2eaf0" : "#263b4a"
    readonly property color muted: dark ? "#adc0ce" : "#5f7687"
    property bool controlsReady: false
    property bool beginPending: false
    property double beginRevision: -1
    property bool cancelledHere: false
    function stages(values) { return values.indexOf(phase) >= 0 }
    function number(field) {
        const text=field.text.trim().replace(",", ".")
        return /^[+]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)$/.test(text) ? Number(text) : NaN
    }
    function mm(value) { return typeof value === "number" && Number.isFinite(value) ? value.toFixed(3)+" mm" : "unbekannt" }
    function resetWitnesses() { motors.checked=false; measured.checked=false; stationary.checked=false; cover.checked=false }
    function resetDraft() { negativeX.text=""; positiveX.text=""; negativeY.text=""; positiveY.text=""; resetWitnesses() }
    function register(control, action) { if(modelMachine && typeof modelMachine.registerNativeActionControl === "function") modelMachine.registerNativeActionControl(control, action) }
    readonly property bool draftComplete: [negativeX,positiveX,negativeY,positiveY].every(f=>Number.isFinite(number(f)) && number(f)>=0 && number(f)<=2000)
    readonly property bool draftValid: draftComplete && !!modelProject && Number.isFinite(modelProject.bedWidth) && Number.isFinite(modelProject.bedHeight) && number(negativeX)+number(positiveX)<=modelProject.bedWidth && number(negativeY)+number(positiveY)<=modelProject.bedHeight && [negativeX,positiveX,negativeY,positiveY].every(f=>number(f)>=2)
    readonly property bool draftMatches: draftValid && !!state.available && state.available.negativeX===number(negativeX) && state.available.positiveX===number(positiveX) && state.available.negativeY===number(negativeY) && state.available.positiveY===number(positiveY)
    readonly property bool witnesses: motors.checked && measured.checked && stationary.checked && cover.checked
    title: "Start ab manuell gesetztem Kopf"
    anchors.centerIn: parent
    modal: true; focus: true
    width: Math.min(parent.width-40,800); height: Math.min(parent.height-40,820)
    closePolicy: Popup.NoAutoClose
    background: Rectangle { radius:9; color: dialog.dark ? "#202e39" : "white"; border.color:"#708290" }
    Component.onCompleted: controlsReady=true
    onAboutToShow: { cancelledHere=false; if(controlsReady)resetWitnesses() }
    onPhaseChanged: {
        if(!controlsReady)return
        if(stages(["reading","positioning","invalid","inactive"]))beginPending=false
        if(stages(["invalid","inactive"]))resetWitnesses()
    }
    onStateChanged: {
        if(controlsReady && beginPending && typeof state.resultRevision === "number" && state.resultRevision>beginRevision && state.lastRequestAccepted===false) beginPending=false
    }
    onClosed: {
        if(supported && !cancelledHere && (beginPending || (!positioned && phase!=="inactive"))) modelMachine.cancelOperatorLocalFrame()
        beginPending=false
    }
    component Witness: CheckBox {
        id: witness
        Layout.fillWidth: true
        contentItem: Text { text:witness.text; textFormat:Text.PlainText; wrapMode:Text.WordWrap; color:dialog.ink; leftPadding:witness.indicator.width+witness.spacing; verticalAlignment:Text.AlignVCenter }
    }
    contentItem: ScrollView {
        id: scroll; clip:true; contentWidth:availableWidth
        ColumnLayout {
            width:scroll.availableWidth; spacing:12
            Label { Layout.fillWidth:true; wrapMode:Text.WordWrap; color:dialog.ink; text:"Bereite das Motiv vor. Anschließend kannst du den Kopf bei tatsächlich freigegebenen Antrieben von Hand setzen. Der gewählte Motivanker liegt an diesem Punkt. Seine globale Maschinenposition ist danach unbekannt." }
            Label {
                objectName:"manualStartPhase"; Layout.fillWidth:true; wrapMode:Text.WordWrap; color:dialog.ink; font.bold:true
                text: ({inactive:"1 · Handpositionierung vorbereiten",reading:"Controllerdaten werden gelesen",positioning:"2 · Kopf setzen und Restwege messen",captureReady:"3 · Lokalen Startpunkt übernehmen",resetting:"Lokaler Bezug wird vorbereitet",capturing:"Lokaler Bezug wird bestätigt",preparing:"4 · Verfahrwege vorbereiten",travelReview:"4 · Gesamte Verfahrwege prüfen",ready:"5 · Auftrag startbereit",running:"Vorgang läuft",paused:"Vorgang pausiert",invalid:"Handstart muss neu vorbereitet werden"})[dialog.phase] || dialog.phase
            }
            Label { objectName:"manualStartReason"; Layout.fillWidth:true; wrapMode:Text.WordWrap; textFormat:Text.PlainText; color:dialog.phase==="invalid" ? "#c05242" : dialog.muted; text:dialog.state.reason || "" }
            ActionButton {
                objectName:"beginManualPlacement"; text:dialog.positioned ? "Kopf von Hand neu setzen …" : "Handpositionierung beginnen"; dark:dialog.dark
                enabled:dialog.idle && !dialog.beginPending && !!dialog.modelProject && !!dialog.modelMachine.connected
                onClicked: { dialog.beginRevision=typeof dialog.state.resultRevision === "number" ? dialog.state.resultRevision : -1; dialog.beginPending=true; dialog.resetDraft(); dialog.modelProject.setStartMode(1); dialog.modelMachine.beginManualPlacement() }
            }
            ColumnLayout {
                Layout.fillWidth:true; visible:dialog.stages(["positioning","captureReady"])
                Label { Layout.fillWidth:true; wrapMode:Text.WordWrap; color:dialog.ink; text:"Kopf nur bewegen, wenn die Achsen wirklich freigegeben sind; nicht gegen Haltekraft drücken. Danach Hände herausnehmen und die Haube schließen. Die folgenden Restwege müssen am tatsächlichen Kopfstandort gemessen sein, in den X-/Y-Richtungen der Maschine." }
                GridLayout {
                    Layout.fillWidth:true; columns:2
                    Label { text:"Restweg X − (mm)"; color:dialog.ink }
                    DialogField { id:negativeX; objectName:"manualNegativeX"; dark:dialog.dark; Layout.fillWidth:true; placeholderText:"gemessen"; maximumLength:18; onTextEdited:dialog.resetWitnesses() }
                    Label { text:"Restweg X + (mm)"; color:dialog.ink }
                    DialogField { id:positiveX; objectName:"manualPositiveX"; dark:dialog.dark; Layout.fillWidth:true; placeholderText:"gemessen"; maximumLength:18; onTextEdited:dialog.resetWitnesses() }
                    Label { text:"Restweg Y − (mm)"; color:dialog.ink }
                    DialogField { id:negativeY; objectName:"manualNegativeY"; dark:dialog.dark; Layout.fillWidth:true; placeholderText:"gemessen"; maximumLength:18; onTextEdited:dialog.resetWitnesses() }
                    Label { text:"Restweg Y + (mm)"; color:dialog.ink }
                    DialogField { id:positiveY; objectName:"manualPositiveY"; dark:dialog.dark; Layout.fillWidth:true; placeholderText:"gemessen"; maximumLength:18; onTextEdited:dialog.resetWitnesses() }
                }
                Label { Layout.fillWidth:true; wrapMode:Text.WordWrap; color:dialog.muted; text:"Zusätzliche Reserve: 2 mm in jeder Richtung. Unbekannte Angaben bleiben leer. Die Summe beider X-Restwege darf den Profilachsweg nicht überschreiten; ebenso für Y. Die Profilmaße allein liefern keine Restwege." }
                Label { objectName:"manualDraftError"; Layout.fillWidth:true; wrapMode:Text.WordWrap; color:"#c05242"; visible:!dialog.draftValid; text:!dialog.draftComplete ? "Alle vier gemessenen Restwege vollständig eintragen." : "Restwege prüfen: mindestens 2 mm Abstand je Richtung; gegenüberliegende Werte dürfen zusammen den Profilachsweg nicht überschreiten." }
                ActionButton { objectName:"setManualTravel"; text:"Gemessene Restwege übernehmen"; dark:dialog.dark; enabled:dialog.idle && !dialog.beginPending && dialog.draftValid; onClicked:{ dialog.resetWitnesses(); dialog.modelMachine.setManualTravel(dialog.number(negativeX),dialog.number(positiveX),dialog.number(negativeY),dialog.number(positiveY)) } }
                Witness { id:motors; objectName:"manualMotorsFreeWitness"; text:"Die Achsen waren beim Handpositionieren tatsächlich frei; ich habe nicht gegen Haltekraft gedrückt." }
                Witness { id:measured; objectName:"manualMeasuredTravelWitness"; text:"Achsrichtungen und alle vier Restwege sind am Gerät geprüft; der ganze angegebene Fahrraum ist frei." }
                Witness { id:stationary; objectName:"manualHeadStationaryWitness"; text:"Der Kopf steht am gewählten Startpunkt; meine Hände sind aus der Maschine." }
                Witness { id:cover; objectName:"manualCoverClosedWitness"; text:"Die Haube ist geschlossen und eingerastet." }
                ActionButton { objectName:"attestManualPlacement"; text:"Handposition und Messung bestätigen"; dark:dialog.dark; enabled:dialog.idle && !dialog.beginPending && dialog.draftMatches && dialog.witnesses; onClicked:dialog.modelMachine.attestManualPlacement(motors.checked,measured.checked,stationary.checked,cover.checked) }
            }
            ActionButton {
                id:capture; objectName:"captureOperatorLocalFrame"
                property bool nativeActionEnabled: dialog.visible && !dialog.beginPending && dialog.stages(["captureReady","resetting","capturing","preparing","travelReview","ready"])
                text:dialog.stages(["resetting","capturing"]) ? "Startpunkt wird übernommen …" : "Hier den lokalen Startpunkt übernehmen"
                dark:dialog.dark; primary:true; visible:dialog.stages(["captureReady","resetting","capturing"])
                enabled:dialog.phase==="captureReady" && dialog.idle && dialog.draftMatches && dialog.witnesses
                Component.onCompleted:dialog.register(capture,"localCapture")
                onClicked:dialog.modelMachine.captureOperatorLocalFrame()
            }
            Label { objectName:"manualLocalPosition"; Layout.fillWidth:true; wrapMode:Text.WordWrap; color:dialog.ink; text:dialog.positioned ? "Lokale Lage ab Handstartpunkt: X "+dialog.mm(dialog.state.localX)+" · Y "+dialog.mm(dialog.state.localY)+". Die globale Maschinenreferenz bleibt unbekannt." : "Lokaler Startpunkt noch nicht bestätigt." }
            ColumnLayout {
                Layout.fillWidth:true; visible:dialog.positioned
                SectionLabel { text:"MOTIVANKER AM KOPF"; dark:dialog.dark }
                Label { Layout.fillWidth:true; wrapMode:Text.WordWrap; color:dialog.ink; text:"Dieser Punkt des Motivs wird an den gesetzten Kopfstandort gelegt. Ein Ankerwechsel berechnet die Wege neu und verwirft eine alte Auftragsbestätigung." }
                GridLayout {
                    columns:3; rowSpacing:5; columnSpacing:5
                    Repeater {
                        model:[6,7,8,3,4,5,0,1,2]
                        delegate:ActionButton {
                            required property int modelData
                            objectName:"manualAnchor"+modelData
                            text:dialog.modelProject && dialog.modelProject.jobOrigin===modelData ? "●" : "○"
                            primary:!!dialog.modelProject && dialog.modelProject.jobOrigin===modelData
                            dark:dialog.dark; Layout.preferredWidth:54
                            enabled:dialog.idle && !dialog.beginPending && !!dialog.modelProject
                            onClicked:dialog.modelProject.setJobOrigin(modelData)
                        }
                    }
                }
                ActionButton { objectName:"prepareLocalJob"; text:dialog.controllerPreparing || dialog.planning ? "Berechnung läuft …" : "Verfahrwege prüfen"; dark:dialog.dark; enabled:dialog.idle && !dialog.beginPending && !dialog.planning && !dialog.controllerPreparing; onClicked:dialog.modelMachine.prepareLocalJob() }
                SectionLabel { text:"GESAMTER FAHRWEG"; dark:dialog.dark }
                Label { Layout.fillWidth:true; wrapMode:Text.WordWrap; color:dialog.muted; text:"Geprüft werden auch Anfahrt, Leerfahrten, Rasterüberlauf und tatsächliche Befehlskoordinaten. Die verfügbare Strecke unten enthält bereits den Abzug von 2 mm Reserve." }
                GridLayout {
                    Layout.fillWidth:true; columns:3; columnSpacing:12; rowSpacing:6
                    Label { text:"Richtung"; color:dialog.ink; font.bold:true }
                    Label { text:"Benötigt"; color:dialog.ink; font.bold:true }
                    Label { text:"Nutzbar"; color:dialog.ink; font.bold:true }
                    Repeater {
                        model:[{key:"negativeX",label:"X −"},{key:"positiveX",label:"X +"},{key:"negativeY",label:"Y −"},{key:"positiveY",label:"Y +"}]
                        delegate:RowLayout {
                            required property var modelData
                            Layout.columnSpan:3; Layout.fillWidth:true; spacing:12
                            Label { text:modelData.label; color:dialog.ink; Layout.fillWidth:true }
                            Label { objectName:"manualRequired_"+modelData.key; text:dialog.mm(dialog.state.requiredActualWire ? dialog.state.requiredActualWire[modelData.key] : undefined); color:dialog.ink; Layout.fillWidth:true }
                            Label { objectName:"manualAvailable_"+modelData.key; text:dialog.mm(dialog.state.usableAfterMargin ? dialog.state.usableAfterMargin[modelData.key] : undefined); color:dialog.ink; Layout.fillWidth:true }
                        }
                    }
                }
                Label { objectName:"manualTravelVerdict"; Layout.fillWidth:true; wrapMode:Text.WordWrap; textFormat:Text.PlainText; color:dialog.state.fits ? "#168e76" : "#c05242"; text:!dialog.state.prepared ? "Wegeprüfung noch nicht vollständig vorbereitet." : dialog.state.fits ? "Der vollständige vorbereitete Fahrweg passt in die bestätigten Restwege." : (dialog.state.reason || "Der Fahrweg passt nicht in die bestätigten Restwege.") }
                ActionButton { objectName:"approveLocalTravel"; text:dialog.state.approved ? "Diese Wegeprüfung ist bestätigt" : "Diese Wegeprüfung bestätigen"; dark:dialog.dark; enabled:dialog.idle && !dialog.beginPending && !!dialog.state.prepared && !!dialog.state.fits && !!dialog.state.approvalId && !dialog.state.approved; onClicked:dialog.modelMachine.approveLocalTravel(dialog.state.approvalId) }
                ExtractionSupervisionPanel { objectName:"manualExtractionPanel"; Layout.fillWidth:true; modelMachine:dialog.modelMachine; dark:dialog.dark; setupReady:dialog.positioned }
                ActionButton {
                    id:frame; objectName:"frameLocalJob"; property bool nativeActionEnabled:dialog.visible && !dialog.beginPending && dialog.positioned
                    text:"Rahmen laserfrei prüfen und zum Startpunkt zurück"; dark:dialog.dark
                    enabled:dialog.idle && !dialog.beginPending && !!dialog.state.prepared && !!dialog.state.fits && !!dialog.state.frameReady
                    Component.onCompleted:dialog.register(frame,"localFrame")
                    onClicked:dialog.modelMachine.frameLocalJob()
                }
                Label { objectName:"manualFrameReason"; Layout.fillWidth:true; visible:!!dialog.state.frameReason; wrapMode:Text.WordWrap; textFormat:Text.PlainText; color:dialog.muted; text:dialog.state.frameReason || "" }
                ActionButton {
                    id:start; objectName:"startLocalJob"; property bool nativeActionEnabled:dialog.visible && !dialog.beginPending && dialog.positioned
                    text:"Auftrag am Handstartpunkt starten"; dark:dialog.dark; primary:true
                    enabled:dialog.idle && !dialog.beginPending && !!dialog.state.startReady
                    Component.onCompleted:dialog.register(start,"localStart")
                    onClicked:dialog.modelMachine.startLocalJob()
                }
                ActionButton { objectName:"pauseLocalJob"; text:"Ⅱ Pause"; dark:dialog.dark; enabled:dialog.phase==="running" && !!dialog.modelMachine && !dialog.modelMachine.requestPending; onClicked:dialog.modelMachine.pause() }
                ActionButton {
                    id:resume; objectName:"resumeLocalJob"; property bool nativeActionEnabled:dialog.visible && !dialog.beginPending && dialog.positioned
                    text:"▶ Pausierten Auftrag fortsetzen"; dark:dialog.dark
                    enabled:dialog.phase==="paused" && !!dialog.modelMachine && !!dialog.modelMachine.holdConfirmed && !dialog.modelMachine.requestPending && !!dialog.state.resumeReady
                    Component.onCompleted:dialog.register(resume,"localResume")
                    onClicked:dialog.modelMachine.resumeLocalJob()
                }
                Label { Layout.fillWidth:true; wrapMode:Text.WordWrap; color:dialog.muted; text:"Alle Berechnungen und Bestätigungen erfolgen vor Startbereitschaft. Übernehmen, Berechnen oder Bestätigen startet keine Fahrt. Nach erneutem Handverschieben, Türereignis oder Verbindungswechsel muss der Handstart neu vorbereitet werden." }
            }
        }
    }
    footer:ColumnLayout {
        RowLayout {
            ActionButton { objectName:"stopManualStart"; text:"■ STOPP"; destructive:true; dark:dialog.dark; Layout.fillWidth:true; onClicked:if(dialog.modelMachine)dialog.modelMachine.stop() }
            ActionButton { objectName:"cancelOperatorLocalFrame"; text:"Handstart verwerfen"; dark:dialog.dark; Layout.fillWidth:true; enabled:dialog.idle; onClicked:{ dialog.cancelledHere=true; dialog.modelMachine.cancelOperatorLocalFrame(); dialog.close() } }
        }
        ActionButton { objectName:"closeManualStart"; text:"Schließen"; dark:dialog.dark; Layout.fillWidth:true; enabled:!dialog.modelMachine || (!dialog.modelMachine.busy && !dialog.modelMachine.requestPending && !dialog.modelMachine.firing); onClicked:dialog.close() }
    }
}
