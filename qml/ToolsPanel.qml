import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ScrollView {
    id: tools
    objectName: "toolsPanel"
    property bool dark: false
    property int operation: 0
    readonly property color ink: dark ? "#e2eaf0" : "#263b4a"
    readonly property color muted: dark ? "#a5b9c7" : "#708594"
    readonly property var op: project.layers[Math.min(operation,project.layers.length-1)] || ({})
    signal importRequested()
    signal exportRequested()
    signal cameraRequested()
    signal csvRequested()
    signal nodesRequested()
    signal operationSelected(int index)
    property int section: 0
    property int nextCorner: 0
    property string imageDraftKey: ""
    // Planning progress also emits Document::changed. Cache the actual page
    // values so progress cannot rebuild an open menu or reset its target.
    property bool sheetsReady: false
    property string sheetModelKey: ""
    property int observedOutputPage: -1
    readonly property string sheetNamesKey: JSON.stringify(project.pages)
    readonly property int outputPage: project.activePage
    function refreshSheets() {
        if (!sheetsReady)return
        let names=project.pages
        let active=project.activePage
        let chosen=observedOutputPage===active ? page.currentIndex : active
        let key=JSON.stringify(names)
        if(sheetModelKey!==key) {
            sheetModelKey=key
            page.model=names.map((name,index)=>(index+1)+" · "+name)
        }
        observedOutputPage=active
        page.currentIndex=Math.max(0,Math.min(chosen,names.length-1))
    }
    onSheetNamesKeyChanged: refreshSheets()
    onOutputPageChanged: refreshSheets()
    function refreshImage() {
        let style=project.selection.style || ({})
        let key=(project.selection.id || "")+JSON.stringify(style)+project.selection.imageWidth+","+project.selection.imageHeight
        if(key===imageDraftKey)return
        imageDraftKey=key
        brightness.value=style.brightness ?? 0; contrast.value=style.contrast ?? 1; imageGamma.value=style.gamma ?? 1; sharpness.value=style.sharpness ?? 0
        invert.checked=style.invert ?? false;ellipseMask.checked=style.ellipseMask ?? false
        imageMode.currentIndex=Math.max(0,["dither","atkinson","threshold","grayscale"].indexOf(style.imageMode || "dither"))
        cropX.value=style.cropX ?? 0;cropY.value=style.cropY ?? 0;cropW.value=style.cropW ?? (project.selection.imageWidth || 1);cropH.value=style.cropH ?? (project.selection.imageHeight || 1)
    }
    Connections {target: project; function onSelectionChanged(){tools.refreshImage()} }
    Component.onCompleted: {refreshImage();sheetsReady=true;refreshSheets()}
    clip: true; contentWidth: availableWidth
    function apply(action,args) { project.command(action,args || ({})) }
    FontDialog { id: fonts; onAccepted: project.command("textStyle",{font:selectedFont.family,bold:selectedFont.bold,spacing:textSpacing.value}) }
    ColumnLayout {
        width: tools.availableWidth; spacing: 13
        Item { height: 6 }
        Text { text: "Werkzeuge & Produktion"; color: tools.ink; font.pixelSize: 22; Layout.leftMargin: 20 }
        TabBar {
            Layout.fillWidth: true
            currentIndex: tools.section; onCurrentIndexChanged: tools.section=currentIndex
            TabButton { HelpBubble { target: parent } id: toolsEditorTab; objectName: "toolsEditor"; text: "Editor"; background: Rectangle { radius: 4; color: toolsEditorTab.checked ? (tools.dark ? "#3b5361" : "#ffffff") : (tools.dark ? "#22323e" : "#e8eff3") } contentItem: Text { text: toolsEditorTab.text; color: tools.ink; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.pixelSize: 12 } }
            TabButton { HelpBubble { target: parent } id: toolsOperationsTab; objectName: "toolsOperations"; text: "Operationen"; background: Rectangle { radius: 4; color: toolsOperationsTab.checked ? (tools.dark ? "#3b5361" : "#ffffff") : (tools.dark ? "#22323e" : "#e8eff3") } contentItem: Text { text: toolsOperationsTab.text; color: tools.ink; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.pixelSize: 12 } }
            TabButton { HelpBubble { target: parent } id: toolsImageTab; objectName: "toolsImage"; text: "Bild"; background: Rectangle { radius: 4; color: toolsImageTab.checked ? (tools.dark ? "#3b5361" : "#ffffff") : (tools.dark ? "#22323e" : "#e8eff3") } contentItem: Text { text: toolsImageTab.text; color: tools.ink; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.pixelSize: 12 } }
            TabButton { HelpBubble { target: parent } id: toolsProductionTab; objectName: "toolsProduction"; text: "Produktion"; background: Rectangle { radius: 4; color: toolsProductionTab.checked ? (tools.dark ? "#3b5361" : "#ffffff") : (tools.dark ? "#22323e" : "#e8eff3") } contentItem: Text { text: toolsProductionTab.text; color: tools.ink; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.pixelSize: 12 } }
        }
        ColumnLayout {
            visible: tools.section===0
            Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20; spacing: 12
            Text { text: "Transformationen und Pfade"; color: tools.ink; font.pixelSize: 16 }
            RowLayout {
                Layout.fillWidth: true
                ActionButton { text: "Linie"; dark: tools.dark; onClicked: tools.apply("addLine") }
                ActionButton { text: "Bézier"; dark: tools.dark; onClicked: tools.apply("addBezier") }
                ActionButton { text: "Knoten bearbeiten"; dark: tools.dark; onClicked: {tools.apply("convert");tools.nodesRequested()} }
            }
            RowLayout {
                Layout.fillWidth: true
                NumberField { id: rotation; label: "↻"; value: 90; unit: "°"; dark: tools.dark; Layout.fillWidth: true; onEdited: n=>value=n }
                ActionButton { objectName: "rotateSelection"; text: "Drehen"; dark: tools.dark; onClicked: tools.apply("rotate",{value:rotation.value}) }
                ActionButton { text: "Spiegeln X"; dark: tools.dark; onClicked: tools.apply("mirrorX") }
                ActionButton { text: "Y"; dark: tools.dark; onClicked: tools.apply("mirrorY") }
            }
            RowLayout {
                Layout.fillWidth: true
                ToolCombo { dark: tools.dark; id: align; Layout.fillWidth: true; model: ["Links","Rechts","Unten","Oben","Mitte X","Mitte Y","Verteilen X","Verteilen Y"] }
                ActionButton { text: "Ausrichten"; dark: tools.dark; onClicked: tools.apply(["alignLeft","alignRight","alignBottom","alignTop","alignCenterX","alignCenterY","distributeX","distributeY"][align.currentIndex]) }
            }
            RowLayout {
                Layout.fillWidth: true
                ToolCombo { dark: tools.dark; id: pathAction; Layout.fillWidth: true; model: ["In Pfade umwandeln","Vereinigen","Differenz","Schnittmenge","Verbinden","Schließen","Teilpfade zerlegen","Vereinfachen","Richtung umkehren"] }
                ActionButton { text: "Anwenden"; dark: tools.dark; onClicked: tools.apply(["convert","union","difference","intersection","join","closePath","split","simplify","reverse"][pathAction.currentIndex]) }
            }
            Text { text: "Pfadumwandlung macht Text zu Konturen. Die Differenz zieht spätere ausgewählte Objekte vom ersten ab. Alle Änderungen mit Undo."; color: tools.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true; font.pixelSize: 10 }
            RowLayout {
                Layout.fillWidth: true
                NumberField { id: offset; label: "Δ"; value: 1; dark: tools.dark; unit: "mm"; Layout.fillWidth: true; onEdited: n=>value=n }
                ActionButton { text: "Konturversatz"; dark: tools.dark; onClicked: tools.apply("offset",{value:offset.value}) }
            }
            SectionLabel { text: "KNOTEN NUMERISCH"; dark: tools.dark }
            SpinBox { HelpBubble { target: parent } id: node; from: 0; to: Math.max(0,project.nodes.length-1); editable: true; Layout.fillWidth: true }
            RowLayout {
                Layout.fillWidth: true
                NumberField { id: nx; label: "X"; value: project.nodes[node.value] ? project.nodes[node.value].x : 0; dark: tools.dark; Layout.fillWidth: true; onEdited: n=>project.setNode(node.value,n,ny.value) }
                NumberField { id: ny; label: "Y"; value: project.nodes[node.value] ? project.nodes[node.value].y : 0; dark: tools.dark; Layout.fillWidth: true; onEdited: n=>project.setNode(node.value,nx.value,n) }
            }
            SectionLabel { text: "TEXT"; dark: tools.dark }
            NumberField { id: textSpacing; label: "↔"; value: 0; unit: "px"; dark: tools.dark; Layout.fillWidth: true; onEdited: n=>value=n }
            RowLayout {
                Layout.fillWidth: true
                ActionButton { text: "Schrift und Stil"; dark: tools.dark; onClicked: {let style=project.selection.style || ({});fonts.selectedFont=Qt.font({family:style.font || "DejaVu Sans",bold:style.bold || false});fonts.open()} }
                ActionButton { text: "Text auf Pfad"; dark: tools.dark; onClicked: tools.apply("textOnPath") }
            }
        }
        ColumnLayout {
            visible: tools.section===1
            Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20; spacing: 10
            Text { text: "Freie Operationen"; color: tools.ink; font.pixelSize: 16 }
            ToolCombo { dark: tools.dark; id: opChoice; objectName: "operationChoice"; model: project.layers.map(op=>op.name); currentIndex: Math.min(tools.operation,count-1); Layout.fillWidth: true; onActivated: index=>tools.operationSelected(index) }
            TextField { HelpBubble { target: parent } id: opName; objectName: "newOperationName"; placeholderText: "Name der neuen Operation"; text: "Feingravur"; Layout.fillWidth: true }
            RowLayout {
                Layout.fillWidth: true
                ToolCombo { dark: tools.dark; id: newMode; model: ["Gravur","Schnitt"]; Layout.fillWidth: true }
                ActionButton { objectName: "addOperation"; text: "+ Operation"; dark: tools.dark; onClicked: {let i=project.addOperation(opName.text,newMode.currentText);if(i>=0)tools.operationSelected(i)} }
                ActionButton { text: "Umbenennen"; dark: tools.dark; onClicked: project.setLayerSetting(tools.operation,"name",opName.text) }
            }
            RowLayout { Layout.fillWidth: true
                ActionButton { text: "↑"; dark: tools.dark; enabled: tools.operation>2; onClicked: {if(project.manageOperation(tools.operation,"up"))tools.operationSelected(tools.operation-1)} }
                ActionButton { text: "↓"; dark: tools.dark; enabled: tools.operation>=2&&tools.operation<project.layers.length-1; onClicked: {if(project.manageOperation(tools.operation,"down"))tools.operationSelected(tools.operation+1)} }
                ActionButton { text: "Operation löschen"; dark: tools.dark; enabled: tools.operation>=2; onClicked: {if(project.manageOperation(tools.operation,"delete"))tools.operationSelected(0)} }
            }
            Text { text: "Die zwei Basisoperationen erhalten die Zuordnung der Materialprofile; zusätzliche Operationen sind frei verwaltbar."; color: tools.muted; wrapMode: Text.WordWrap; font.pixelSize: 10; Layout.fillWidth: true }
            Text { text: "Operationen für die Auswahl: Reihenfolge der Häkchen bestimmt Haupt- und zusätzliche Bearbeitung. Eigene Objektwerte gelten für die Hauptoperation."; color: tools.muted; wrapMode: Text.WordWrap; font.pixelSize: 10; Layout.fillWidth: true }
            Repeater {
                model: project.layers
                CheckBox { HelpBubble { target: parent }
                    required property var modelData; required property int index
                    objectName: "operationAssign"+index
                    text: modelData.name; checked: (project.selection.operations || []).indexOf(index)>=0
                    onClicked: {let rows=(project.selection.operations || []).slice();let pos=rows.indexOf(index);if(checked&&pos<0)rows.push(index);else if(!checked&&pos>=0)rows.splice(pos,1);project.assignOperations(rows)}
                }
            }
            GridLayout {
                columns: 2; Layout.fillWidth: true
                Text { text: "Scanwinkel"; color: tools.ink }
                NumberField { hint: "Richtung der Gravurlinien in Grad: 0 waagerecht, 90 senkrecht."; value: tools.op.angle || 0; unit: "°"; dark: tools.dark; Layout.fillWidth: true; onEdited: n=>project.setLayerSetting(tools.operation,"angle",n) }
                Text { text: "Winkel je Durchgang"; color: tools.ink }
                NumberField { hint: "Zusätzliche Drehung der Gravurlinien je Wiederholung in Grad."; value: tools.op.angleIncrement || 0; unit: "°"; dark: tools.dark; Layout.fillWidth: true; onEdited: n=>project.setLayerSetting(tools.operation,"angleIncrement",n) }
                Text { text: "Kreuzschraffur"; color: tools.ink }
                CheckBox { HelpBubble { target: parent } checked: tools.op.crosshatch || false; onClicked: project.setLayerSetting(tools.operation,"crosshatch",checked) }
                Text { text: "Überlauf / Randweg"; color: tools.ink }
                NumberField { hint: "Zusätzlicher Fahrweg ohne Laser vor und nach einer Gravurlinie in Millimetern. Benötigt freien Platz."; value: tools.op.overscan || 0; unit: "mm"; dark: tools.dark; Layout.fillWidth: true; onEdited: n=>project.setLayerSetting(tools.operation,"overscan",n) }
                Text { text: "Versatz Rückrichtung"; color: tools.ink }
                NumberField { hint: "Gemessene Korrektur zwischen hin- und rücklaufenden Gravurlinien in Millimetern."; value: tools.op.scanOffset || 0; unit: "mm"; decimals: 3; dark: tools.dark; Layout.fillWidth: true; onEdited: n=>project.setLayerSetting(tools.operation,"scanOffset",n) }
                Text { text: "Schnittfuge (±)"; color: tools.ink }
                NumberField { hint: "Korrigiert die Schnittkontur um die gemessene Schnittfugenbreite. Positiv nach außen, negativ nach innen."; value: tools.op.kerf || 0; unit: "mm"; decimals: 3; dark: tools.dark; Layout.fillWidth: true; onEdited: n=>project.setLayerSetting(tools.operation,"kerf",n) }
                Text { text: "Haltestege / Anzahl"; color: tools.ink }
                SpinBox { HelpBubble { target: parent } from: 0; to: 100; value: tools.op.tabs || 0; onValueModified: project.setLayerSetting(tools.operation,"tabs",value) }
                Text { text: "Breite der Haltestege"; color: tools.ink }
                NumberField { hint: "Breite der ungeschnittenen Haltestege in Millimetern."; value: tools.op.tabWidth || 1; unit: "mm"; dark: tools.dark; Layout.fillWidth: true; onEdited: n=>project.setLayerSetting(tools.operation,"tabWidth",n) }
                Text { text: "Einlauf / Auslauf"; color: tools.ink }
                RowLayout { Layout.fillWidth: true
                    NumberField { hint: "Fahrweg zum Start einer Schnittkontur in Millimetern."; value: tools.op.leadIn || 0; dark: tools.dark; Layout.fillWidth: true; onEdited: n=>project.setLayerSetting(tools.operation,"leadIn",n) }
                    NumberField { hint: "Fahrweg nach dem Ende einer Schnittkontur in Millimetern."; value: tools.op.leadOut || 0; dark: tools.dark; Layout.fillWidth: true; onEdited: n=>project.setLayerSetting(tools.operation,"leadOut",n) }
                }
                Text { text: "Konturstart / Anteil"; color: tools.ink }
                NumberField { hint: "Startposition entlang einer geschlossenen Kontur in Prozent."; value: tools.op.startPoint || 0; unit: "%"; dark: tools.dark; Layout.fillWidth: true; onEdited: n=>project.setLayerSetting(tools.operation,"startPoint",n) }
                Text { text: "Nächsten Start wählen"; color: tools.ink }
                CheckBox { HelpBubble { target: parent } checked: tools.op.optimize || false; onClicked: project.setLayerSetting(tools.operation,"optimize",checked) }
            }
            Text { text: "Überlauf, Ein-/Auslauf und Schnittfuge können den Platzbedarf vergrößern. Ausgabe außerhalb des Arbeitsraums wird vollständig abgelehnt."; color: tools.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true; font.pixelSize: 10 }
        }
        ColumnLayout {
            visible: tools.section===2
            Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20; spacing: 12
            Text { text: "Rasterbild bearbeiten"; color: tools.ink; font.pixelSize: 16 }
            ToolCombo { dark: tools.dark; id: imageMode; objectName: "imageModeChoice"; model: ["Floyd-Steinberg","Atkinson","Schwellenwert","Graustufen / variable Leistung"]; Layout.fillWidth: true }
            Text { text: "B: Helligkeit −1…1 · K: Kontrast 0…3 · γ: Gamma 0,1…5 · S: Schärfung 0…3"; color: tools.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true; font.pixelSize: 10 }
            NumberField { id: brightness; hint: "Bildhelligkeit: 0 unverändert, negative Werte dunkler, positive heller."; label: "B"; value: 0; unit: "±1"; dark: tools.dark; Layout.fillWidth: true; onEdited: n=>value=n }
            NumberField { id: contrast; hint: "Bildkontrast: 1 unverändert; größere Werte trennen hell und dunkel stärker."; label: "K"; value: 1; unit: "×"; dark: tools.dark; Layout.fillWidth: true; onEdited: n=>value=n }
            NumberField { id: imageGamma; label: "γ"; value: 1; unit: "×"; dark: tools.dark; Layout.fillWidth: true; onEdited: n=>value=n }
            NumberField { id: sharpness; hint: "Bildschärfung: 0 unverändert. Prüfe feine Kanten in der Vorschau."; label: "S"; value: 0; unit: "×"; dark: tools.dark; Layout.fillWidth: true; onEdited: n=>value=n }
            CheckBox { HelpBubble { target: parent } id: invert; text: "Bild invertieren" }
            CheckBox { HelpBubble { target: parent } id: ellipseMask; text: "Elliptische Maske" }
            ActionButton { objectName: "imageApply"; enabled: project.selection.kind==="image"; text: "Bildwerte übernehmen"; dark: tools.dark; onClicked: tools.apply("imageSettings",{imageMode:["dither","atkinson","threshold","grayscale"][imageMode.currentIndex],brightness:brightness.value,contrast:contrast.value,gamma:imageGamma.value,sharpness:sharpness.value,invert:invert.checked,ellipseMask:ellipseMask.checked}) }
            GridLayout { columns: 2; Layout.fillWidth: true
                NumberField { id: cropX; label: "X"; value: 0; unit: "px"; decimals: 0; dark: tools.dark; onEdited: n=>value=n; Layout.fillWidth: true }
                NumberField { id: cropY; label: "Y"; value: 0; unit: "px"; decimals: 0; dark: tools.dark; onEdited: n=>value=n; Layout.fillWidth: true }
                NumberField { id: cropW; label: "B"; value: 100; unit: "px"; decimals: 0; dark: tools.dark; onEdited: n=>value=n; Layout.fillWidth: true }
                NumberField { id: cropH; label: "H"; value: 100; unit: "px"; decimals: 0; dark: tools.dark; onEdited: n=>value=n; Layout.fillWidth: true }
            }
            ActionButton { text: "Zuschnitt setzen"; enabled: project.selection.kind==="image"; dark: tools.dark; onClicked: tools.apply("imageSettings",{cropX:cropX.value,cropY:cropY.value,cropW:cropW.value,cropH:cropH.value}) }
            RowLayout { Layout.fillWidth: true
                ActionButton { text: "Bild drehen"; dark: tools.dark; onClicked: tools.apply("imageRotate") }
                ActionButton { text: "Spiegeln"; dark: tools.dark; onClicked: tools.apply("imageMirror") }
                ActionButton { text: "Maskenform"; dark: tools.dark; onClicked: tools.apply("imageMask") }
            }
            RowLayout { Layout.fillWidth: true
                ActionButton { text: "Vektorisieren"; dark: tools.dark; onClicked: tools.apply("trace") }
                ActionButton { text: "Als Reliefkarte"; dark: tools.dark; onClicked: tools.apply("imageToRelief") }
            }
            Text { text: "Vektorisierung zeichnet dunkle Bildflächen nach. Als Reliefkarte übernimmt nur die Helligkeit; es rekonstruiert keine räumliche Form aus einem Foto."; wrapMode: Text.WordWrap; color: tools.muted; Layout.fillWidth: true; font.pixelSize: 10 }
        }
        ColumnLayout {
            visible: tools.section===3
            Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20; spacing: 12
            Text { text: "Arbeitsblätter und Serien"; color: tools.ink; font.pixelSize: 16 }
            Text { objectName: "activeOutputPage"; text: "Aktive Ausgabe: " + (project.activePage+1) + " · " + project.pages[project.activePage]; textFormat: Text.PlainText; color: tools.ink; wrapMode: Text.Wrap; Layout.fillWidth: true }
            ToolCombo { objectName: "pageTarget"; dark: tools.dark; id: page; Layout.fillWidth: true }
            Text { text: "Gewähltes Zielblatt. Anzeige und Ausgabe wechseln erst mit Anzeigen / ausgeben. Operationen gelten projektweit."; color: tools.muted; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            ActionButton { objectName: "activatePageButton"; text: "Anzeigen / ausgeben"; dark: tools.dark; onClicked: project.activatePage(page.currentIndex) }
            TextField { HelpBubble { target: parent; description: "Name für ein neues Blatt, eine vollständige Blattkopie oder das gewählte Blatt. Maximal 200 Zeichen." } objectName: "pageNameEditor"; id: pageName; text: "Neues Blatt"; maximumLength: 200; Layout.fillWidth: true; selectByMouse: true }
            RowLayout { Layout.fillWidth: true
                ActionButton { objectName: "addPageButton"; text: "+ Blatt"; dark: tools.dark; onClicked: project.addPage(pageName.text) }
                ActionButton { objectName: "copyPageButton"; text: "Blatt kopieren"; dark: tools.dark; onClicked: project.copyPage(page.currentIndex,pageName.text) }
            }
            RowLayout { Layout.fillWidth: true
                ActionButton { objectName: "renamePageButton"; text: "Umbenennen"; dark: tools.dark; onClicked: project.renamePage(page.currentIndex,pageName.text) }
                ActionButton { objectName: "removePageButton"; text: "Blatt löschen"; dark: tools.dark; destructive: true; enabled: project.pages.length>1; hint: "Löscht das gewählte Blatt mit seinem gesamten Inhalt. Rückgängig stellt es wieder her."; onClicked: project.removePage(page.currentIndex) }
            }
            ActionButton { objectName: "copySelectionPageButton"; text: "Auswahl ins Zielblatt kopieren"; dark: tools.dark; enabled: project.selectedRows.length>0; onClicked: tools.apply("copyToPage",{page:page.currentIndex}) }
            NumberField { id: copies; hint: "Gesamtzahl der gewünschten Kopien."; label: "×"; value: 4; unit: "Stk"; decimals: 0; dark: tools.dark; Layout.fillWidth: true; onEdited: n=>value=n }
            NumberField { id: columns; hint: "Anzahl der Spalten im Kopienraster."; label: "↔"; value: 2; unit: "Sp."; decimals: 0; dark: tools.dark; Layout.fillWidth: true; onEdited: n=>value=n }
            NumberField { id: gap; hint: "Freier Abstand zwischen den kopierten Objekten in Millimetern."; label: "Δ"; value: 2; unit: "mm"; dark: tools.dark; Layout.fillWidth: true; onEdited: n=>value=n }
            CheckBox { HelpBubble { target: parent } id: allowRotation; text: "Beim Nesting 90°-Drehung erlauben" }
            CheckBox { HelpBubble { target: parent } id: moreSheets; text: "Bei Bedarf weitere Arbeitsblätter erzeugen"; checked: true }
            RowLayout { Layout.fillWidth: true
                ActionButton { text: "Kopien als Raster"; dark: tools.dark; onClicked: tools.apply("duplicateGrid",{copies:copies.value,columns:columns.value,gap:gap.value}) }
                ActionButton { text: "Auswahl verschachteln"; dark: tools.dark; onClicked: tools.apply("nest",{gap:gap.value,rotate:allowRotation.checked,multipage:moreSheets.checked}) }
            }
            TextField { HelpBubble { target: parent } id: qrText; placeholderText: "QR-Inhalt"; Layout.fillWidth: true }
            ActionButton { text: "QR-Code erzeugen"; dark: tools.dark; onClicked: tools.apply("qr",{text:qrText.text}) }
            Text { text: "Variable Textfelder: {Spaltenname} aus CSV sowie {number}. Stapelexport erzeugt einzelne GCode-Dateien; keine unbeaufsichtigte Jobfolge."; wrapMode: Text.WordWrap; Layout.fillWidth: true; color: tools.muted; font.pixelSize: 10 }
            ActionButton { text: "CSV-Stapel exportieren"; dark: tools.dark; onClicked: tools.csvRequested() }
            RowLayout { Layout.fillWidth: true
                ActionButton { text: "Maßprobe"; dark: tools.dark; onClicked: tools.apply("calibration",{kind:"scale"}) }
                ActionButton { text: "Rasterprobe"; dark: tools.dark; onClicked: tools.apply("calibration",{kind:"interval"}) }
                ActionButton { text: "Fokusmarken"; dark: tools.dark; onClicked: tools.apply("calibration",{kind:"focus"}) }
            }
            RowLayout { Layout.fillWidth: true
                ActionButton { text: "Datei importieren"; dark: tools.dark; onClicked: tools.importRequested() }
                ActionButton { text: "SVG exportieren"; dark: tools.dark; onClicked: tools.exportRequested() }
            }
            Text { text: "DXF (ASCII), SVG, HPGL/PLT-Grundpfade, GCode-Geometrie, PDF/AI über Inkscape, Rasterbilder, STL/OBJ und unterstützte LightBurn-/MillMage-Grundformen und eingebettete PNG-Bilder. Nicht unterstützte Inhalte werden gemeldet."; wrapMode: Text.WordWrap; Layout.fillWidth: true; color: tools.muted; font.pixelSize: 10 }
            SectionLabel { text: "REFERENZFOTO / 4-PUNKT-REGISTRIERUNG"; dark: tools.dark }
            ActionButton { text: "Foto laden"; dark: tools.dark; enabled: typeof cameraRegistration!=="undefined"; onClicked: {tools.nextCorner=0;tools.cameraRequested()} }
            Text { text: "Pixelkoordinaten der vier Arbeitsraum-Ecken: links oben, rechts oben, rechts unten, links unten. Das Foto muss die reale 200 × 150-mm-Fläche zeigen. Keine automatische Kameraaufnahme oder Linsenentzerrung."; color: tools.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true; font.pixelSize: 10 }
            Image {
                id: referencePreview; objectName: "referencePreview"; Layout.fillWidth: true; Layout.preferredHeight: 190
                fillMode: Image.PreserveAspectFit
                source: typeof cameraRegistration!=="undefined" ? cameraRegistration.source : ""
                MouseArea { anchors.fill: parent; hoverEnabled: true; ToolTip.visible: containsMouse; ToolTip.text: "Nacheinander die vier Ecken der Arbeitsfläche anklicken: unten links, unten rechts, oben rechts, oben links. Danach Kalibrieren wählen."; ToolTip.delay: 650; onClicked: mouse=>{let x=(mouse.x-(referencePreview.width-referencePreview.paintedWidth)/2)/referencePreview.paintedWidth*cameraRegistration.imageWidth;let y=(mouse.y-(referencePreview.height-referencePreview.paintedHeight)/2)/referencePreview.paintedHeight*cameraRegistration.imageHeight;if(isFinite(x)&&isFinite(y)&&x>=0&&y>=0&&x<=cameraRegistration.imageWidth&&y<=cameraRegistration.imageHeight){corners.itemAt(tools.nextCorner%4).setCoordinates(x,y);tools.nextCorner++}} }
            }
            Text { text: "Im Foto die vier Ecken der Reihe nach anklicken; nächster Punkt: P"+(tools.nextCorner%4+1); color: tools.muted; font.pixelSize: 10 }
            Repeater { id: corners; model: 4
                RowLayout { required property int index; Layout.fillWidth: true
                    Text { text: "P"+(index+1); color: tools.ink }
                    NumberField { id: px; hint: "Waagerechte Lage der Foto-Ecke in Bildpixeln."; property string role: "x"; value: typeof cameraRegistration!=="undefined" && (index===1||index===2) ? cameraRegistration.imageWidth : 0; unit: "px"; dark: tools.dark; Layout.fillWidth: true; onEdited: n=>value=n }
                    NumberField { id: py; hint: "Senkrechte Lage der Foto-Ecke in Bildpixeln."; property string role: "y"; value: typeof cameraRegistration!=="undefined" && index>=2 ? cameraRegistration.imageHeight : 0; unit: "px"; dark: tools.dark; Layout.fillWidth: true; onEdited: n=>value=n }
                    function coordinates() { return [px.value,py.value] }
                    function setCoordinates(x,y) {px.value=x;py.value=y}
                }
            }
            RowLayout { Layout.fillWidth: true
                ActionButton { objectName: "registerCamera"; text: "Kalibrieren"; dark: tools.dark; enabled: typeof cameraRegistration!=="undefined"; onClicked: {let points=[];for(let i=0;i<4;i++)points=points.concat(corners.itemAt(i).coordinates());if(!cameraRegistration.calibrate(points,[0,150,200,150,200,0,0,0]))project.notify("Vier nicht kollineare Eckpunkte im Foto erforderlich.")} }
                CheckBox { HelpBubble { target: parent } text: "Foto einblenden"; enabled: typeof cameraRegistration!=="undefined" && cameraRegistration.calibrated; checked: typeof cameraRegistration!=="undefined" && cameraRegistration.visible; onClicked: cameraRegistration.visible=checked }
            }
            ActionButton { text: "Letzte Sicherung wiederherstellen"; dark: tools.dark; enabled: typeof recovery!=="undefined" && recovery.available; onClicked: recovery.restore() }
        }
        Item { height: 20 }
    }
}
