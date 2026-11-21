import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    objectName: "manualRegistrationDialog"
    required property var document
    property var service: null
    property bool dark: false
    readonly property color ink: dark ? "#e2eaf0" : "#263b4a"
    readonly property var review: service ? service.review : ({})
    readonly property var state: service ? service.state : ({})
    readonly property var report: review.valid === true || !!review.reason ? review : state.enabled ? state : review
    readonly property string blocked: service ? service.blockReason : "Registrierungsdienst nicht verfügbar."
    property string previousToken: ""
    function fields() { return [d1x,d1y,t1x,t1y,d2x,d2y,t2x,t2y] }
    function coordinates() { return fields().map(field => field ? field.numericValue() : NaN) }
    readonly property string inputError: {
        const values = coordinates()
        for (let i=0;i<values.length;i++) {
            const n=values[i]
            if (typeof n !== "number" || !Number.isFinite(n)) return "Ungültige Rohkoordinate: eine endliche Länge in mm oder in eingeben."
            if (n<0 || n>(i%2===0 ? document.bedWidth : document.bedHeight)) return "Referenz- und Zielpunkte müssen innerhalb der Profilfläche liegen."
        }
        const error=residual ? residual.numericValue() : NaN
        if (typeof error!=="number" || !Number.isFinite(error) || error<0 || error>10) return "Zulässigen Zweitpunktfehler zwischen 0 und 10 mm eingeben."
        return ""
    }
    function invalidateDraft() {
        if(consent)consent.checked=false
        if (service && service.review.token) service.clearReview()
    }
    function selection(index) {
        if (!service) return
        const point=service.selectionReference()
        if (!point.valid) { draftMessage.text=point.reason; return }
        const x=index===0?d1x:d2x, y=index===0?d1y:d2y
        x.value=point.x;y.value=point.y;x.resetInput();y.resetInput()
        draftMessage.text="Auswahlzentrum als Designpunkt übernommen; Zielkoordinaten ausdrücklich eingeben."
        invalidateDraft()
    }
    function length(n) { return Number.isFinite(Number(n)) ? Number(n).toFixed(3)+" mm" : "–" }
    title: "Print and Cut · manuelle Registrierung"
    anchors.centerIn: parent; modal: true; focus: true
    width: Math.min(parent.width-40,780); height: Math.min(parent.height-40,720)
    background: Rectangle { color: dialog.dark?"#202e39":"white";radius:9;border.color:"#708290" }
    onClosed: { if(service)service.clearReview();consent.checked=false }
    Connections {
        target: dialog.service
        function onChanged() {
            const token=dialog.review.token || ""
            if(token!==dialog.previousToken) {consent.checked=false;dialog.previousToken=token}
        }
    }
    contentItem: ScrollView {
        id: scroll;clip:true;contentWidth:availableWidth
        ColumnLayout {
            width:scroll.availableWidth;spacing:12
            Label { Layout.fillWidth:true;wrapMode:Text.WordWrap;color:dialog.ink;text:"Zwei Designpunkte ihren absoluten Zielkoordinaten zuordnen. Eingaben sind manuelle Daten im profilgebundenen Offline-G54-Rahmen; hier wird kein Laser bewegt, gepulst oder real vermessen. X nach rechts, Y nach oben." }
            Label { objectName:"registrationState";Layout.fillWidth:true;wrapMode:Text.WordWrap;textFormat:Text.PlainText;color:dialog.state.enabled&&!dialog.state.valid?"#c05242":dialog.ink;text:dialog.state.enabled?(dialog.state.busy?"Registrierte Ausgabe wird berechnet …":dialog.state.valid?"AKTIV: Simulation, GCode und Auftragsrahmen sind registriert.":"REGISTRIERUNG GESPERRT: "+(dialog.state.reason||"Quellstand erneut prüfen.")):"Keine Ausgaberegistrierung aktiv." }
            Label { Layout.fillWidth:true;wrapMode:Text.WordWrap;color:dialog.ink;text:"Designpunkte können die Zentren einzeln ausgewählter Vektormarken bzw. vollständiger Gruppen sein. T1/T2-Marken bleiben ohne Ausgabe. Mindestens 10 mm Basisabstand; weiter auseinander liegende Punkte sind günstiger." }
            GroupBox {
                title:"1 · Erstes Punktpaar";Layout.fillWidth:true
                ColumnLayout {
                    anchors.fill:parent
                    RowLayout { Layout.fillWidth:true
                        NumberField { id:d1x;objectName:"registrationDesign1X";label:"Design X";value:0;unit:"mm";dark:dialog.dark;resetOnFinish:false;Layout.fillWidth:true;onEdited:n=>{value=n;resetInput()};onInputTextChanged:dialog.invalidateDraft() }
                        NumberField { id:d1y;objectName:"registrationDesign1Y";label:"Y";value:0;unit:"mm";dark:dialog.dark;resetOnFinish:false;Layout.fillWidth:true;onEdited:n=>{value=n;resetInput()};onInputTextChanged:dialog.invalidateDraft() }
                    }
                    ActionButton { objectName:"registrationSelection1";text:"Auswahlzentrum als Designpunkt 1";dark:dialog.dark;enabled:dialog.blocked.length===0;onClicked:dialog.selection(0) }
                    RowLayout { Layout.fillWidth:true
                        NumberField { id:t1x;objectName:"registrationTarget1X";label:"Ziel X";value:0;unit:"mm";dark:dialog.dark;resetOnFinish:false;Layout.fillWidth:true;onEdited:n=>{value=n;resetInput()};onInputTextChanged:dialog.invalidateDraft() }
                        NumberField { id:t1y;objectName:"registrationTarget1Y";label:"Y";value:0;unit:"mm";dark:dialog.dark;resetOnFinish:false;Layout.fillWidth:true;onEdited:n=>{value=n;resetInput()};onInputTextChanged:dialog.invalidateDraft() }
                    }
                }
            }
            GroupBox {
                title:"2 · Zweites Punktpaar";Layout.fillWidth:true
                ColumnLayout {
                    anchors.fill:parent
                    RowLayout { Layout.fillWidth:true
                        NumberField { id:d2x;objectName:"registrationDesign2X";label:"Design X";value:0;unit:"mm";dark:dialog.dark;resetOnFinish:false;Layout.fillWidth:true;onEdited:n=>{value=n;resetInput()};onInputTextChanged:dialog.invalidateDraft() }
                        NumberField { id:d2y;objectName:"registrationDesign2Y";label:"Y";value:0;unit:"mm";dark:dialog.dark;resetOnFinish:false;Layout.fillWidth:true;onEdited:n=>{value=n;resetInput()};onInputTextChanged:dialog.invalidateDraft() }
                    }
                    ActionButton { objectName:"registrationSelection2";text:"Auswahlzentrum als Designpunkt 2";dark:dialog.dark;enabled:dialog.blocked.length===0;onClicked:dialog.selection(1) }
                    RowLayout { Layout.fillWidth:true
                        NumberField { id:t2x;objectName:"registrationTarget2X";label:"Ziel X";value:0;unit:"mm";dark:dialog.dark;resetOnFinish:false;Layout.fillWidth:true;onEdited:n=>{value=n;resetInput()};onInputTextChanged:dialog.invalidateDraft() }
                        NumberField { id:t2y;objectName:"registrationTarget2Y";label:"Y";value:0;unit:"mm";dark:dialog.dark;resetOnFinish:false;Layout.fillWidth:true;onEdited:n=>{value=n;resetInput()};onInputTextChanged:dialog.invalidateDraft() }
                    }
                }
            }
            Label { id:draftMessage;objectName:"registrationDraftMessage";Layout.fillWidth:true;wrapMode:Text.WordWrap;color:dialog.ink }
            CheckBox { id:scaled;objectName:"registrationScaleChoice";text:"Proportional skalieren (aus beiden Punktabständen)";onClicked:dialog.invalidateDraft();Layout.fillWidth:true }
            Label { Layout.fillWidth:true;wrapMode:Text.WordWrap;color:dialog.ink;text:scaled.checked?"Ein gemeinsamer Maßstab plus Drehung und Versatz. Keine Scherung, Spiegelung oder perspektivische Korrektur.":"Ohne Skalierung: Originalgröße bleibt erhalten; Punkt 1 wird exakt zugeordnet, Punkt 2 bestimmt die Drehung. Der verbleibende Zweitpunktfehler wird geprüft." }
            NumberField { id:residual;objectName:"registrationResidual";label:"Max. Zweitpunktfehler";value:.5;unit:"mm";dark:dialog.dark;resetOnFinish:false;Layout.fillWidth:true;onEdited:n=>{value=n;resetInput()};onInputTextChanged:dialog.invalidateDraft() }
            Label { objectName:"registrationInputError";visible:text.length>0;text:dialog.inputError||dialog.blocked;Layout.fillWidth:true;wrapMode:Text.WordWrap;textFormat:Text.PlainText;color:"#c05242" }
            ActionButton { id:reviewButton;objectName:"reviewRegistration";text:"Registrierte Ausgabe prüfen";dark:dialog.dark;enabled:dialog.service&&dialog.blocked.length===0&&dialog.inputError.length===0;onClicked:if(enabled)dialog.service.reviewPoints(dialog.coordinates(),scaled.checked,residual.numericValue()) }
            Label { objectName:"registrationReviewSummary";Layout.fillWidth:true;wrapMode:Text.WordWrap;textFormat:Text.PlainText;color:dialog.report.valid?dialog.ink:"#c05242";text:dialog.report.valid?"Drehung: "+Number(dialog.report.angleDegrees).toFixed(3)+"° · Maßstab: "+Number(dialog.report.scale).toFixed(6)+"\nZweitpunktfehler: "+dialog.length(dialog.report.residualMm)+" · Basis: "+dialog.length(dialog.report.designDistanceMm)+" → "+dialog.length(dialog.report.targetDistanceMm)+"\nAusgabe einschließlich Überlauf innerhalb der Profilfläche. "+(dialog.report.approachKnown?"Anfahrt ab bekanntem Vorschaukopf separat laserfrei geplant.":"Tatsächlicher Anfahrtsstart unbekannt; Encoder fährt den ersten Ausgabepunkt mit abgeschalteter Leistung an."):dialog.report.reason||"Noch keine geprüfte Registrierung." }
            Image { objectName:"manualRegistrationPreview";Layout.fillWidth:true;Layout.preferredHeight:240;fillMode:Image.PreserveAspectFit;source:dialog.review.previewSource || "";visible:source.toString().length>0;cache:false }
            Label { visible:!!dialog.review.previewSource;Layout.fillWidth:true;wrapMode:Text.WordWrap;color:dialog.ink;text:"Geprüfte registrierte Ausgabe: Blau = Bearbeitung, Orange = leistungsfreier Überlauf, Rot = manuelle Zielpunkte. Darstellung der Profilfläche; kein Kamerabild und kein Messnachweis." }
            CheckBox { id:consent;objectName:"registrationConsent";text:"Punktzuordnung und registrierte Vorschau geprüft. Als sitzungsbezogene Ausgabe übernehmen; keine reale Messung oder Gerätefreigabe.";enabled:dialog.review.valid===true&&!!dialog.review.token;Layout.fillWidth:true;Layout.minimumWidth:0;contentItem:Text { text:consent.text;color:dialog.ink;wrapMode:Text.WordWrap;leftPadding:consent.indicator.width+consent.spacing;verticalAlignment:Text.AlignVCenter } }
            ActionButton { id:applyButton;objectName:"applyRegistration";text:"Registrierung für Ausgabe aktivieren";primary:true;dark:dialog.dark;enabled:dialog.service&&dialog.review.valid===true&&!!dialog.review.token&&consent.checked&&dialog.blocked.length===0;onClicked:if(enabled)dialog.service.applyReview(dialog.review.token) }
            ActionButton { objectName:"resetRegistration";text:"Ausgaberegistrierung ausdrücklich zurücksetzen";dark:dialog.dark;enabled:dialog.service&&dialog.state.enabled===true&&dialog.blocked.length===0;onClicked:if(enabled)dialog.service.resetAlignment() }
            Label { objectName:"registrationMessage";Layout.fillWidth:true;wrapMode:Text.WordWrap;textFormat:Text.PlainText;color:dialog.ink;text:dialog.service?dialog.service.message:"" }
            Label { Layout.fillWidth:true;wrapMode:Text.WordWrap;color:dialog.ink;text:"Aktivierung wählt Absolute Koordinaten. Änderungen an Quelle, Auswahlmaske oder Profil sperren alte Zuordnungen. Projektdateien, Vorlagen und Undo-Stände speichern diese Ausrichtung nicht; nach Öffnen/Verlaufswechsel neu prüfen. CSV-Stapel/Materialtest-Export sind bei aktiver Registrierung gesperrt. Kamera- und Linsenkalibrierung sowie automatische Markensuche fehlen." }
        }
    }
    footer:RowLayout { Item{Layout.fillWidth:true} ActionButton{text:"Schließen";dark:dialog.dark;onClicked:dialog.close()} }
}
