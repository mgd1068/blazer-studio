import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    objectName: "jobPlacementDialog"
    required property var document
    property var modelMachine: null
    property bool dark: false
    readonly property bool simulation: !modelMachine || typeof modelMachine.simulation === "undefined" || modelMachine.simulation
    readonly property bool editable: modelMachine && !modelMachine.busy && !modelMachine.firing
    readonly property var summary: document.placementSummary
    readonly property color ink: dark ? "#e2eaf0" : "#263b4a"
    function length(value) {
        if (!Number.isFinite(Number(value))) return "–"
        const inches = typeof appServices !== "undefined" && appServices.lengthUnit === "in"
        return (Number(value)/(inches ? 25.4 : 1)).toFixed(inches ? 4 : 3) + (inches ? " in" : " mm")
    }
    title: "Startposition und Auftragsursprung"
    anchors.centerIn: parent; modal: true; focus: true
    width: Math.min(parent.width - 48, 720)
    height: Math.min(parent.height - 48, 650)
    background: Rectangle { color: dialog.dark ? "#202e39" : "white"; radius: 9; border.color: "#708290" }
    contentItem: ScrollView {
        id: placementScroll; clip: true; contentWidth: availableWidth
        ColumnLayout {
            width: placementScroll.availableWidth; spacing: 12
            CheckBox {
                id: selectedOutput; objectName: "cutSelectedGraphicsChoice"
                Layout.fillWidth: true; enabled: dialog.editable
                checked: dialog.document.cutSelectedGraphics
                text: "Nur ausgewählte Grafiken ausgeben"
                contentItem: Text { text: selectedOutput.text; textFormat: Text.PlainText; color: dialog.ink; wrapMode: Text.WordWrap; leftPadding: selectedOutput.indicator.width + selectedOutput.spacing; verticalAlignment: Text.AlignVCenter }
                onClicked: dialog.document.setCutSelectedGraphics(checked)
            }
            CheckBox {
                id: selectionOrigin; objectName: "useSelectionOriginChoice"
                Layout.fillWidth: true
                enabled: dialog.editable && dialog.document.cutSelectedGraphics && dialog.document.startMode !== 0
                checked: dialog.document.useSelectionOrigin
                text: "Auswahl als Bezug für die neun Ursprungspunkte verwenden"
                contentItem: Text { text: selectionOrigin.text; textFormat: Text.PlainText; color: selectionOrigin.enabled ? dialog.ink : (dialog.dark ? "#9babb6" : "#657680"); wrapMode: Text.WordWrap; leftPadding: selectionOrigin.indicator.width + selectionOrigin.spacing; verticalAlignment: Text.AlignVCenter }
                onClicked: dialog.document.setUseSelectionOrigin(checked)
            }
            Label {
                objectName: "outputSelectionExplanation"; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: dialog.ink
                text: !dialog.document.cutSelectedGraphics ? "Ausgabe: alle aktiven Grafiken auf diesem Blatt. Die Auswahl im Editor schränkt den Auftrag nicht ein." : "Ausgabe: nur ausgewählte aktive Grafiken auf diesem Blatt. Eine leere oder nicht ausgebende Auswahl erzeugt keinen Auftrag." + (dialog.document.startMode === 0 ? " Absolute Zeichnungskoordinaten bleiben erhalten; der Auswahlursprung hat hier keine Wirkung." : dialog.document.useSelectionOrigin ? " Die Ursprungspunkte beziehen sich auf die Auswahl, um sie separat zu platzieren." : " Die Ursprungspunkte beziehen sich weiterhin auf das gesamte wirksame Design, damit eine Teilausgabe ihre ursprüngliche Lage behält.")
            }
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: dialog.ink; text: "Diese Auswahl ändert die Ausgabevorschau sofort. Zeichnungsobjekte bleiben an ihrer Position. Eine Rückfahrt nach Auftragsende wird damit nicht eingeschaltet." }
            ToolCombo { dark: dialog.dark;
                objectName: "startModeChoice"; Layout.fillWidth: true
                model: ["Absolute Zeichnungskoordinaten", "Aktuelle Position", "Gespeicherter Benutzerursprung"]
                currentIndex: dialog.document.startMode
                enabled: dialog.editable
                onActivated: index => dialog.document.setStartMode(index)
            }
            Label {
                objectName: "placementModeExplanation"; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: dialog.ink
                text: dialog.document.startMode === 0 ? "Absolut: Die Ausgabe folgt den Koordinaten der Zeichnung. Die neun Ankerpunkte haben hier keine Wirkung." : dialog.document.startMode === 1 ? "Aktuelle Position: Der gewählte Grafikanker wird an die ausdrücklich bekannte Kopfposition gesetzt." : "Benutzerursprung: Der Grafikanker wird an den gespeicherten und für diesen Koordinatenraum bestätigten Ursprung gesetzt."
            }
            RowLayout {
                Layout.fillWidth: true; spacing: 16
                GridLayout {
                    columns: 3; rowSpacing: 5; columnSpacing: 5
                    Repeater {
                        model: [{value:6,label:"↖"},{value:7,label:"↑"},{value:8,label:"↗"},{value:3,label:"←"},{value:4,label:"●"},{value:5,label:"→"},{value:0,label:"↙"},{value:1,label:"↓"},{value:2,label:"↘"}]
                        delegate: ActionButton {
                            required property var modelData
                            objectName: "jobOrigin" + modelData.value
                            text: modelData.label; implicitWidth: 48; implicitHeight: 42; dark: dialog.dark
                            selected: dialog.document.startMode !== 0 && dialog.document.jobOrigin === modelData.value
                            enabled: dialog.editable && dialog.document.startMode !== 0
                            onClicked: dialog.document.setJobOrigin(modelData.value)
                        }
                    }
                }
                Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: dialog.ink; text: "Anker der auszugebenden Grafik: Ecke, Kantenmitte oder Mitte. Leerfahrten und Rasterüberlauf verschieben diesen Grafikanker nicht; sie werden trotzdem auf Grenzüberschreitungen geprüft." }
            }
            Label {
                objectName: "previewHeadPosition"; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: dialog.ink
                text: dialog.simulation && dialog.summary.previewPositionKnown ? "Simulatorposition: X " + dialog.length(dialog.modelMachine.x) + " · Y " + dialog.length(dialog.modelMachine.y) : dialog.simulation ? "Keine aktuelle Simulatorposition bekannt. Den Simulator zuerst verbinden und einen laufenden Ablauf beenden." : "Keine freigegebene Vorschauposition für ein reales Gerät vorhanden. Gemeldete 0/0 sind kein Positionsnachweis."
            }
            ActionButton { objectName: "refreshPlacementPosition"; text: "Simulatorposition aktualisieren"; dark: dialog.dark; enabled: dialog.simulation && dialog.editable && dialog.modelMachine.connected && dialog.modelMachine.ready; onClicked: dialog.modelMachine.refreshPreviewPosition() }
            Label {
                visible: dialog.document.startMode === 2
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: dialog.ink
                text: dialog.summary.userOrigin.present ? "Gespeicherter Ursprung: X " + dialog.length(dialog.summary.userOrigin.x) + " · Y " + dialog.length(dialog.summary.userOrigin.y) + (dialog.summary.originConfirmed ? " · für diesen Vorschau-Koordinatenraum bestätigt" : " · noch nicht bestätigt") : "Noch kein Benutzerursprung gespeichert."
            }
            RowLayout {
                visible: dialog.document.startMode === 2; Layout.fillWidth: true
                ActionButton { objectName: "captureJobUserOrigin"; text: "Simulatorposition als Ursprung"; dark: dialog.dark; enabled: dialog.simulation && dialog.editable && dialog.summary.previewPositionKnown; onClicked: dialog.document.captureUserOrigin() }
                ActionButton { objectName: "confirmJobUserOrigin"; text: "Gespeicherten Ursprung bestätigen"; dark: dialog.dark; enabled: dialog.simulation && dialog.editable && dialog.summary.previewPositionKnown && dialog.summary.userOrigin.present && !dialog.summary.originConfirmed; onClicked: dialog.document.confirmUserOrigin() }
            }
            ActionButton { objectName: "clearJobUserOrigin"; text: "Gespeicherten Ursprung entfernen"; dark: dialog.dark; visible: dialog.document.startMode === 2; enabled: dialog.editable && dialog.summary.userOrigin.present; onClicked: dialog.document.clearUserOrigin() }
            Label { objectName: "placementValidation"; Layout.fillWidth: true; wrapMode: Text.WordWrap; textFormat: Text.PlainText; color: dialog.summary.valid ? dialog.ink : "#c05242"; text: dialog.summary.busy ? "Vorschau wird berechnet …" : dialog.summary.valid ? "Ausgabevorschau innerhalb der Arbeitsfläche. " + (dialog.summary.approachKnown ? "Anfahrt von bekannter Vorschauposition laserfrei geplant." : "Der Anfahrtsstart ist noch unbekannt; dies ist keine reale Freigabe.") : dialog.summary.reason || "Noch keine gültige Ausgabevorschau." }
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: dialog.ink; text: dialog.simulation ? "Diese Positionen gehören zum Simulator. Sie bestätigen weder einen realen Maschinenursprung noch Verfahrgrenzen oder Absaugung." : "Relative Ausgabe an realer Hardware ist noch nicht eingerichtet. Die Steuerung bleibt bis zur bestätigten Geräteeinrichtung gesperrt." }
        }
    }
    footer: RowLayout {
        Item { Layout.fillWidth: true }
        ActionButton { text: "Schließen"; dark: dialog.dark; onClicked: dialog.close() }
    }
}
