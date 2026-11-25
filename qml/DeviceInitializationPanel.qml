import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: panel
    property var modelMachine: null
    property bool dark: false
    property bool actionsAllowed: true
    property string actionError: ""
    signal reviewRequested()
    readonly property var state: modelMachine && typeof modelMachine.commissioning !== "undefined" ? modelMachine.commissioning : ({})
    readonly property string stage: state.stage || "inactive"
    readonly property bool idle: modelMachine && !modelMachine.requestPending && !modelMachine.busy && !modelMachine.firing
    readonly property color ink: dark ? "#e2eaf0" : "#263b4a"
    spacing: 8
    onStageChanged: actionError=""
    Connections {
        target: panel.modelMachine
        ignoreUnknownSignals: true
        function onNativeActionDenied(action, reason) {
            if (panel.visible && action.indexOf("setup") === 0) panel.actionError=reason
        }
    }
    Label {
        objectName: "initializationStatus"
        Layout.fillWidth: true; wrapMode: Text.WordWrap; textFormat: Text.PlainText
        color: panel.stage === "fault" ? "#c05242" : panel.ink
        text: (panel.stage === "complete" && panel.state.calibrationStored ? "Geräteeinrichtung gespeichert. " : "") + (panel.state.reason || "Nach dem Verbinden werden die Controllerdaten automatisch geprüft.")
    }
    Label {
        objectName: "initializationStorageError"
        Layout.fillWidth: true; wrapMode: Text.WordWrap; textFormat: Text.PlainText; color: "#c05242"
        visible: text.length > 0
        text: panel.state.storageError || ""
    }
    Label {
        objectName: "initializationActionError"
        Layout.fillWidth: true; wrapMode: Text.WordWrap; textFormat: Text.PlainText; color: "#c05242"
        visible: text.length > 0; text: panel.actionError
    }
    ActionButton {
        id: reference; objectName: "initializeSavedHome"
        Layout.fillWidth: true; dark: panel.dark; primary: true
        property bool nativeActionEnabled: panel.visible && panel.actionsAllowed && panel.stage === "savedHomeReady"
        visible: panel.stage === "savedHomeReady" || panel.stage === "restoringHome"
        enabled: panel.stage === "savedHomeReady" && panel.idle && panel.actionsAllowed
        text: panel.stage === "restoringHome" ? "Referenzfahrt läuft …" : "Referenzieren – ohne Laser"
        hint: "Verwendet die gespeicherte, mit diesem Controller abgeglichene Einrichtung. Referenzweg freihalten und Fahrt am Gerät beobachten."
        Component.onCompleted: if(panel.modelMachine) panel.modelMachine.registerNativeActionControl(reference,"setupHome")
        onClicked: panel.modelMachine.commissioningHome()
    }
    ActionButton {
        id: approach; objectName: "initializeSavedApproach"
        Layout.fillWidth: true; dark: panel.dark; primary: true
        property bool nativeActionEnabled: panel.visible && panel.actionsAllowed && panel.stage === "savedApproachReady"
        property var nativeActionArguments: ({index:0})
        visible: panel.stage === "savedApproachReady" || panel.stage === "restoringApproach"
        enabled: panel.stage === "savedApproachReady" && panel.idle && panel.actionsAllowed
        text: panel.stage === "restoringApproach" ? "Sicherer Startpunkt wird angefahren …" : "Sicheren Startpunkt anfahren – ohne Laser"
        Component.onCompleted: if(panel.modelMachine) panel.modelMachine.registerNativeActionControl(approach,"setupCorner")
        onClicked: panel.modelMachine.commissioningCorner(0)
    }
    ActionButton {
        objectName: "initializeOriginReview"
        Layout.fillWidth: true; dark: panel.dark
        visible: panel.stage === "restoredOriginReview"
        enabled: panel.idle && panel.actionsAllowed
        text: "Geänderten Arbeitsursprung prüfen …"
        onClicked: panel.reviewRequested()
    }
}
