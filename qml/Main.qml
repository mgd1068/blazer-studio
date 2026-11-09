import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import BlazerStudio

ApplicationWindow {
    id: win
    width: 1480; height: 920
    minimumWidth: 1180; minimumHeight: 760
    visible: true
    title: (project.dirty ? "● " : "") + project.title + " — Blazer Studio"
    color: bg
    property bool night: false
    property int page: 0
    property int operation: 1
    property real step: 10
    property string pendingAction: ""
    readonly property color bg: night ? "#18232d" : "#f3f6f8"
    readonly property color panel: night ? "#202e39" : "#ffffff"
    readonly property color line: night ? "#34434f" : "#e3e9ee"
    readonly property color ink: night ? "#e2eaf0" : "#263b4a"
    readonly property color muted: night ? "#92a7b7" : "#81929f"
    readonly property var currentLayer: project.layers[operation]
    palette.windowText: ink
    palette.text: ink
    palette.base: panel
    palette.button: panel
    palette.buttonText: ink
    palette.highlight: "#209b76"
    font.family: "DejaVu Sans"
    font.pixelSize: 12

    function requestAction(action) {
        if (project.dirty) { pendingAction = action; discardDialog.open() }
        else performAction(action)
    }
    function performAction(action) {
        if (action === "demo") project.demo()
        else if (action === "new") project.newProject()
        else if (action === "open") openDialog.open()
        else if (action === "close") { machine.stop(); Qt.quit() }
    }
    function showManufacturerReference() { manufacturerDialog.open() }
    ManufacturerDialog {
        id: manufacturerDialog
        catalog: typeof manufacturerReference === "undefined" ? ({tables: []}) : manufacturerReference
        dark: win.night
    }
    onClosing: function(close) {
        machine.focus(false)
        if (project.dirty) { close.accepted = false; pendingAction = "close"; discardDialog.open() }
        else machine.stop()
    }
    onActiveChanged: if (!active) machine.focus(false)
    onPageChanged: machine.focus(false)
    Connections {
        target: project
        function onSelectionChanged() { if (project.selected >= 0) win.operation = project.selection.layer }
    }
    Shortcut { sequences: [StandardKey.Save]; onActivated: saveDialog.open() }
    Shortcut { sequences: [StandardKey.Open]; onActivated: win.requestAction("open") }
    Shortcut { sequences: [StandardKey.Undo]; onActivated: project.undo() }
    Shortcut { sequences: [StandardKey.Redo]; onActivated: project.redo() }
    Shortcut { sequence: "Ctrl+I"; onActivated: importDialog.open() }
    Shortcut { sequence: "Escape"; onActivated: { machine.stop(); project.select(-1) } }
    FileDialog { id: importDialog; title: "SVG importieren"; nameFilters: ["SVG-Grafiken (*.svg)"]; onAccepted: project.importSvg(selectedFile) }
    FileDialog { id: saveDialog; title: "Blazer-Projekt speichern"; fileMode: FileDialog.SaveFile; defaultSuffix: "blazer"; nameFilters: ["Blazer-Projekt (*.blazer)"]; onAccepted: project.save(selectedFile) }
    FileDialog { id: openDialog; title: "Blazer-Projekt öffnen"; nameFilters: ["Blazer-Projekt (*.blazer)"]; onAccepted: project.open(selectedFile) }
    Dialog {
        id: discardDialog
        width: 400
        anchors.centerIn: parent
        title: "Ungespeicherte Änderungen"
        modal: true
        standardButtons: Dialog.Discard | Dialog.Cancel
        contentItem: Text { text: "Änderungen verwerfen und fortfahren?"; color: win.ink; padding: 20 }
        onDiscarded: win.performAction(win.pendingAction)
    }

    // Application header and workflow tabs.
    header: Rectangle {
        height: 68; color: "#17232e"
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 24; anchors.rightMargin: 20; spacing: 12
            Rectangle {
                width: 32; height: 32; radius: 9; color: "#239c79"
                Text { anchors.centerIn: parent; text: "B"; color: "white"; font.pixelSize: 22; font.bold: true }
            }
            Column {
                spacing: 2
                Text { text: "blazer studio"; font.pixelSize: 18; font.weight: Font.DemiBold; color: "#f6f9fb"; font.letterSpacing: -.5 }
                Text { text: "LASER WORKSPACE"; color: "#89a0ad"; font.pixelSize: 8; font.letterSpacing: 1.8 }
            }
            Item { Layout.fillWidth: true }
            Rectangle {
                Layout.preferredWidth: 368; height: 40; radius: 7; color: "#22313e"
                Row {
                    anchors.fill: parent; anchors.margins: 4; spacing: 3
                    Repeater {
                        model: ["01  Vorbereiten", "02  Vorschau", "03  Gerät"]
                        delegate: Button {
                            required property string modelData
                            required property int index
                            objectName: "workflow" + index
                            width: 118; height: 32
                            onClicked: win.page = index
                            background: Rectangle { radius: 5; color: win.page === index ? "#405462" : "transparent" }
                            contentItem: Text { text: modelData; color: win.page === index ? "white" : "#9eb1be"; font.pixelSize: 11; font.weight: Font.Medium; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                        }
                    }
                }
            }
            Item { Layout.fillWidth: true }
            Rectangle {
                width: 92; height: 26; radius: 13; color: "#243b3c"
                Text { anchors.centerIn: parent; text: "●  SIMULATOR"; color: "#81c9b3"; font.pixelSize: 9; font.weight: Font.DemiBold }
            }
            ActionButton { text: win.night ? "Hell" : "Dunkel"; dark: true; compact: true; onClicked: win.night = !win.night }
        }
    }

    ColumnLayout {
        anchors.fill: parent; spacing: 0
        // Project toolbar.
        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 55; color: win.panel
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: win.line }
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 22; anchors.rightMargin: 20; spacing: 8
                Text { text: project.title; color: win.ink; font.pixelSize: 13; font.weight: Font.DemiBold }
                Text { text: project.dirty ? "●" : ""; color: "#d79b54"; font.pixelSize: 10 }
                Text { text: "/  Blatt 01"; color: win.muted; font.pixelSize: 11 }
                Item { Layout.fillWidth: true }
                ActionButton { text: "Neu"; dark: win.night; compact: true; onClicked: win.requestAction("new") }
                ActionButton { text: "Beispiel"; dark: win.night; compact: true; onClicked: win.requestAction("demo") }
                ActionButton { text: "Öffnen"; dark: win.night; compact: true; onClicked: win.requestAction("open") }
                ActionButton { text: "↓  SVG importieren"; dark: win.night; compact: true; onClicked: importDialog.open() }
                ActionButton { text: "Speichern"; dark: win.night; compact: true; onClicked: saveDialog.open() }
                Rectangle { width: 1; height: 22; color: win.line; Layout.leftMargin: 5; Layout.rightMargin: 5 }
                ActionButton {
                    text: win.page === 0 ? "Vorschau  →" : "▶  Demo starten"
                    primary: true; compact: true; enabled: win.page === 0 || (machine.connected && !machine.busy && !machine.firing && project.count > 0)
                    onClicked: { if (win.page === 0) win.page = 1; else { win.page = 2; machine.start() } }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 0
            // Left sidebar: setup, objects, operations.
            Rectangle {
                Layout.preferredWidth: 244; Layout.fillHeight: true; color: win.panel
                Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: win.line }
                ScrollView {
                    id: setupScroll
                    anchors.fill: parent
                    clip: true
                    contentWidth: availableWidth
                    contentHeight: setupColumn.implicitHeight + 36
                    ColumnLayout {
                    id: setupColumn
                    x: 18; y: 18
                    width: setupScroll.availableWidth - 36
                    spacing: 15
                    SectionLabel { text: "MASCHINE & MATERIAL"; dark: win.night }
                    Rectangle {
                        Layout.fillWidth: true; height: 78; radius: 7; color: win.night ? "#293e47" : "#f0f7f4"; border.color: win.night ? "#36554f" : "#d8e9e2"
                        Column {
                            anchors.fill: parent; anchors.margins: 12; spacing: 5
                            Text { text: "◈  " + deviceProfile.name; color: win.ink; font.pixelSize: 13; font.weight: Font.DemiBold }
                            Text { text: "200 × 150 mm  ·  GRBL"; color: win.muted; font.pixelSize: 11 }
                            Text { text: "Profil aus LightBurn"; color: "#299679"; font.pixelSize: 10 }
                        }
                    }
                    ColumnLayout {
                        spacing: 7; Layout.fillWidth: true
                        Text { text: "Material"; color: win.muted; font.pixelSize: 11 }
                        Rectangle {
                            Layout.fillWidth: true; height: 37; color: win.bg; radius: 5; border.color: win.line
                            Text { anchors.left: parent.left; anchors.leftMargin: 10; anchors.verticalCenter: parent.verticalCenter; text: "Sperrholz · 3 mm"; color: win.ink; font.pixelSize: 12 }
                        }
                        Text { text: "Beispiel · keine geprüften Schnittwerte"; color: win.muted; font.pixelSize: 9; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    }
                    ActionButton {
                        objectName: "manufacturerButton"
                        text: "Herstellerwerte ansehen"
                        dark: win.night; compact: true; Layout.fillWidth: true
                        onClicked: win.showManufacturerReference()
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: win.line }
                    RowLayout {
                        SectionLabel { text: "OBJEKTE"; dark: win.night; Layout.fillWidth: true }
                        Text { text: project.count + " Elemente"; color: win.muted; font.pixelSize: 10 }
                    }
                    ListView {
                        id: objectList
                        Layout.fillWidth: true; Layout.preferredHeight: Math.min(contentHeight, 230); Layout.minimumHeight: 65
                        clip: true; model: project; spacing: 3
                        delegate: Rectangle {
                            id: objectRow
                            required property int index
                            required property string artName
                            required property string artKind
                            required property color artColor
                            required property bool artVisible
                            width: objectList.width; height: 34; radius: 5
                            color: project.selected === index ? (win.night ? "#2e4845" : "#eaf5f0") : "transparent"
                            RowLayout {
                                anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 5; spacing: 8
                                Rectangle { width: 7; height: 7; radius: 2; color: objectRow.artColor }
                                Text { text: objectRow.artName; elide: Text.ElideRight; Layout.fillWidth: true; color: objectRow.artVisible ? win.ink : win.muted; font.pixelSize: 11 }
                                Text { text: objectRow.artKind === "text" ? "T" : objectRow.artKind === "svg" ? "SVG" : objectRow.artKind === "ellipse" ? "○" : "□"; color: win.muted; font.pixelSize: 11 }
                            }
                            MouseArea { anchors.fill: parent; onClicked: project.select(objectRow.index) }
                        }
                        ScrollBar.vertical: ScrollBar { }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 5
                        ActionButton { text: "+ Form"; dark: win.night; compact: true; Layout.fillWidth: true; onClicked: shapeMenu.popup() }
                        Menu {
                            id: shapeMenu
                            MenuItem { text: "Rechteck"; onTriggered: project.addShape("rect") }
                            MenuItem { text: "Ellipse"; onTriggered: project.addShape("ellipse") }
                            MenuItem { text: "Text"; onTriggered: project.addShape("text") }
                        }
                        ActionButton { text: "Kopie"; dark: win.night; compact: true; enabled: project.selected >= 0; onClicked: project.duplicateSelected() }
                        ActionButton { text: "−"; hint: "Ausgewähltes Objekt löschen"; dark: win.night; compact: true; enabled: project.selected >= 0; onClicked: project.removeSelected() }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: win.line }
                    SectionLabel { text: "BEARBEITUNGEN"; dark: win.night }
                    Repeater {
                        model: project.layers
                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            Layout.fillWidth: true; height: 57; radius: 6
                            color: win.operation === index ? win.bg : "transparent"; border.color: win.operation === index ? win.line : "transparent"
                            RowLayout {
                                anchors.fill: parent; anchors.margins: 8; spacing: 8
                                Rectangle { width: 3; height: 30; radius: 1; color: index === 0 ? "#cb713c" : "#4c7bd9" }
                                Column {
                                    Layout.fillWidth: true; spacing: 4
                                    Text { text: modelData.name; color: win.ink; font.pixelSize: 11; font.weight: Font.Medium }
                                    Text { text: modelData.power + " %  ·  " + modelData.speed + " mm/min"; color: win.muted; font.pixelSize: 9 }
                                }
                                Rectangle { width: 6; height: 6; radius: 3; color: modelData.enabled ? "#209b76" : win.muted }
                            }
                            MouseArea { anchors.fill: parent; onClicked: win.operation = index }
                        }
                    }
                    Item { Layout.fillHeight: true }
                    Text { text: "Blazer Studio · 0.1\nLokal auf deinem Rechner"; color: win.muted; font.pixelSize: 9; lineHeight: 1.5 }
                    }
                }
            }
            // Central workspace.
            ColumnLayout {
                Layout.fillWidth: true; Layout.fillHeight: true; spacing: 0
                Rectangle {
                    Layout.fillWidth: true; Layout.preferredHeight: 48; color: win.bg
                    RowLayout {
                        anchors.fill: parent; anchors.leftMargin: 18; anchors.rightMargin: 18; spacing: 6
                        ActionButton { text: "↖"; hint: "Auswählen und ziehen · 0,5-mm-Raster"; compact: true; selected: true; dark: win.night; enabled: win.page === 0; onClicked: project.notify("Objekte auf der Fläche auswählen und ziehen.") }
                        ActionButton { objectName: "addRectangle"; text: "□"; hint: "Rechteck"; compact: true; dark: win.night; enabled: win.page === 0; onClicked: project.addShape("rect") }
                        ActionButton { text: "○"; hint: "Ellipse"; compact: true; dark: win.night; enabled: win.page === 0; onClicked: project.addShape("ellipse") }
                        ActionButton { text: "T"; hint: "Text"; compact: true; dark: win.night; enabled: win.page === 0; onClicked: project.addShape("text") }
                        Rectangle { width: 1; height: 18; color: win.line; Layout.leftMargin: 3; Layout.rightMargin: 3 }
                        ActionButton { text: "↶"; hint: "Rückgängig · Strg+Z"; compact: true; dark: win.night; enabled: project.canUndo; onClicked: project.undo() }
                        ActionButton { text: "↷"; hint: "Wiederholen · Strg+Umschalt+Z"; compact: true; dark: win.night; enabled: project.canRedo; onClicked: project.redo() }
                        Item { Layout.fillWidth: true }
                        Text { text: win.page === 0 ? "ARBEITSFLÄCHE" : win.page === 1 ? "LAYOUTVORSCHAU" : "GERÄTESTEUERUNG"; color: win.muted; font.pixelSize: 9; font.letterSpacing: 1 }
                    }
                }
                StackLayout {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    currentIndex: win.page === 2 ? 1 : 0
                    Item {
                        Workspace { id: canvas; anchors.fill: parent; document: project; simulator: machine; preview: win.page === 1; dark: win.night }
                        Rectangle {
                            anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.margins: 20
                            width: 168; height: 30; radius: 15; color: win.panel; border.color: win.line
                            Text { anchors.centerIn: parent; text: win.page === 0 ? "Blatt 01  ·  200 × 150 mm" : "Reihenfolge der Objektliste"; color: win.muted; font.pixelSize: 10 }
                        }
                        Rectangle {
                            visible: win.page === 1
                            anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: zoomBar.top; anchors.bottomMargin: 12
                            width: Math.min(parent.width - 40, 510); height: 46; radius: 7; color: win.panel; border.color: win.line
                            Text { anchors.fill: parent; anchors.margins: 9; text: "Layoutvorschau mit Begrenzungen und Verbindungswegen.\nLaserpfade für Text/SVG und Füllgravur folgen im nächsten Ausbau."; color: win.muted; font.pixelSize: 10; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; wrapMode: Text.WordWrap }
                        }
                        Row {
                            id: zoomBar
                            anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter; anchors.bottomMargin: 16; spacing: 5
                            ActionButton { text: "−"; compact: true; dark: win.night; onClicked: canvas.zoomBy(1/1.2) }
                            ActionButton { text: Math.round(canvas.zoom * 100) + " %"; hint: "Auf Arbeitsfläche einpassen"; compact: true; dark: win.night; onClicked: canvas.fit() }
                            ActionButton { text: "+"; compact: true; dark: win.night; onClicked: canvas.zoomBy(1.2) }
                            ActionButton { text: "Einpassen"; compact: true; dark: win.night; onClicked: canvas.fit() }
                        }
                    }
                    ScrollView {
                        id: deviceScroll
                        clip: true; contentWidth: availableWidth
                        ColumnLayout {
                            width: deviceScroll.availableWidth; spacing: 16
                            Item { height: 4 }
                            ColumnLayout {
                                Layout.fillWidth: true; Layout.leftMargin: 28; Layout.rightMargin: 28; spacing: 15
                                RowLayout {
                                    Text { text: "Gerät & Positionierung"; color: win.ink; font.pixelSize: 22; font.weight: Font.DemiBold; Layout.fillWidth: true }
                                    ActionButton { text: machine.connected ? "Trennen" : "Simulator verbinden"; dark: win.night; onClicked: machine.toggleConnection() }
                                }
                                Text { text: "Simulierter Blaze X M3 · Es werden keine Befehle an Hardware gesendet."; color: win.muted; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                                Rectangle {
                                    Layout.fillWidth: true; height: 233; radius: 9; color: win.panel; border.color: win.line
                                    RowLayout {
                                        anchors.fill: parent; anchors.margins: 22; spacing: 26
                                        ColumnLayout {
                                            Layout.fillWidth: true; spacing: 12
                                            SectionLabel { text: "POSITIONIEREN"; dark: win.night }
                                            Text { text: "X  " + machine.x.toFixed(1) + "     Y  " + machine.y.toFixed(1) + " mm"; color: win.ink; font.pixelSize: 19; font.family: "DejaVu Sans Mono" }
                                            Text { text: "Schrittweite"; color: win.muted; font.pixelSize: 11 }
                                            Row {
                                                spacing: 4
                                                Repeater { model: [1,10,25]; delegate: ActionButton { required property int modelData; text: modelData + " mm"; dark: win.night; compact: true; selected: win.step === modelData; onClicked: win.step = modelData } }
                                            }
                                            ActionButton { text: "⌂  Referenzfahrt"; dark: win.night; enabled: machine.connected && !machine.busy && !machine.firing; onClicked: machine.home() }
                                        }
                                        GridLayout {
                                            columns: 3; rowSpacing: 7; columnSpacing: 7; enabled: machine.connected && !machine.busy && !machine.firing
                                            Item { width: 48; height: 46 }
                                            ActionButton { text: "↑"; implicitWidth: 48; implicitHeight: 46; dark: win.night; onClicked: machine.jog(0,win.step) }
                                            Item { width: 48; height: 46 }
                                            ActionButton { text: "←"; implicitWidth: 48; implicitHeight: 46; dark: win.night; onClicked: machine.jog(-win.step,0) }
                                            Rectangle { width: 48; height: 46; radius: 7; color: win.bg; Text { anchors.centerIn: parent; text: "XY"; color: win.muted; font.pixelSize: 11 } }
                                            ActionButton { text: "→"; implicitWidth: 48; implicitHeight: 46; dark: win.night; onClicked: machine.jog(win.step,0) }
                                            Item { width: 48; height: 46 }
                                            ActionButton { text: "↓"; implicitWidth: 48; implicitHeight: 46; dark: win.night; onClicked: machine.jog(0,-win.step) }
                                        }
                                    }
                                }
                                Rectangle {
                                    Layout.fillWidth: true; height: 125; radius: 9; color: win.panel; border.color: win.line
                                    RowLayout {
                                        anchors.fill: parent; anchors.margins: 22; spacing: 20
                                        ColumnLayout {
                                            Layout.fillWidth: true; spacing: 8
                                            SectionLabel { text: "POSITION & FOKUS"; dark: win.night }
                                            Text { text: machine.firing ? "2 % aktiv · simuliert" : "Fokuspunkt mit 2 %"; color: machine.firing ? "#d58d39" : win.ink; font.pixelSize: 17; font.weight: Font.DemiBold }
                                            Text { text: "Gedrückt halten · max. 3 Sekunden\nFokus manuell einstellen; Z-Achse laut Profil aus."; color: win.muted; font.pixelSize: 10 }
                                        }
                                        ActionButton { objectName: "focusHold"; text: "◎  Halten für 2 %"; primary: true; implicitHeight: 44; enabled: machine.connected && !machine.busy; onPressedChanged: machine.focus(pressed) }
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true; spacing: 8
                                    ActionButton { text: "□  Rahmenfahrt"; Layout.fillWidth: true; dark: win.night; enabled: machine.connected && !machine.busy && !machine.firing; onClicked: machine.frame() }
                                    ActionButton { text: machine.state === "Pausiert" ? "▶  Fortsetzen" : "Ⅱ  Pause"; Layout.fillWidth: true; dark: win.night; enabled: machine.state === "Läuft" || machine.state === "Pausiert"; onClicked: machine.pause() }
                                    ActionButton { text: "■  Stop"; Layout.fillWidth: true; dark: win.night; destructive: true; onClicked: machine.stop() }
                                }
                                ProgressBar { Layout.fillWidth: true; value: machine.progress; visible: machine.busy }
                                SectionLabel { text: "EREIGNISSE"; dark: win.night; Layout.topMargin: 6 }
                                Repeater { model: machine.log.slice(0,6); delegate: Text { required property string modelData; text: modelData; color: win.muted; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true } }
                                ActionButton { text: "Verbindungsfehler testen"; dark: win.night; compact: true; enabled: machine.connected; onClicked: machine.disconnectFault() }
                            }
                            Item { height: 20 }
                        }
                    }
                }
                Rectangle {
                    Layout.fillWidth: true; Layout.preferredHeight: 39; color: win.panel
                    Rectangle { height: 1; width: parent.width; color: win.line }
                    RowLayout {
                        anchors.fill: parent; anchors.leftMargin: 18; anchors.rightMargin: 18
                        Text { text: "●  " + (win.page === 2 ? machine.state : "Blatt 01"); color: "#209b76"; font.pixelSize: 10 }
                        Item { Layout.fillWidth: true }
                        Text { text: win.page === 2 ? "X " + machine.x.toFixed(1) + "  /  Y " + machine.y.toFixed(1) + " mm" : "Ziehen: 0,5 mm   ·   Mausrad: Zoom   ·   Mitteltaste: Verschieben"; color: win.muted; font.pixelSize: 9 }
                    }
                }
            }
            // Right sidebar: inspector and operation settings.
            Rectangle {
                Layout.preferredWidth: 278; Layout.fillHeight: true; color: win.panel
                Rectangle { width: 1; height: parent.height; color: win.line }
                ScrollView {
                    id: inspectorScroll
                    anchors.fill: parent; clip: true; contentWidth: availableWidth
                    ColumnLayout {
                        width: inspectorScroll.availableWidth; spacing: 15
                        ColumnLayout {
                            Layout.fillWidth: true; Layout.margins: 18; spacing: 15
                            RowLayout {
                                SectionLabel { text: "EINSTELLUNGEN"; dark: win.night; Layout.fillWidth: true }
                                Rectangle { width: 53; height: 21; radius: 4; color: win.bg; Text { anchors.centerIn: parent; text: "Einfach"; color: win.muted; font.pixelSize: 9 } }
                            }
                            Text { text: win.currentLayer.name; color: win.ink; font.pixelSize: 17; font.weight: Font.DemiBold }
                            Rectangle {
                                Layout.fillWidth: true; height: 31; radius: 5; color: win.bg
                                RowLayout {
                                    anchors.fill: parent; anchors.leftMargin: 9; anchors.rightMargin: 9
                                    Text { text: "Operation ausgeben"; color: win.ink; font.pixelSize: 11; Layout.fillWidth: true }
                                    Switch { checked: win.currentLayer.enabled; implicitHeight: 24; onToggled: project.setLayerSetting(win.operation,"enabled",checked) }
                                }
                            }
                            NumberField { label: "v"; value: win.currentLayer.speed; unit: "mm/min"; decimals: 0; dark: win.night; Layout.fillWidth: true; onEdited: n => project.setLayerSetting(win.operation,"speed",n) }
                            NumberField { label: "P"; value: win.currentLayer.power; unit: "%"; decimals: 1; dark: win.night; Layout.fillWidth: true; onEdited: n => project.setLayerSetting(win.operation,"power",n) }
                            NumberField { label: "×"; value: win.currentLayer.passes; unit: "Mal"; decimals: 0; dark: win.night; Layout.fillWidth: true; onEdited: n => project.setLayerSetting(win.operation,"passes",n) }
                            Text { text: "Entwurfswerte · Materialtest erforderlich"; color: win.muted; font.pixelSize: 9; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                            Rectangle { Layout.fillWidth: true; height: 1; color: win.line }
                            SectionLabel { text: "AUSGEWÄHLTES OBJEKT"; dark: win.night }
                            TextField {
                                Layout.fillWidth: true; enabled: project.selected >= 0
                                text: project.selection.name || "Nichts ausgewählt"
                                color: win.ink; font.pixelSize: 12; implicitHeight: 34; selectByMouse: true
                                background: Rectangle { radius: 5; color: win.bg; border.color: win.line }
                                onEditingFinished: project.setProperty("name",text)
                            }
                            GridLayout {
                                Layout.fillWidth: true; columns: 2; columnSpacing: 8; rowSpacing: 9
                                enabled: project.selected >= 0
                                NumberField { label: "X"; value: project.selection.x || 0; dark: win.night; Layout.fillWidth: true; onEdited: n => project.setProperty("x",n) }
                                NumberField { label: "Y"; value: project.selection.y || 0; dark: win.night; Layout.fillWidth: true; onEdited: n => project.setProperty("y",n) }
                                NumberField { label: "B"; value: project.selection.width || 0; dark: win.night; Layout.fillWidth: true; onEdited: n => project.setProperty("width",n) }
                                NumberField { label: "H"; value: project.selection.height || 0; dark: win.night; Layout.fillWidth: true; onEdited: n => project.setProperty("height",n) }
                            }
                            Text { text: "Objektkoordinaten: ab linker oberer Ecke"; color: win.muted; font.pixelSize: 9; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                            TextField {
                                visible: project.selection.kind === "text"; Layout.fillWidth: true
                                text: project.selection.text || ""; color: win.ink; font.pixelSize: 12; selectByMouse: true
                                background: Rectangle { radius: 5; color: win.bg; border.color: win.line }
                                onEditingFinished: project.setProperty("text",text)
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                ActionButton { text: "Gravur"; dark: win.night; compact: true; Layout.fillWidth: true; selected: project.selection.layer === 0; enabled: project.selected >= 0; onClicked: project.setLayer(0) }
                                ActionButton { text: "Schnitt"; dark: win.night; compact: true; Layout.fillWidth: true; selected: project.selection.layer === 1; enabled: project.selected >= 0; onClicked: project.setLayer(1) }
                            }
                            ActionButton { text: project.selection.visible ? "Objekt ausblenden" : "Objekt einblenden"; dark: win.night; compact: true; Layout.fillWidth: true; enabled: project.selected >= 0; onClicked: project.toggleVisible(project.selected) }
                            Rectangle { Layout.fillWidth: true; height: 1; color: win.line }
                            SectionLabel { text: "GERÄTEPROFIL"; dark: win.night }
                            Repeater {
                                model: [{k:"Verbindung",v:"GRBL · seriell"},{k:"Baudrate",v:"115200"},{k:"S-Max",v:"1000"},{k:"Fokuspunkt",v:"2 % / S20"},{k:"Z-Achse",v:"Deaktiviert"}]
                                delegate: RowLayout {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    Text { text: modelData.k; color: win.muted; font.pixelSize: 10; Layout.fillWidth: true }
                                    Text { text: modelData.v; color: win.ink; font.pixelSize: 10 }
                                }
                            }
                            Text { text: "Konfiguration aus LightBurn übernommen.\nNoch nicht live am Gerät verifiziert."; color: win.muted; font.pixelSize: 9; lineHeight: 1.4; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        }
                    }
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 29; color: "#17232e"
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 18; anchors.rightMargin: 18
                Text { text: project.message; color: "#a6bac8"; font.pixelSize: 9; elide: Text.ElideRight; Layout.fillWidth: true }
                Text { text: "SIMULATION · KEINE LASEREMISSION"; color: "#81c9b3"; font.pixelSize: 8; font.letterSpacing: .8 }
            }
        }
    }
}
