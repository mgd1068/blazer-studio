import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    objectName: "m3CommissioningDialog"
    property var modelMachine: null
    property bool dark: false
    property string actionError: ""
    property bool finishRequested: false
    property bool finishSawPending: false
    readonly property var state: modelMachine && typeof modelMachine.commissioning !== "undefined" ? modelMachine.commissioning : ({stage:"inactive",reason:"Geräteeinrichtung ist in dieser Ansicht nicht verfügbar."})
    readonly property string stage: state.stage || "inactive"
    readonly property bool idle: modelMachine && !modelMachine.requestPending && !modelMachine.busy && !modelMachine.firing
    readonly property color ink: dark ? "#e2eaf0" : "#263b4a"
    readonly property color muted: dark ? "#adc0ce" : "#5f7687"
    function mm(value) { return typeof value === "number" && Number.isFinite(value) ? value.toFixed(3) + " mm" : "unbekannt" }
    function numeric(field) { const text=field.text.trim().replace(",", "."); return /^[+-]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)$/.test(text) ? Number(text) : NaN }
    function inStages(values) { return values.indexOf(stage) >= 0 }
    function register(control, action) { if (modelMachine && typeof modelMachine.registerNativeActionControl !== "undefined") modelMachine.registerNativeActionControl(control, action) }
    title: dialog.state.hasSavedCalibration ? "Gerät initialisieren" : "M3 einrichten – Referenz und Fahrraum"
    anchors.centerIn: parent; modal: true; focus: true
    width: Math.min(parent.width-40,780); height: Math.min(parent.height-40,760)
    closePolicy: Popup.NoAutoClose
    background: Rectangle { radius: 9; color: dialog.dark ? "#202e39" : "white"; border.color: "#708290" }
    onAboutToShow: {
        actionError=""
        finishRequested=false; finishSawPending=false
        switches.checked=false; direction.checked=false; freeTravel.checked=false; laserDisabled.checked=false
        observedHome.checked=false; measuredProbe.checked=false; observedCorner.checked=false; coordinates.checked=false
        // A fault contains the reason why evidence was revoked. Opening this
        // dialog must not erase it or silently start the whole wizard again.
        if (modelMachine && typeof modelMachine.beginCommissioning !== "undefined" && stage === "inactive") modelMachine.beginCommissioning()
    }
    property bool controlsReady: false
    Component.onCompleted: controlsReady=true
    onStageChanged: {
        if(!controlsReady)return
        actionError=""
        if (stage === "complete" && finishRequested) { finishRequested=false; dialog.close() }
        if (stage === "fault") finishRequested=false
        if (stage === "mechanics" || stage === "fault") { switches.checked=false; direction.checked=false; freeTravel.checked=false; laserDisabled.checked=false }
        if (stage === "homing") observedHome.checked=false
        if (stage === "probePending") measuredProbe.checked=false
        if (stage === "cornerPending") observedCorner.checked=false
        if (stage === "zeroPending") coordinates.checked=false
    }
    onClosed: if (modelMachine && typeof modelMachine.cancelCommissioning !== "undefined" && stage !== "complete") modelMachine.cancelCommissioning()
    function checkFinish() {
        if (!finishRequested) return
        if (stage === "complete") { finishRequested=false; dialog.close(); return }
        if (stage === "fault") { finishRequested=false; return }
        if (modelMachine.requestPending) finishSawPending=true
        else if (finishSawPending) {
            finishRequested=false
            actionError="Abschluss nicht bestätigt. " + (state.reason || "Bitte die Koordinatenzuordnung erneut prüfen.")
        }
    }
    Timer {
        interval: 6000; running: dialog.finishRequested
        onTriggered: {
            dialog.finishRequested=false
            dialog.actionError="Abschluss noch nicht bestätigt. " + (dialog.state.reason || "Controller-Rückmeldung fehlt.")
        }
    }
    Connections {
        target: dialog.modelMachine
        ignoreUnknownSignals: true
        function onChanged() { Qt.callLater(dialog.checkFinish) }
        function onNativeActionDenied(action, reason) {
            if (dialog.visible && action.indexOf("setup") === 0) dialog.actionError=reason
        }
    }
    component Witness: CheckBox {
        id: witness
        Layout.fillWidth: true
        contentItem: Text { text: witness.text; textFormat: Text.PlainText; wrapMode: Text.WordWrap; color: dialog.ink; leftPadding: witness.indicator.width+witness.spacing; verticalAlignment: Text.AlignVCenter }
    }
    contentItem: ScrollView {
        id: scroll; clip: true; contentWidth: availableWidth
        ColumnLayout {
            width: scroll.availableWidth; spacing: 12
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: dialog.ink; text: "Diese Einrichtung gilt für das ausgewählte reale Gerät und die aktuelle Verbindung. Messwerte werden vom Controller gelesen; mechanische Beobachtungen bestätigst du am Gerät. Alle Prüffahrten erfolgen ohne Laserfreigabe." }
            Label {
                objectName: "commissioningStage"; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: dialog.ink; font.bold: true
                text: ({savedHomeReady:"Referenzieren mit gespeicherter Einrichtung",restoringHome:"Referenzfahrt läuft",savedApproachReady:"Sicheren Startpunkt anfahren",restoringApproach:"Sicherer Startpunkt wird angefahren",restoredOriginReview:"Geänderten Arbeitsursprung prüfen",inactive:"Einrichtung beginnen",readback:"1 · Controllerdaten lesen",mechanics:"2 · Mechanik prüfen",homeReady:"3 · Referenzfahrt",homing:"3 · Referenzfahrt läuft",geometry:"4 · Referenz und Maßstab prüfen",probePending:"4 · Prüfschritt läuft",probeConfirm:"4 · Prüfschritt bestätigen",limits:"5 · Gemessenen Innenfahrraum eingeben",cornerReady:"5 · Innenraum prüfen",cornerPending:"5 · Ecke wird angefahren",cornerConfirm:"5 · Tatsächliche Ecke bestätigen",workCoordinates:"6 · Zeichnungskoordinaten zuordnen",zeroPending:"6 · Arbeitsnullpunkt wird geprüft",complete:"Einrichtung dieser Verbindung abgeschlossen",fault:"Einrichtung unterbrochen"})[dialog.stage] || dialog.stage
            }
            Label { objectName: "commissioningReason"; Layout.fillWidth: true; textFormat: Text.PlainText; wrapMode: Text.WordWrap; color: dialog.stage === "fault" ? "#c05242" : dialog.muted; text: dialog.state.reason || "" }
            Label { objectName: "commissioningControllerState"; Layout.fillWidth: true; visible: !!dialog.state.controllerState; textFormat: Text.PlainText; wrapMode: Text.WordWrap; color: dialog.state.activeInputs || dialog.state.controllerState === "Alarm" ? "#c05242" : dialog.muted; text: "Letzte Controller-Rückmeldung: " + (dialog.state.controllerState || "unbekannt") + (dialog.state.activeInputs ? " · Endschalter oder Sperreingang aktiv. Ursache am Gerät prüfen; nicht automatisch entriegeln." : "") }
            Label { objectName: "commissioningActionError"; Layout.fillWidth: true; visible: text.length > 0; textFormat: Text.PlainText; wrapMode: Text.WordWrap; color: "#c05242"; text: dialog.actionError }
            Label { objectName: "commissioningPosition"; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: dialog.ink; text: "Maschinenposition: X " + dialog.mm(dialog.state.machineX) + " · Y " + dialog.mm(dialog.state.machineY) + "\nArbeitsposition: X " + dialog.mm(dialog.state.workX) + " · Y " + dialog.mm(dialog.state.workY) + "\nWCO: X " + dialog.mm(dialog.state.wcoX) + " · Y " + dialog.mm(dialog.state.wcoY) }
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: dialog.muted; visible: !!dialog.state.fingerprint && dialog.state.fingerprint.length > 0; text: "Controller- und Einstellungsstand: " + (dialog.state.fingerprint || "") }
            ColumnLayout {
                Layout.fillWidth: true; visible: dialog.stage === "mechanics"
                Witness { id: switches; objectName: "setupSwitchesWitness"; text: "Die vorgesehenen Endschalter sind vorhanden, richtig angeschlossen und am Gerät geprüft." }
                Witness { id: direction; objectName: "setupDirectionWitness"; text: "Homingrichtung und Pull-off passen zur tatsächlich beobachteten Mechanik." }
                Witness { id: freeTravel; objectName: "setupFreeTravelWitness"; text: "Referenzweg und die kleinen anschließenden Prüfschritte sind frei; ich beobachte die Fahrten am Gerät." }
                Witness { id: laserDisabled; objectName: "setupLaserDisabledWitness"; text: "Laseremission ist für diese Einrichtung am Gerät sicher deaktiviert." }
                ActionButton { objectName: "attestSetupMechanics"; text: "Mechanik bestätigt – Referenzfahrt vorbereiten"; dark: dialog.dark; enabled: dialog.idle && switches.checked && direction.checked && freeTravel.checked && laserDisabled.checked; onClicked: dialog.modelMachine.attestCommissioningMechanics(switches.checked,direction.checked,freeTravel.checked,laserDisabled.checked) }
            }
            ColumnLayout {
                Layout.fillWidth: true; visible: dialog.inStages(["homeReady","homing","savedHomeReady","restoringHome"])
                Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: dialog.ink; text: "Erst der folgende Klick fordert eine echte Referenzfahrt an. Bei falscher Richtung oder unerwartetem Verhalten sofort STOPP bzw. den Not-Aus am Gerät verwenden." }
                ActionButton { id: home; objectName: "setupHomeButton"; property bool nativeActionEnabled: dialog.visible && dialog.inStages(["homeReady","homing","savedHomeReady","restoringHome"]); text: dialog.inStages(["homing","restoringHome"]) ? "Referenzfahrt läuft …" : "Referenzfahrt jetzt starten"; dark: dialog.dark; primary: true; enabled: dialog.inStages(["homeReady","savedHomeReady"]) && dialog.idle; Component.onCompleted: dialog.register(home,"setupHome"); onClicked: dialog.modelMachine.commissioningHome() }
            }
            ColumnLayout {
                Layout.fillWidth: true; visible: dialog.inStages(["savedApproachReady","restoringApproach"])
                Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: dialog.ink; text: "Der Referenzpunkt liegt außerhalb des nutzbaren Innenraums. Den gespeicherten freien Anfahrtsweg beobachten; diese Fahrt erfolgt ohne Laser." }
                ActionButton {
                    id: restoredApproach; objectName: "setupRestoredApproach"
                    property bool nativeActionEnabled: dialog.visible && dialog.stage === "savedApproachReady"
                    property var nativeActionArguments: ({index:0})
                    text: dialog.stage === "restoringApproach" ? "Startpunkt wird angefahren …" : "Sicheren Startpunkt laserfrei anfahren"
                    dark: dialog.dark; enabled: dialog.stage === "savedApproachReady" && dialog.idle
                    Component.onCompleted: dialog.register(restoredApproach,"setupCorner")
                    onClicked: dialog.modelMachine.commissioningCorner(0)
                }
            }
            ColumnLayout {
                Layout.fillWidth: true; visible: dialog.inStages(["geometry","probePending","probeConfirm"])
                Witness { id: observedHome; objectName: "setupObservedHome"; visible: !dialog.state.homeObserved; text: "Der Kopf steht am tatsächlichen vorgesehenen Referenzort; der Pull-off ist abgeschlossen." }
                ActionButton { objectName: "confirmSetupHome"; visible: !dialog.state.homeObserved; text: "Beobachteten Referenzort bestätigen"; dark: dialog.dark; enabled: dialog.stage === "geometry" && dialog.idle && observedHome.checked; onClicked: dialog.modelMachine.confirmCommissioningHome() }
                Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: dialog.ink; text: "Je Achse einen freien 1-mm-Prüfschritt wählen, Richtung und tatsächliche Strecke messen. Mit der Zeichnungsansicht vergleichen: Ursprung links unten, +X nach rechts und +Y nach oben; eine gedrehte Kameraansicht ist kein Achsenbezug. Ein Positionszähler allein ersetzt die Messung nicht." }
                RowLayout {
                    Layout.fillWidth: true
                    Repeater {
                        model: [{label:"X −1 mm",dx:-1,dy:0},{label:"X +1 mm",dx:1,dy:0},{label:"Y −1 mm",dx:0,dy:-1},{label:"Y +1 mm",dx:0,dy:1}]
                        delegate: ActionButton {
                            id: probe; required property var modelData
                            objectName: "setupProbe_" + modelData.dx + "_" + modelData.dy
                            property bool nativeActionEnabled: dialog.visible && dialog.inStages(["geometry","probePending","probeConfirm"])
                            property var nativeActionArguments: ({dx:modelData.dx,dy:modelData.dy})
                            text: modelData.label; dark: dialog.dark; Layout.fillWidth: true
                            enabled: dialog.stage === "geometry" && !!dialog.state.homeObserved && dialog.idle
                            Component.onCompleted: dialog.register(probe,"setupProbe")
                            onClicked: { measuredProbe.checked=false; dialog.modelMachine.commissioningProbe(modelData.dx,modelData.dy) }
                        }
                    }
                }
                Witness { id: measuredProbe; objectName: "setupMeasuredProbe"; text: "Der letzte Prüfschritt fuhr in die richtige Richtung und tatsächlich 1 mm." }
                ActionButton { objectName: "confirmSetupProbe"; text: "Letzten Prüfschritt bestätigen"; dark: dialog.dark; enabled: dialog.stage === "probeConfirm" && dialog.idle && measuredProbe.checked; onClicked: { dialog.modelMachine.confirmCommissioningProbe(); measuredProbe.checked=false } }
                Label { Layout.fillWidth: true; color: dialog.muted; text: "Bestätigte Achsprüfungen: " + (dialog.state.probesConfirmed || 0) + " / 2" }
            }
            ColumnLayout {
                Layout.fillWidth: true; visible: dialog.stage === "limits"
                Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: dialog.ink; text: "Den tatsächlich gemessenen nutzbaren Innenfahrraum in Maschinenkoordinaten eintragen, mit Abstand zu den mechanischen Grenzen. Negative Werte sind zulässig. Nominelle 200 × 150 mm werden nicht automatisch als sicher übernommen." }
                GridLayout {
                    Layout.fillWidth: true; columns: 2
                    Label { text: "X Minimum (mm)"; color: dialog.ink }
                    DialogField { id: minX; objectName: "setupMinX"; dark: dialog.dark; Layout.fillWidth: true; placeholderText: "gemessen"; maximumLength: 18 }
                    Label { text: "X Maximum (mm)"; color: dialog.ink }
                    DialogField { id: maxX; objectName: "setupMaxX"; dark: dialog.dark; Layout.fillWidth: true; placeholderText: "gemessen"; maximumLength: 18 }
                    Label { text: "Y Minimum (mm)"; color: dialog.ink }
                    DialogField { id: minY; objectName: "setupMinY"; dark: dialog.dark; Layout.fillWidth: true; placeholderText: "gemessen"; maximumLength: 18 }
                    Label { text: "Y Maximum (mm)"; color: dialog.ink }
                    DialogField { id: maxY; objectName: "setupMaxY"; dark: dialog.dark; Layout.fillWidth: true; placeholderText: "gemessen"; maximumLength: 18 }
                }
                ActionButton { objectName: "submitSetupLimits"; text: "Innenfahrraum übernehmen und einzeln prüfen"; dark: dialog.dark; enabled: dialog.idle && [minX,minY,maxX,maxY].every(f=>Number.isFinite(dialog.numeric(f))) && dialog.numeric(minX)<dialog.numeric(maxX) && dialog.numeric(minY)<dialog.numeric(maxY); onClicked: dialog.modelMachine.setCommissioningLimits(dialog.numeric(minX),dialog.numeric(minY),dialog.numeric(maxX),dialog.numeric(maxY)) }
            }
            ColumnLayout {
                Layout.fillWidth: true; visible: dialog.inStages(["cornerReady","cornerPending","cornerConfirm"])
                Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: dialog.ink; text: "Bestätigter Innenraum: X " + dialog.mm(dialog.state.minX) + " bis " + dialog.mm(dialog.state.maxX) + " · Y " + dialog.mm(dialog.state.minY) + " bis " + dialog.mm(dialog.state.maxY) + "\nJede der vier Ecken einzeln laserfrei anfahren und die tatsächliche Position beobachten. Ziel: X " + dialog.mm(dialog.state.targetMachineX) + " · Y " + dialog.mm(dialog.state.targetMachineY) + "." }
                ActionButton { id: corner; objectName: "setupCornerButton"; property bool nativeActionEnabled: dialog.visible && dialog.inStages(["cornerReady","cornerPending","cornerConfirm"]); property var nativeActionArguments: ({index:dialog.state.cornerIndex}); text: dialog.stage === "cornerPending" ? "Ecke wird angefahren …" : "Ecke " + (Number(dialog.state.cornerIndex)+1) + " laserfrei anfahren"; dark: dialog.dark; enabled: dialog.stage === "cornerReady" && dialog.idle; Component.onCompleted: dialog.register(corner,"setupCorner"); onClicked: { observedCorner.checked=false; dialog.modelMachine.commissioningCorner(dialog.state.cornerIndex) } }
                Witness { id: observedCorner; objectName: "setupObservedCorner"; text: "Der Kopf steht an der erwarteten gemessenen Innenecke, mit freiem Abstand zur Begrenzung." }
                ActionButton { objectName: "confirmSetupCorner"; text: "Diese tatsächliche Ecke bestätigen"; dark: dialog.dark; enabled: dialog.stage === "cornerConfirm" && dialog.idle && observedCorner.checked; onClicked: { dialog.modelMachine.confirmCommissioningCorner(); observedCorner.checked=false } }
                Label { Layout.fillWidth: true; color: dialog.muted; text: "Bestätigte Ecken: " + (dialog.state.cornersConfirmed || 0) + " / 4" }
            }
            ColumnLayout {
                Layout.fillWidth: true; visible: dialog.inStages(["workCoordinates","zeroPending","restoredOriginReview"])
                Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: dialog.ink; text: "Maschinenposition = Arbeitsposition + WCO. Die angezeigte G54-Zuordnung muss zu den tatsächlich geprüften Ecken und den Zeichnungskoordinaten passen. Kein automatisches Setzen oder Raten des Nullpunkts." }
                ActionButton { id: zero; objectName: "setupZeroButton"; property bool nativeActionEnabled: dialog.visible && dialog.inStages(["workCoordinates","zeroPending","restoredOriginReview"]); visible: dialog.stage !== "restoredOriginReview"; text: "Geprüfte Ursprungsecke als Arbeitsnull setzen"; dark: dialog.dark; enabled: dialog.stage === "workCoordinates" && dialog.idle; Component.onCompleted: dialog.register(zero,"setupZero"); onClicked: { coordinates.checked=false; dialog.modelMachine.commissioningZero() } }
                Witness { id: coordinates; objectName: "setupCoordinatesWitness"; text: "Die angezeigten Maschinen-/Arbeitskoordinaten und die Zuordnung zum Zeichnungsursprung stimmen am Gerät. Das Design muss innerhalb des gemessenen Innenraums liegen; die nominelle Profilfläche kann größer sein." }
                ActionButton { objectName: "confirmSetupCoordinates"; text: dialog.finishRequested ? "Abschluss wird bestätigt …" : "Koordinaten bestätigen und Einrichtung abschließen"; dark: dialog.dark; primary: true; enabled: dialog.inStages(["workCoordinates","restoredOriginReview"]) && dialog.idle && coordinates.checked && !dialog.finishRequested; onClicked: { dialog.finishRequested=true; dialog.finishSawPending=false; dialog.modelMachine.confirmCommissioningCoordinates() } }
            }
            Label { visible: dialog.stage === "complete"; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: dialog.ink; text: "Referenz und Fahrraum sind für diese Verbindung eingerichtet. Gerätebetrieb, Material und Anwesenheit werden vor der Bearbeitung geprüft. Hier wird kein Laserauftrag automatisch gestartet. Für die erste beaufsichtigte Materialprobe die Laserfreigabe erst nach Abschluss der Einrichtung am Gerät wiederherstellen; anschließend Gerätelüfter, Material und geschlossenen Arbeitsraum prüfen." }
            ActionButton { objectName: "restartSetup"; visible: dialog.inStages(["inactive","fault"]); text: "Controllerdaten neu lesen"; dark: dialog.dark; enabled: dialog.idle && modelMachine && modelMachine.connected; onClicked: dialog.modelMachine.beginCommissioning() }
        }
    }
    footer: RowLayout {
        ActionButton { objectName: "stopSetup"; text: "■ STOPP"; destructive: true; dark: dialog.dark; onClicked: { if(dialog.modelMachine) dialog.modelMachine.stop() } }
        Item { Layout.fillWidth: true }
        ActionButton { objectName: "cancelSetup"; text: "Einrichtung verwerfen"; dark: dialog.dark; visible: dialog.stage !== "complete"; enabled: !dialog.finishRequested; onClicked: dialog.close() }
        ActionButton { objectName: "closeSetup"; text: "Zurück zum Gerät"; dark: dialog.dark; visible: dialog.stage === "complete"; onClicked: dialog.close() }
    }
}
