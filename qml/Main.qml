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
    readonly property bool simulationMode: typeof machine.simulation === "undefined" || machine.simulation
    property bool basic: true
    property bool proofConfirmed: false
    property string probeKey: ""
    readonly property string parameterKey: project.deviceInfo.outputFingerprint+JSON.stringify(project.material)+JSON.stringify(project.layers)
    readonly property bool isBasicProbe: probeKey===parameterKey && project.count===1 && project.selection.width===5 && project.selection.height===5
    onParameterKeyChanged: proofConfirmed=false
    onBasicChanged: if(basic && page>2)page=0
    property bool night: false
    property int page: 0
    property int operation: 1
    property string frameScope: "job"
    property var frameRegion: ({valid:false,reason:"Rahmenziel wird berechnet."})
    function refreshFraming() { frameRegion=project.framingBounds(frameScope) }
    onFrameScopeChanged: refreshFraming()
    property real step: 10
    property real selectedModuleW: project.deviceInfo.modulePowerW || 0
    readonly property real appliedModuleW: project.material.moduleW || project.deviceInfo.modulePowerW || 0
    onAppliedModuleWChanged: selectedModuleW = appliedModuleW
    readonly property var toolHeadPowers: { let names = materialLibrary.names; return materialLibrary.modulePowers() }
    readonly property var toolHeadNames: toolHeadPowers.map(watts => watts + " W" + (watts === project.deviceInfo.modulePowerW ? " · eingerichtet" : ""))
    readonly property var materialRows: { let names = materialLibrary.names; return materialLibrary.rowsForModule(selectedModuleW) }
    readonly property var materialNames: materialRows.map(row => materialLibrary.profileAt(row).name)
    readonly property int materialRow: materialRows.indexOf(libraryIndex)
    readonly property bool projectHeadMatches: appliedModuleW === selectedModuleW
    property string pendingAction: ""
    property url pendingProjectFile
    property var saveDialog: null
    property url lastProjectSaveFolder
    readonly property bool textInputFocused: activeFocusItem !== null && typeof activeFocusItem.copy === "function" && typeof activeFocusItem.selectAll === "function"
    readonly property bool clipboardDialogActive: projectDialogActive || importDialog.visible || imageDialog.visible || reliefDialog.visible || cameraPhoto.visible || photoDepth.visible || universalImport.visible || vectorExport.visible || imageExport.visible || gcodeDialog.visible || csvImport.visible || csvDirectory.visible || diagnosticsDialog.visible || onboarding.visible || betaHelp.visible || manufacturerDialog.visible || materialEditor.visible || deleteMaterialDialog.visible || materialTestDialog.visible
    readonly property bool projectDialogActive: historyDialog.visible || recoveryDialog.visible || discardDialog.visible || projectDetails.visible || recentProjects.visible || projectMenu.visible || (saveDialog && saveDialog.visible) || openDialog.visible || saveTemplateDialog.visible || openTemplateDialog.visible
    readonly property string lengthUnit: typeof appServices!=="undefined" && appServices && appServices.lengthUnit==="in" ? "in" : "mm"
    property bool discardCloseApproved: false
    readonly property color bg: night ? "#18232d" : "#f3f6f8"
    readonly property color panel: night ? "#202e39" : "#ffffff"
    readonly property color line: night ? "#34434f" : "#e3e9ee"
    readonly property color ink: night ? "#e2eaf0" : "#263b4a"
    readonly property color muted: night ? "#a7bbc9" : "#607583"
    readonly property var currentLayer: {
        let selected = project.layers[operation]
        if (basic && (!selected || !selected.enabled)) {
            for (let layer of project.layers)
                if (layer.enabled) return layer
        }
        return selected || project.layers[0]
    }
    palette.window: panel
    palette.windowText: ink
    palette.text: ink
    palette.base: panel
    palette.button: panel
    palette.buttonText: ink
    palette.highlight: "#209b76"
    font.family: "DejaVu Sans"
    font.pixelSize: 12

    Component { id: dialogHelpFactory; HelpBubble {} }
    function helpDialogButton(dialog, buttonType, description) {
        let button=dialog.standardButton(buttonType)
        if(button) {
            let names=({})
            names[Dialog.Yes]="Ja"; names[Dialog.No]="Nein"; names[Dialog.Save]="Speichern"; names[Dialog.Discard]="Verwerfen"; names[Dialog.Cancel]="Abbrechen"
            if(names[buttonType])button.text=names[buttonType]
            dialogHelpFactory.createObject(button,{target:button,description:description})
        }
    }
    function offerFirstRun() { if(typeof appServices!=="undefined" && appServices.firstRun && !Qt.application.arguments.some(arg=>["--screenshot","--export-gcode","--self-test"].indexOf(arg)>=0))onboarding.open() }
    function resumeRecoveryDecision() { if(typeof recovery!=="undefined" && recovery.pendingDecision) Qt.callLater(recoveryDialog.open) }
    function finishSave(file) {
        let action=win.pendingAction
        win.pendingAction=""
        if(!project.save(file)) { win.pendingProjectFile=""; win.resumeRecoveryDecision(); return false }
        if(action.length>0)win.performAction(action)
        return true
    }
    function showSaveDialog() {
        if(saveDialog && saveDialog.visible)return
        if(saveDialog && saveDialog.used) {
            let old = saveDialog
            win.lastProjectSaveFolder = old.currentFolder
            old.retired = true
            if(typeof appServices !== "undefined" && appServices)appServices.reclaimFileDialogResources(old,win)
            old.objectName = ""
            saveDialog = null
            old.destroy()
        }
        if(!saveDialog)saveDialog = saveDialogFactory.createObject(win)
        if(!saveDialog) { win.pendingAction="";win.pendingProjectFile="";project.notify("Speicherdialog konnte nicht geöffnet werden.");return }
        saveDialog.used = true
        if(win.lastProjectSaveFolder.toString().length>0)saveDialog.currentFolder=win.lastProjectSaveFolder
        if(project.fileUrl.toString().length>0)saveDialog.selectedFile=project.fileUrl
        saveDialog.open()
    }
    ProjectDialog {
        id: projectDetails
        document: project
        dark: win.night
        onAboutToShow: machine.focus(false)
    }
    HistoryDialog { id: historyDialog; document: project; dark: win.night; onAboutToShow: machine.focus(false) }
    RecentProjectsDialog {
        id: recentProjects
        service: typeof appServices !== "undefined" ? appServices : null
        dark: win.night
        onAboutToShow: machine.focus(false)
        onProjectRequested: file => { win.pendingProjectFile = file; win.requestAction("recent") }
    }
    OnboardingDialog {
        id: onboarding; onAboutToShow: machine.focus(false); dark: win.night; service: typeof appServices!=="undefined" ? appServices : null
        onPracticeRequested: {
            if(machine.busy || machine.firing) { project.notify("Bitte den laufenden Geräteauftrag zuerst beenden."); return }
            if(typeof machine.useSimulator==="function")machine.useSimulator()
            win.basic=true; win.page=0; win.requestAction("demo")
        }
    }
    BetaHelpDialog {
        id: betaHelp; onAboutToShow: machine.focus(false); dark: win.night; service: typeof appServices!=="undefined" ? appServices : null
        onIntroductionRequested: onboarding.open()
        onDiagnosticsRequested: diagnosticsDialog.open()
    }
    FileDialog {
        id: diagnosticsDialog; title: "Technischen Diagnosebericht speichern"; fileMode: FileDialog.SaveFile; defaultSuffix: "json"; nameFilters: ["JSON-Diagnosebericht (*.json)"]
        onAccepted: if(typeof appServices!=="undefined")appServices.exportDiagnostics(selectedFile)
    }
    function revealObjectSelection() {
        if (project.selected < 0) return
        let rows = project.selectedRows
        objectList.positionViewAtIndex(rows[rows.length-1], ListView.Contain)
        objectList.positionViewAtIndex(rows[0], ListView.Contain)
        let delegate = objectList.itemAtIndex(rows[0])
        let viewport = setupScroll.contentItem
        if (delegate && viewport) {
            let point = delegate.mapToItem(viewport, 0, 0)
            let delta = point.y < 8 ? point.y - 8 : point.y + delegate.height > viewport.height - 8 ? point.y + delegate.height - viewport.height + 8 : 0
            viewport.contentY = Math.max(0, Math.min(viewport.contentHeight - viewport.height, viewport.contentY + delta))
        }
    }
    function selectObject(index) {
        // Commit the focused editor before changing the selected object.
        objectList.forceActiveFocus(Qt.MouseFocusReason)
        project.select(index)
        page = !basic && project.selection.kind === "relief" ? 3 : 0
        canvas.ensureSelectionVisible()
        objectList.positionViewAtIndex(project.selected, ListView.Contain)
    }
    function importMotif(file) {
        let useProfile = basic && !project.beginnerSummary.requiresAdvanced && !project.beginnerSummary.modified
        if(project.importFile(file) && useProfile)project.applyBasicMaterial(project.material)
    }
    function addShape(kind) {
        project.addShape(kind)
        if(basic && project.layers[0].enabled!==project.layers[1].enabled)
            project.setLayer(project.layers[0].enabled ? 0 : 1)
    }
    function requestAction(action) {
        if (project.dirty) { pendingAction = action; discardDialog.open() }
        else performAction(action)
    }
    function performAction(action) {
        if (action === "recent") {
            let file = win.pendingProjectFile
            win.pendingProjectFile = ""
            if(typeof appServices !== "undefined" && !appServices.openRecentProject(file) && appServices.message.length > 0) project.notify(appServices.message)
            return
        }
        win.pendingProjectFile = ""
        if (action === "demo") project.demo()
        else if (action === "new") project.newProject()
        else if(action==="basicprobe") {
            let profile=JSON.parse(JSON.stringify(project.material))
            project.newProject();project.applyBasicMaterial(profile);project.addShape("rect")
            project.setProperty("width",5);project.setProperty("height",5);project.setProperty("x",10);project.setProperty("y",10)
            project.setProperty("name","Materialprobe 5 mm")
            project.setLayer(profile.engrave && profile.engrave.enabled ? 0 : 1)
            win.probeKey=win.parameterKey;win.page=1
        }
        else if (action === "materialtest") project.createMaterialTest(testMinSpeed.numericValue(),testMaxSpeed.numericValue(),testMinPower.numericValue(),testMaxPower.numericValue(),testSteps.value,win.selectedModuleW)
        else if (action === "open") openDialog.open()
        else if (action === "template") openTemplateDialog.open()
        else if (action === "close") {
            discardCloseApproved = true
            win.close()
            discardCloseApproved = false
        }
    }
    readonly property int libraryIndex: {
        let names = materialLibrary.names
        return materialLibrary.indexOf(project.material.id || "")
    }
    function applyLibraryProfile(index) {
        let profile = materialLibrary.profileAt(index)
        if (profile.id) {
            selectedModuleW = profile.moduleW
            if(basic)project.applyBasicMaterial(profile);else project.applyMaterial(profile)
        }
    }
    function editMaterial(asNew) {
        let profile = projectHeadMatches ? (libraryIndex >= 0 ? materialLibrary.profileAt(libraryIndex) : project.material) : ({})
        if (!profile.id) {
            profile = materialLibrary.newProfile()
            profile.moduleW = selectedModuleW
        }
        materialEditor.edit(profile, asNew)
    }
    MaterialEditor {
        id: materialEditor
        library: materialLibrary; dark: win.night
        onAboutToShow: machine.focus(false)
        onSaved: profileId => win.applyLibraryProfile(materialLibrary.indexOf(profileId))
    }
    Dialog {
        id: deleteMaterialDialog
        Component.onCompleted: {
            win.helpDialogButton(deleteMaterialDialog,Dialog.Yes,"Löscht dieses Profil aus der Bibliothek. Die Projektwerte bleiben erhalten.")
            win.helpDialogButton(deleteMaterialDialog,Dialog.No,"Behält das Profil und schließt diese Rückfrage.")
        }
        title: "Materialprofil löschen?"
        width: 430; anchors.centerIn: parent; modal: true
        standardButtons: Dialog.Yes | Dialog.No
        contentItem: Text { text: "Das Profil wird aus der eigenen Bibliothek entfernt.\nGeöffnete Projekte behalten ihre übernommenen Werte."; color: win.ink; wrapMode: Text.WordWrap; padding: 18 }
        onAccepted: materialLibrary.removeProfile(project.material.id)
        onAboutToShow: machine.focus(false)
    }
    function showManufacturerReference() { manufacturerDialog.open() }
    ManufacturerDialog {
        id: manufacturerDialog
        catalog: typeof manufacturerReference === "undefined" ? ({tables: []}) : manufacturerReference
        dark: win.night
        onAboutToShow: machine.focus(false)
    }
    onClosing: function(close) {
        machine.focus(false)
        if (project.dirty && !discardCloseApproved) { close.accepted = false; pendingAction = "close"; discardDialog.open() }
        else { machine.stop(); if(typeof recovery!=="undefined") recovery.closeSession() }
    }
    onActiveChanged: if (!active) machine.focus(false)
    onPageChanged: { machine.focus(false); if(basic && page>2)basic=false }
    Connections {
        target: project
        function onChanged() { if(win.operation>=project.layers.length)win.operation=0;win.refreshFraming() }
        function onSelectionChanged() {
            win.refreshFraming()
            if (project.selected >= 0) {
                if (project.selection.layer >= 0) win.operation = project.selection.layer
                Qt.callLater(win.revealObjectSelection)
            }
        }
    }
    Shortcut { sequences: [StandardKey.Copy]; enabled: !clipboardDialogActive && !win.textInputFocused; onActivated: project.copySelectionToClipboard() }
    Shortcut { sequences: [StandardKey.Cut]; enabled: !clipboardDialogActive && !win.textInputFocused; onActivated: project.copySelectionToClipboard(true) }
    Shortcut { sequences: [StandardKey.Paste]; enabled: !clipboardDialogActive && !win.textInputFocused; onActivated: project.pasteFromClipboard() }
    Shortcut { sequence: "Ctrl+Shift+V"; enabled: !clipboardDialogActive && !win.textInputFocused; onActivated: project.pasteFromClipboard(true) }
    Shortcut { sequences: [StandardKey.Save]; enabled: !projectDialogActive; onActivated: win.showSaveDialog() }
    Shortcut { sequences: [StandardKey.Open]; enabled: !projectDialogActive; onActivated: win.requestAction("open") }
    Shortcut { sequences: [StandardKey.Undo]; enabled: !clipboardDialogActive && !win.textInputFocused; onActivated: project.undo() }
    Shortcut { sequences: [StandardKey.Redo]; enabled: !clipboardDialogActive && !win.textInputFocused; onActivated: project.redo() }
    Shortcut { sequence: "Ctrl+I"; enabled: !projectDialogActive; onActivated: importDialog.open() }
    Shortcut { sequence: "Escape"; enabled: !recoveryDialog.visible && !projectDialogActive; onActivated: { canvas.cancelGuideDrag(); canvas.selectedGuideId=""; machine.stop(); project.select(-1) } }
    FileDialog { id: importDialog; objectName: "importMotifDialog"; onVisibleChanged: if (visible) machine.focus(false); title: "Grafik, GCode oder Modell importieren"; nameFilters: ["Geometrie, Bilder und Modelle (*.svg *.dxf *.hpgl *.plt *.gc *.gcode *.nc *.rd *.pdf *.ai *.eps *.png *.jpg *.jpeg *.bmp *.gif *.tif *.tiff *.webp *.stl *.obj *.lbrn *.lbrn2 *.mage)"]; onAccepted: win.importMotif(selectedFile) }
    Component {
        id: saveDialogFactory
        FileDialog {
            objectName: "saveProjectDialog"
            parentWindow: win
            property bool used: false
            property bool retired: false
            onVisibleChanged: if (visible) machine.focus(false)
            title: "Blazer-Projekt speichern"
            fileMode: FileDialog.SaveFile
            defaultSuffix: "blazer"
            nameFilters: ["Blazer-Projekt (*.blazer)"]
            onAccepted: if(!retired)win.finishSave(selectedFile)
            onRejected: if(!retired) {win.pendingAction="";win.pendingProjectFile="";win.resumeRecoveryDecision()}
        }
    }
    FileDialog { id: openDialog; objectName: "openProjectDialog"; onVisibleChanged: if (visible) machine.focus(false); title: "Blazer-Projekt öffnen"; nameFilters: ["Blazer-Projekt (*.blazer)"]; onAccepted: project.open(selectedFile) }
    FileDialog { id: saveTemplateDialog; objectName: "saveTemplateDialog"; onVisibleChanged: if (visible) machine.focus(false); title: "Als Blazer-Vorlage speichern"; fileMode: FileDialog.SaveFile; defaultSuffix: "blazertemplate"; nameFilters: ["Blazer-Vorlage (*.blazertemplate)"]; onAccepted: project.saveTemplate(selectedFile) }
    FileDialog { id: openTemplateDialog; objectName: "openTemplateDialog"; onVisibleChanged: if (visible) machine.focus(false); title: "Neues Projekt aus Vorlage"; nameFilters: ["Blazer-Vorlage (*.blazertemplate)"]; onAccepted: if(project.newFromTemplate(selectedFile)) {win.proofConfirmed=false;win.probeKey="";win.page=0} }
    FileDialog {
        id: imageExport; objectName: "imageExportDialog"
        property bool processed: true
        property string imageId: ""
        property var revision: 0
        title: processed ? "Bearbeitetes Bild exportieren" : "Bild ohne aktuelle Korrekturen exportieren"
        fileMode: FileDialog.SaveFile
        nameFilters: ["PNG mit Transparenz (*.png)", "JPEG mit weißem Hintergrund (*.jpg *.jpeg)", "BMP mit weißem Hintergrund (*.bmp)"]
        defaultSuffix: selectedNameFilter.index===1 ? "jpg" : selectedNameFilter.index===2 ? "bmp" : "png"
        onVisibleChanged: if (visible) machine.focus(false)
        onAccepted: project.exportImage(selectedFile,processed,imageId,revision)
    }
    FileDialog { id: gcodeDialog; objectName: "exportGcodeDialog"; title: "GRBL-GCode exportieren"; fileMode: FileDialog.SaveFile; defaultSuffix: "gcode"; nameFilters: ["GRBL-GCode (*.gcode)"]; onAccepted: project.exportGcode(selectedFile); onVisibleChanged: if (visible) machine.focus(false) }
    FileDialog { id: imageDialog; title: "Bild importieren"; nameFilters: ["Bilder (*.png *.jpg *.jpeg *.bmp)"]; onAccepted: project.importImage(selectedFile); onVisibleChanged: if (visible) machine.focus(false) }
    FileDialog { id: reliefDialog; title: "Relief-Tiefenkarte importieren"; nameFilters: ["Tiefenkarten (*.png *.jpg *.jpeg *.bmp)"]; onAccepted: { if (project.importRelief(selectedFile)) win.page = 3 } onVisibleChanged: if (visible) machine.focus(false) }
    FileDialog { id: cameraPhoto; title: "Referenzfoto des Arbeitsraums"; nameFilters: ["Bilder (*.png *.jpg *.jpeg)"]; onAccepted: {if(!cameraRegistration.load(selectedFile))project.notify("Referenzfoto ungültig.")} }
    FileDialog { id: photoDepth; title: "Foto für lokale Tiefenschätzung"; nameFilters: ["Bilder (*.png *.jpg *.jpeg *.webp)"]; onAccepted: depthService.estimate(selectedFile) }
    property url csvPath
    FileDialog { id: universalImport; title: "Grafik, GCode, Modell oder Projekt importieren"; nameFilters: ["Unterstützte Dateien (*.svg *.dxf *.hpgl *.plt *.gc *.gcode *.nc *.rd *.pdf *.ai *.eps *.png *.jpg *.jpeg *.bmp *.gif *.tif *.tiff *.webp *.stl *.obj *.blazer *.lbrn *.lbrn2 *.mage)"]; onAccepted: project.importFile(selectedFile) }
    FileDialog { id: vectorExport; title: "Vektoren exportieren"; fileMode: FileDialog.SaveFile; defaultSuffix: "svg"; onAccepted: project.exportSvg(selectedFile) }
    FileDialog { id: csvImport; title: "CSV für variable Textfelder"; nameFilters: ["CSV (*.csv)"]; onAccepted: {win.csvPath=selectedFile;csvDirectory.open()} }
    FolderDialog { id: csvDirectory; title: "Ausgabeverzeichnis für neue GCode-Dateien"; onAccepted: project.batch(win.csvPath,selectedFolder,"") }
    Dialog {
        id: recoveryDialog; focus: true; objectName: "recoveryDialog"; height: 260; width: Math.min(parent.width-48,600); title: "Automatische Sicherung gefunden"; anchors.centerIn: parent; modal: true
        contentItem: Text { text: "Ein früherer Projektstand ist noch gesichert. Möchtest du ihn wiederherstellen? Beim Beenden bleibt die Sicherung für den nächsten Start erhalten."; color: win.ink; wrapMode: Text.WordWrap; padding: 20 }
        onRejected: win.close()
        footer: DialogButtonBox {
            ActionButton { objectName: "restoreRecovery"; text: "Wiederherstellen"; primary: true; hint: "Lädt die gespeicherten Projektdaten. Anschließend separat speichern."; onClicked: { if(recovery.restore()) { recoveryDialog.close(); Qt.callLater(win.offerFirstRun) } } }
            ActionButton { objectName: "discardRecovery"; text: "Sicherung verwerfen"; dark: win.night; destructive: true; hint: "Entfernt die automatische Sicherung ausdrücklich und öffnet das Startprojekt."; onClicked: { recovery.clear(); if(!recovery.pendingDecision) { recoveryDialog.close(); Qt.callLater(win.offerFirstRun) } } }
            ActionButton { objectName: "keepRecovery"; text: "Beenden und behalten"; dark: win.night; hint: "Beendet das Programm. Die Sicherung bleibt für den nächsten Start erhalten."; onClicked: win.close() }
        }
    }
    Component.onCompleted: { win.refreshFraming(); saveDialog=saveDialogFactory.createObject(win); if(typeof recovery!=="undefined" && recovery.available)recoveryDialog.open(); else win.offerFirstRun() }
    Dialog {
        id: materialTestDialog
        objectName: "materialTestDialog"
        anchors.centerIn: parent; width: Math.min(parent.width-40,520); modal: true
        title: "Materialtest vorbereiten"
        contentItem: ColumnLayout {
            spacing: 12
            Text { text: "5-mm-Gravurfelder mit 8 mm Abstand ab X10/Y10. Zeilen: Geschwindigkeit, Spalten: Leistung. Erzeugt ein neues bearbeitbares Projekt; kein automatischer Laserstart."; wrapMode: Text.WordWrap; Layout.fillWidth: true; color: win.ink }
            NumberField { id: testMinSpeed; label: "v↓"; value: 1000; unit: "mm/min"; onEdited: n => value=n; Layout.fillWidth: true; dark: win.night }
            NumberField { id: testMaxSpeed; label: "v↑"; value: 3000; unit: "mm/min"; onEdited: n => value=n; Layout.fillWidth: true; dark: win.night }
            NumberField { id: testMinPower; label: "P↓"; value: 10; unit: "%"; onEdited: n => value=n; Layout.fillWidth: true; dark: win.night }
            NumberField { id: testMaxPower; label: "P↑"; value: 50; unit: "%"; onEdited: n => value=n; Layout.fillWidth: true; dark: win.night }
            RowLayout { Text { text: "Stufen je Achse"; color: win.ink; Layout.fillWidth: true } SpinBox { HelpBubble { target: parent } id: testSteps; from: 2; to: 10; value: 4 } }
        }
        footer: DialogButtonBox {
            ActionButton { text: "Abbrechen"; onClicked: materialTestDialog.close(); dark: win.night }
            ActionButton { objectName: "prepareMaterialTestButton"; text: "Projekt erzeugen"; primary: true; onClicked: { materialTestDialog.close(); win.requestAction("materialtest") } }
        }
    }
    Dialog {
        id: discardDialog
        objectName: "discardDialog"
        Component.onCompleted: {
            standardButton(Dialog.Discard).objectName = "discardChangesButton"
            standardButton(Dialog.Cancel).objectName = "cancelCloseButton"
            standardButton(Dialog.Save).objectName = "saveChangesButton"
            win.helpDialogButton(discardDialog,Dialog.Save,"Speichert das Projekt und führt die angeforderte Aktion nur nach erfolgreichem Speichern aus.")
            win.helpDialogButton(discardDialog,Dialog.Discard,"Verwirft die ungespeicherten Änderungen und führt die angeforderte Aktion aus.")
            win.helpDialogButton(discardDialog,Dialog.Cancel,"Bricht die angeforderte Aktion ab. Deine Änderungen bleiben geöffnet; du kannst sie zuerst speichern.")
        }
        width: 400
        anchors.centerIn: parent
        title: "Ungespeicherte Änderungen"
        modal: true
        standardButtons: Dialog.Save | Dialog.Discard | Dialog.Cancel
        contentItem: Text { text: "Das Projekt enthält ungespeicherte Änderungen. Speichern, verwerfen oder zur Bearbeitung zurückkehren?"; color: win.ink; padding: 20 }
        onAccepted: win.showSaveDialog()
        onDiscarded: {
            let action = win.pendingAction
            win.pendingAction = ""
            close()
            win.performAction(action)
        }
        onRejected: { win.pendingAction = ""; win.pendingProjectFile = ""; win.resumeRecoveryDecision() }
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
            ActionButton { objectName: "modeToggle"; text: win.basic ? "Basic → Advanced" : "Advanced → Basic"; hint: "Basic führt durch einen einfachen Auftrag mit Profilwerten. Advanced zeigt alle Bearbeitungswerkzeuge. Der Wechsel verändert das Projekt nicht."; dark: win.night; onClicked: win.basic=!win.basic }
            Rectangle {
                Layout.preferredWidth: win.basic ? 368 : 610; height: 40; radius: 7; color: "#22313e"
                Row {
                    anchors.fill: parent; anchors.margins: 4; spacing: 3
                    Repeater {
                        model: win.basic ? ["01  Vorbereiten", "02  Vorschau", "03  Gerät"] : ["01  Vorbereiten", "02  Vorschau", "03  Gerät", "04  Relief", "05  Werkzeuge"]
                        delegate: Button { HelpBubble { target: parent }
                            required property string modelData
                            required property int index
                            objectName: "workflow" + index
                            text: modelData
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
                Text { anchors.centerIn: parent; text: win.simulationMode ? "●  SIMULATOR" : "●  GRBL"; color: "#81c9b3"; font.pixelSize: 11; font.weight: Font.DemiBold }
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
                ActionButton {
                    id: projectDetailsButton
                    objectName: "projectDetailsButton"
                    Layout.maximumWidth: win.width < 1280 ? 140 : 220
                    Layout.minimumWidth: 60
                    Layout.fillWidth: true
                    text: project.title + " ▾"
                    dark: win.night
                    compact: true
                    hint: "Öffnet Projektangaben und Vorlagen. Eine Vorlage lässt sich als neues, unabhängiges Projekt verwenden."
                    contentItem: Text { text: projectDetailsButton.text; textFormat: Text.PlainText; elide: Text.ElideRight; color: win.ink; font.pixelSize: 13; font.weight: Font.DemiBold; verticalAlignment: Text.AlignVCenter }
                    onClicked: projectMenu.open()
                    Menu {
                        id: projectMenu
                        objectName: "projectMenu"
                        y: projectDetailsButton.height
                        width: 280
                        onAboutToShow: machine.focus(false)
                        MenuItem { objectName: "projectMetadataMenuItem"; text: "Projektangaben …"; onTriggered: projectDetails.open() }
                        MenuItem { objectName: "historyMenuItem"; text: "Bearbeitungsverlauf …"; onTriggered: historyDialog.open() }
                        MenuSeparator {}
                        MenuItem { objectName: "newTemplateMenuItem"; text: "Neu aus Vorlage …"; onTriggered: win.requestAction("template") }
                        MenuItem { objectName: "saveTemplateMenuItem"; text: "Als Vorlage speichern …"; onTriggered: saveTemplateDialog.open() }
                    }
                }
                Text { text: project.dirty ? "●" : ""; color: "#d79b54"; font.pixelSize: 12 }
                Text {
                    id: activePageLabel
                    objectName: "activePageIndicator"
                    text: "/  " + (project.activePage+1) + " · " + project.pages[project.activePage]
                    textFormat: Text.PlainText; color: win.muted; font.pixelSize: 11
                    Layout.fillWidth: true; Layout.minimumWidth: 40; Layout.preferredWidth: 150; Layout.maximumWidth: 150
                    elide: Text.ElideRight
                    HoverHandler { id: pageHover }
                    ToolTip {
                        id: pageNameTip; objectName: "activePageTooltip"; parent: activePageLabel; visible: pageHover.hovered; delay: 650
                        x: 0; y: activePageLabel.height + 6
                        text: "Aktives Blatt für Vorschau und Ausgabe: " + project.pages[project.activePage]
                        width: Math.min(380,implicitWidth)
                        background: Rectangle { radius: 5; color: win.panel; border.color: win.line }
                        contentItem: Text { text: pageNameTip.text; textFormat: Text.PlainText; color: win.ink; wrapMode: Text.Wrap }
                    }
                }
                Item { Layout.fillWidth: true }
                ActionButton { text: "Neu"; dark: win.night; compact: true; onClicked: win.requestAction("new") }
                ActionButton { text: "Beispiel"; dark: win.night; compact: true; onClicked: win.requestAction("demo") }
                ActionButton { objectName: "openProjectButton"; text: "Öffnen"; dark: win.night; compact: true; onClicked: win.requestAction("open") }
                ActionButton { objectName: "recentProjectsButton"; text: "Zuletzt"; dark: win.night; compact: true; visible: typeof appServices !== "undefined"; onClicked: recentProjects.open() }
                ActionButton { objectName: "importMotifButton"; text: win.basic ? "Motiv laden" : "↓  Importieren"; hint: "Lädt ein Bild oder eine Vektorzeichnung als Motiv. Das allein startet noch keinen Laser."; dark: win.night; compact: true; onClicked: importDialog.open() }
                ActionButton { objectName: "importImageButton"; visible: !win.basic; text: "Bild"; dark: win.night; compact: true; onClicked: imageDialog.open() }
                ActionButton { objectName: "exportGcodeButton"; text: "GCode"; visible: !win.basic; dark: win.night; compact: true; enabled: project.jobSummary.valid && project.jobSummary.outputSupported; onClicked: gcodeDialog.open() }
                ActionButton { objectName: "betaHelpButton"; text: "Hilfe / Beta"; compact: true; dark: win.night; hint: "Öffnet Einführung, bekannte Einschränkungen, Versionsdaten und den freiwilligen Diagnoseexport."; onClicked: betaHelp.open() }
                ActionButton { objectName: "saveProjectButton"; text: "Speichern"; dark: win.night; compact: true; onClicked: win.showSaveDialog() }
                Rectangle { width: 1; height: 22; color: win.line; Layout.leftMargin: 5; Layout.rightMargin: 5 }
                ActionButton {
                    objectName: "startJobButton"; text: win.page === 0 ? "Vorschau  →" : win.simulationMode ? (win.basic ? "▶  Am Bildschirm testen" : "▶  Simulation starten") : "▶  Am Laser starten"
                    primary: true; compact: true; enabled: win.page === 0 || (machine.ready && !project.planningBusy && project.jobSummary.valid && (!win.basic || win.simulationMode || (!project.beginnerSummary.requiresAdvanced && !project.beginnerSummary.modified && win.projectHeadMatches && (win.proofConfirmed || win.isBasicProbe))))
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
                            Text { objectName: "deviceProfileName"; width: parent.width; elide: Text.ElideRight; textFormat: Text.PlainText; text: "◈  " + project.deviceInfo.name; color: win.ink; font.pixelSize: 13; font.weight: Font.DemiBold }
                            Text { width: parent.width; elide: Text.ElideRight; textFormat: Text.PlainText; text: project.bedWidth+" × "+project.bedHeight+" mm  ·  "+project.deviceInfo.controller; color: win.muted; font.pixelSize: 11 }
                            Text { text: "Projektprofil"; color: "#299679"; font.pixelSize: 12 }
                        }
                    }
                    ColumnLayout {
                        spacing: 7; Layout.fillWidth: true
                        Text { text: "Laseraufsatz / Toolkopf"; color: win.muted; font.pixelSize: 11 }
                        ComboBox { HelpBubble { target: parent }
                            id: toolHeadChoice
                            objectName: "toolHeadChoice"
                            Layout.fillWidth: true
                            implicitHeight: 38
                            leftPadding: 10; rightPadding: 10
                            model: win.toolHeadNames
                            // ComboBox selects row 0 during a model reset; restore the bound selection afterwards.
                            onModelChanged: Qt.callLater(function() { toolHeadChoice.currentIndex = Qt.binding(function() { return win.toolHeadPowers.indexOf(win.selectedModuleW) }) })
                            currentIndex: win.toolHeadPowers.indexOf(win.selectedModuleW)
                            displayText: currentIndex >= 0 ? currentText : win.selectedModuleW > 0 ? win.selectedModuleW + " W" : "Leistung unbekannt"
                            onActivated: index => win.selectedModuleW = win.toolHeadPowers[index]
                            contentItem: Text { text: toolHeadChoice.displayText; color: win.ink; font: toolHeadChoice.font; verticalAlignment: Text.AlignVCenter; rightPadding: toolHeadChoice.indicator.width + 8 }
                            indicator: Text { x: toolHeadChoice.width - width - 10; anchors.verticalCenter: parent.verticalCenter; text: "▾"; color: win.ink; font.pixelSize: 14 }
                            background: Rectangle { color: win.night ? "#202d37" : "#f7f9fa"; radius: 5; border.color: toolHeadChoice.activeFocus ? "#209b76" : win.line }
                        }
                        Text { text: "Material"; color: win.muted; font.pixelSize: 11 }
                        ComboBox { HelpBubble { target: parent }
                            id: materialChoice
                            objectName: "materialChoice"
                            Layout.fillWidth: true
                            implicitHeight: Math.max(38, contentItem.implicitHeight + 16)
                            leftPadding: 10; rightPadding: 10
                            contentItem: Text {
                                text: materialChoice.displayText
                                font: materialChoice.font
                                color: win.ink
                                rightPadding: materialChoice.indicator.width + 8
                                wrapMode: Text.Wrap
                                verticalAlignment: Text.AlignVCenter
                            }
                            indicator: Text {
                                x: materialChoice.width - width - 10
                                anchors.verticalCenter: parent.verticalCenter
                                text: "▾"; color: win.ink; font.pixelSize: 14
                            }
                            background: Rectangle { color: win.night ? "#202d37" : "#f7f9fa"; radius: 5; border.color: materialChoice.activeFocus ? "#209b76" : win.line }
                            delegate: ItemDelegate { HelpBubble { target: parent; description: "Wählt dieses Materialprofil. Geschwindigkeit, Leistung, Wiederholungen und Linienabstand werden daraus übernommen." }
                                required property var modelData
                                required property int index
                                width: ListView.view.width
                                implicitHeight: Math.max(40, contentItem.implicitHeight + 16)
                                highlighted: materialChoice.highlightedIndex === index
                                contentItem: Text {
                                    objectName: "materialOptionText"
                                    text: modelData; font: materialChoice.font; color: win.ink
                                    wrapMode: Text.Wrap; verticalAlignment: Text.AlignVCenter
                                }
                                background: Rectangle { color: parent.highlighted ? (win.night ? "#304e49" : "#e3f3ed") : "transparent"; radius: 4 }
                            }
                            popup: Popup {
                                objectName: "materialPopup"
                                parent: Overlay.overlay
                                width: Math.min(560, parent.width - 32)
                                height: Math.min(contentItem.implicitHeight + topPadding + bottomPadding, 360, parent.height - 32)
                                x: Math.max(16, Math.min(materialChoice.mapToItem(parent, 0, 0).x, parent.width - width - 16))
                                y: Math.max(16, Math.min(materialChoice.mapToItem(parent, 0, materialChoice.height).y + 4, parent.height - height - 16))
                                padding: 6
                                contentItem: ListView {
                                    clip: true
                                    implicitHeight: contentHeight
                                    model: materialChoice.popup.visible ? materialChoice.delegateModel : null
                                    currentIndex: materialChoice.highlightedIndex
                                    boundsBehavior: Flickable.StopAtBounds
                                    ScrollBar.vertical: ScrollBar { }
                                }
                                background: Rectangle { color: win.panel; border.color: win.line; radius: 6 }
                            }
                            model: win.materialNames
                            onModelChanged: Qt.callLater(function() { materialChoice.currentIndex = Qt.binding(function() { return win.materialRow }) })
                            enabled: win.materialRows.length > 0
                            currentIndex: win.materialRow
                            displayText: currentIndex >= 0 ? currentText : (win.projectHeadMatches ? (project.material.name || "Material wählen") : "Material für " + win.selectedModuleW + " W wählen")
                            onActivated: index => {
                                win.applyLibraryProfile(win.materialRows[index])
                                Qt.callLater(function() {
                                    materialChoice.currentIndex = Qt.binding(function() { return win.materialRow })
                                })
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 4
                            ActionButton { objectName: "editMaterialButton"; text: "Bearbeiten"; dark: win.night; compact: true; Layout.fillWidth: true; enabled: win.materialRow >= 0; onClicked: win.editMaterial(false) }
                            ActionButton { objectName: "newMaterialButton"; text: "+"; hint: "Eigenes Profil als Kopie anlegen"; dark: win.night; compact: true; onClicked: win.editMaterial(true) }
                            ActionButton { text: "⋯"; dark: win.night; compact: true; onClicked: materialMenu.popup() }
                            Menu {
                                id: materialMenu
                                MenuItem { HelpBubble { target: parent } text: "Profil duplizieren"; enabled: win.materialRow >= 0; onTriggered: { let id = materialLibrary.duplicateProfile(win.libraryIndex); if (id) win.applyLibraryProfile(materialLibrary.indexOf(id)) } }
                                MenuItem { HelpBubble { target: parent } text: "Profil aus Bibliothek löschen"; enabled: win.materialRow >= 0; onTriggered: deleteMaterialDialog.open() }
                            }
                        }
                        Text { visible: win.materialRows.length === 0; text: "Noch kein Materialprofil für diesen Kopf · mit + anlegen"; color: win.muted; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        Text { visible: !win.projectHeadMatches; text: "Projektprofil: " + win.appliedModuleW + " W · passendes Material wählen"; color: win.muted; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        Text { text: project.materialModified ? "Projektwerte weichen vom Profil ab" : (project.material.model !== "M3" ? "Herstellerreferenzen · Gerätezuordnung prüfen" : "Eigene Werte · noch nicht am Material geprüft"); color: project.materialModified ? "#bd8b40" : win.muted; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        Text { visible: !!project.material.sourceParameters && project.material.sourceParameters.passes === null; text: "Gravur-Durchgänge: keine Herstellerangabe · Projektwert " + project.layers[0].passes; color: win.muted; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        Text { visible: materialLibrary.message.length > 0; text: materialLibrary.message; color: win.muted; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    }
                    ActionButton {
                        objectName: "manufacturerButton"
                        text: "Herstellerwerte ansehen"
                        dark: win.night; compact: true; Layout.fillWidth: true
                        onClicked: win.showManufacturerReference()
                    }
                    ActionButton { objectName: "materialTestButton"; text: "Materialtest"; dark: win.night; compact: true; Layout.fillWidth: true; onClicked: materialTestDialog.open() }
                    Rectangle { Layout.fillWidth: true; height: 1; color: win.line }
                    RowLayout {
                        SectionLabel { text: "OBJEKTE"; dark: win.night; Layout.fillWidth: true }
                        Text { text: project.count + " im Projekt"; color: win.muted; font.pixelSize: 12 }
                    }
                    ListView {
                        id: objectList
                        objectName: "objectList"
                        currentIndex: project.selected
                        activeFocusOnTab: true
                        Keys.onPressed: event => {
                            if (event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
                                let row = Math.max(0, Math.min(project.count - 1, project.selected + (event.key === Qt.Key_Up ? -1 : 1)))
                                win.selectObject(row)
                                event.accepted = true
                            }
                        }
                        Layout.fillWidth: true; Layout.preferredHeight: Math.min(contentHeight, 230); Layout.minimumHeight: 65
                        clip: true; model: project; spacing: 3
                        delegate: ItemDelegate {
                            id: objectRow
                            objectName: "objectRow" + index
                            required property int index
                            required property string artName
                            required property string artKind
                            required property color artColor
                            required property bool artVisible
                            required property int artToolLayer
                            required property int artLayer
                            required property string artGroup
                            required property string artGroupName
                            required property bool artOnPage
                            required property bool artGroupFirst
                            width: objectList.width; visible: artOnPage; height: artOnPage ? (artGroupFirst ? 94 : 64) : 0
                            padding: 7
                            onClicked: win.selectObject(index)
                            background: Rectangle {
                                radius: 5
                                color: project.selectedRows.indexOf(objectRow.index) >= 0 ? (win.night ? "#2e4845" : "#eaf5f0") : objectRow.hovered ? win.bg : "transparent"
                                border.color: project.selectedRows.indexOf(objectRow.index) >= 0 ? "#209b76" : "transparent"
                            }
                            contentItem: ColumnLayout {
                                spacing: 4
                                ActionButton { objectName: "objectGroup" + objectRow.index; visible: objectRow.artGroupFirst; hint: "Wählt alle Objekte dieser Gruppe gemeinsam aus."; text: "▣ " + objectRow.artGroupName; dark: win.night; compact: true; Layout.fillWidth: true; onClicked: { objectList.forceActiveFocus(); project.selectGroup(objectRow.index); win.page = 0; canvas.ensureSelectionVisible() } }
                                RowLayout {
                                    Layout.fillWidth: true; spacing: 7
                                    CheckBox { HelpBubble { target: parent } objectName: "objectCheck" + objectRow.index; implicitWidth: 20; implicitHeight: 20; checked: project.selectedRows.indexOf(objectRow.index) >= 0; onClicked: { objectList.forceActiveFocus(Qt.MouseFocusReason); project.toggleSelection(objectRow.index) } }
                                    Rectangle { width: 7; height: 7; radius: 2; color: objectRow.artColor }
                                    Text { text: (objectRow.artToolLayer>0 ? "T"+objectRow.artToolLayer+" · " : "")+objectRow.artName; elide: Text.ElideRight; Layout.fillWidth: true; color: objectRow.artVisible ? win.ink : win.muted; font.pixelSize: 11 }
                                    Text { text: objectRow.artKind === "text" ? "T" : objectRow.artKind === "svg" ? "SVG" : objectRow.artKind === "ellipse" ? "○" : "□"; color: win.muted; font.pixelSize: 11 }
                                }
                                RowLayout {
                                    Layout.fillWidth: true; spacing: 6
                                    Text { text: "Bearbeitung"; color: win.muted; font.pixelSize: 11; Layout.fillWidth: true }
                                    ComboBox { HelpBubble { target: parent }
                                        id: objectOperation
                                        objectName: "objectOperation" + objectRow.index
                                        enabled: !win.basic || (project.layers[0].enabled && project.layers[1].enabled)
                                        model: project.layers.map(op=>op.name).concat(["T1 · Hilfsform","T2 · Hilfsform"])
                                        currentIndex: objectRow.artToolLayer>0 ? project.layers.length+objectRow.artToolLayer-1 : objectRow.artLayer
                                        implicitHeight: 28; implicitWidth: 112
                                        leftPadding: 7; rightPadding: 7
                                        onActivated: operation => { win.selectObject(objectRow.index); if(operation>=project.layers.length)project.setToolLayer(operation-project.layers.length+1);else project.setLayer(operation) }
                                        contentItem: Text { text: objectOperation.displayText; color: win.ink; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter; rightPadding: objectOperation.indicator.width + 5 }
                                        indicator: Text { x: objectOperation.width - width - 7; anchors.verticalCenter: parent.verticalCenter; text: "▾"; color: win.ink; font.pixelSize: 12 }
                                        background: Rectangle { radius: 4; color: win.bg; border.color: win.line }
                                    }
                                }
                            }
                            HelpBubble { target: objectRow; description: objectRow.artName+" auswählen. Position und Größe rechts bearbeiten. Mit dem Häkchen mehrere Objekte markieren." }
                        }
                        ScrollBar.vertical: ScrollBar { }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 5
                        ActionButton { objectName: "groupObjectsButton"; text: "Gruppieren"; compact: true; dark: win.night; Layout.fillWidth: true; enabled: project.selectedRows.length > 1 && !project.selection.grouped; onClicked: { objectList.forceActiveFocus(); project.groupSelection() } }
                        ActionButton { objectName: "ungroupObjectsButton"; text: "Auflösen"; compact: true; dark: win.night; enabled: !!project.selection.grouped; onClicked: { objectList.forceActiveFocus(); project.ungroupSelection() } }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 5
                        ActionButton { text: "+ Form"; dark: win.night; compact: true; Layout.fillWidth: true; onClicked: shapeMenu.popup() }
                        Menu {
                            id: shapeMenu
                            MenuItem { HelpBubble { target: parent } text: "Rechteck"; onTriggered: win.addShape("rect") }
                            MenuItem { HelpBubble { target: parent } text: "Ellipse"; onTriggered: win.addShape("ellipse") }
                            MenuItem { HelpBubble { target: parent } text: "Text"; onTriggered: win.addShape("text") }
                        }
                        ActionButton { objectName: "copyObjectsButton"; text: "Kopie"; dark: win.night; compact: true; enabled: project.selected >= 0; onClicked: project.duplicateSelected() }
                        ActionButton { objectName: "deleteObjectsButton"; text: win.basic ? "Löschen" : "−"; hint: "Auswahl löschen"; dark: win.night; compact: true; enabled: project.selected >= 0; onClicked: project.removeSelected() }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 5
                        ActionButton { objectName: "clipboardCopyButton"; text: "Kopieren"; hint: "Auswahl in die Systemzwischenablage kopieren · Strg+C"; dark: win.night; compact: true; Layout.fillWidth: true; enabled: project.selected>=0; onClicked: project.copySelectionToClipboard() }
                        ActionButton { objectName: "clipboardPasteButton"; text: "Einfügen"; hint: "Blazer-Objekte, SVG, Bild oder Text einfügen · Strg+V; Strg+Umschalt+V erhält die ursprüngliche Lage von Blazer-Objekten."; dark: win.night; compact: true; Layout.fillWidth: true; onClicked: project.pasteFromClipboard() }
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
                                Rectangle { width: 3; height: 30; radius: 1; color: project.layerColor(index) }
                                Column {
                                    Layout.fillWidth: true; spacing: 4
                                    Text { text: modelData.name; color: win.ink; font.pixelSize: 11; font.weight: Font.Medium }
                                    Text { text: modelData.power + " %  ·  " + modelData.speed + " mm/min"; color: win.muted; font.pixelSize: 11 }
                                }
                                Rectangle { width: 6; height: 6; radius: 3; color: modelData.enabled ? "#209b76" : win.muted }
                            }
                            MouseArea { id: operationArea; anchors.fill: parent; hoverEnabled: true; onClicked: win.operation = index; ToolTip.visible: containsMouse; ToolTip.text: "Zeigt die Werte der Bearbeitung "+modelData.name+" rechts an. Eine Auswahl hier ändert noch keine Objektzuordnung."; ToolTip.delay: 650 }
                        }
                    }
                    Item { Layout.fillHeight: true }
                    Text { text: "Blazer Studio · "+(typeof appServices!=="undefined" ? appServices.version : "0.9-Testkandidat")+"\nLokal auf deinem Rechner"; color: win.muted; font.pixelSize: 11; lineHeight: 1.5 }
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
                        ActionButton { objectName: "addRectangle"; text: win.basic ? "Rechteck" : "□"; hint: "Fügt ein Rechteck ein. Größe und Position rechts in Millimetern einstellen."; compact: true; dark: win.night; enabled: win.page === 0; onClicked: win.addShape("rect") }
                        ActionButton { text: win.basic ? "Kreis" : "○"; hint: "Fügt eine Ellipse ein. Gleiche Breite und Höhe ergeben einen Kreis."; compact: true; dark: win.night; enabled: win.page === 0; onClicked: win.addShape("ellipse") }
                        ActionButton { visible: !win.basic; text: "⋈"; hint: "Knoten ein/aus"; compact: true; dark: win.night; enabled: win.page===0 && !win.basic; selected: canvas.nodes; onClicked: canvas.nodes=!canvas.nodes }
                        ActionButton { text: "Text"; hint: "Fügt einen Text ein. Den Inhalt rechts ändern."; compact: true; dark: win.night; enabled: win.page === 0; onClicked: win.addShape("text") }
                        Rectangle { width: 1; height: 18; color: win.line; Layout.leftMargin: 3; Layout.rightMargin: 3 }
                        ActionButton { text: "↶"; hint: "Rückgängig · Strg+Z"; compact: true; dark: win.night; enabled: project.canUndo; onClicked: project.undo() }
                        ActionButton { text: "↷"; hint: "Wiederholen · Strg+Umschalt+Z"; compact: true; dark: win.night; enabled: project.canRedo; onClicked: project.redo() }
                        ActionButton { objectName: "openHistoryButton"; text: "Verlauf"; hint: "Zeigt benannte Bearbeitungsschritte und wechselt zu einem früheren oder späteren Stand."; compact: true; dark: win.night; onClicked: historyDialog.open() }
                        Item { Layout.fillWidth: true }
                        Text { text: win.page === 0 ? "ARBEITSFLÄCHE" : win.page === 1 ? "PFADVORSCHAU" : win.page === 3 ? "RELIEF / TIEFENKARTE" : win.page===4 ? "EDITOR / PRODUKTION" : "GERÄTESTEUERUNG"; color: win.muted; font.pixelSize: 11; font.letterSpacing: 1 }
                    }
                }
                Rectangle {
                    visible: win.page === 1
                    Layout.fillWidth: true; Layout.preferredHeight: 64
                    color: win.panel; border.color: win.line; clip: true
                            Text { anchors.fill: parent; anchors.margins: 9; maximumLineCount: 3; elide: Text.ElideRight; text: project.jobSummary.valid ? project.jobSummary.segments + " Segmente · " + (project.jobSummary.workLength / 1000).toFixed(2) + " m · ca. " + Math.ceil(project.jobSummary.seconds / 60) + " min\nFarbig: Bearbeitung · Grau: Leerfahrt\nSchätzung mit konservativer Beschleunigung; Kommunikation nicht gemessen" : project.jobSummary.errors.join("\n"); color: win.muted; font.pixelSize: 12; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; wrapMode: Text.WordWrap }
                }
                Rectangle {
                    visible: win.basic; color: win.panel; border.color: win.line
                    Layout.fillWidth: true; Layout.preferredHeight: basicGuide.implicitHeight+20
                    ColumnLayout { id: basicGuide; anchors.left: parent.left; anchors.right: parent.right; anchors.margins: 12; y: 10; spacing: 6
                        Text { text: win.page===0 ? "1. Motiv laden → 2. Laseraufsatz und Material links wählen → 3. Größe und Platzierung rechts prüfen" : win.page===1 ? "4. Vorschau prüfen und zuerst eine kleine Materialprobe vorbereiten" : "5. Laser verbinden, Position prüfen und Auftrag bewusst starten"; color: win.ink; wrapMode: Text.WordWrap; Layout.fillWidth: true; font.pixelSize: 12 }
                        Text { text: "Werte aus: "+project.beginnerSummary.material+" · "+project.beginnerSummary.source+". Übernommene Werte sind noch keine am eigenen Gerät bestätigte Empfehlung."; color: win.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true; font.pixelSize: 12 }
                        Text { visible: project.beginnerSummary.reason.length>0 || !win.projectHeadMatches; text: !win.projectHeadMatches ? "Toolkopf und Projektprofil passen noch nicht zusammen. Links ein Material für den tatsächlichen Kopf auswählen." : project.beginnerSummary.reason; color: "#c07b2a"; wrapMode: Text.WordWrap; Layout.fillWidth: true; font.pixelSize: 11 }
                        RowLayout { Layout.fillWidth: true
                            ActionButton { objectName: "basicProfileApply"; text: "Profilwerte übernehmen"; dark: win.night; hint: "Übernimmt die Materialwerte für dieses Blatt und entfernt eigene Objekt-Abweichungen. Mit Rückgängig umkehrbar."; onClicked: project.applyBasicMaterial(project.material) }
                            ActionButton { objectName: "basicProbe"; text: "5-mm-Probe vorbereiten"; dark: win.night; hint: "Erzeugt ein neues Projekt mit einer kleinen Probe und genau den gewählten Materialwerten. Ungespeicherte Änderungen werden vorher abgefragt."; onClicked: win.requestAction("basicprobe") }
                        }
                        CheckBox { objectName: "basicProof"; text: "Diese Werte habe ich am tatsächlichen Material geprüft"; checked: win.proofConfirmed; onClicked: win.proofConfirmed=checked; HelpBubble {target: parent; description: "Eine persönliche Bestätigung nach einer echten Materialprobe. Das Programm misst das Ergebnis nicht selbst. Bei geänderten Materialwerten wird die Bestätigung zurückgesetzt."} }
                    }
                }
                Rectangle {
                    visible: project.planningBusy || project.planningState==="cancelled" || project.planningState==="cancelling" || project.planningState==="failed"
                    Layout.fillWidth: true; Layout.preferredHeight: Math.max(58,planningRow.implicitHeight+20); color: win.panel
                    RowLayout { id: planningRow; anchors.fill: parent; anchors.margins: 10
                        BusyIndicator { running: project.planningBusy; visible: running; implicitWidth: 26; implicitHeight: 26 }
                        Text { text: project.batchBusy ? "CSV-Stapel wird aus dem angeforderten Projektstand exportiert …" : project.planningBusy ? "Auftrag wird berechnet … Änderungen werden automatisch berücksichtigt." : project.planningState==="failed" ? project.jobSummary.errors.join("\n") : "Berechnung abgebrochen. Das Projekt bleibt erhalten."; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: win.ink; font.pixelSize: 12 }
                        ActionButton { objectName: "cancelPlanning"; visible: project.planningBusy; text: "Berechnung abbrechen"; hint: "Bricht die Vorbereitung des Auftrags ab. Dies ist kein Laser-Stop. Dein Projekt bleibt erhalten."; onClicked: project.cancelPlanning() }
                        ActionButton { objectName: "retryPlanning"; visible: !project.planningBusy; text: "Neu berechnen"; hint: "Berechnet den aktuellen Projektstand erneut. Ein Laserauftrag startet dadurch nicht."; onClicked: project.retryPlanning() }
                    }
                }
                StackLayout {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    currentIndex: win.page === 4 ? 3 : win.page === 3 ? 2 : win.page === 2 ? 1 : 0
                    Item {
                        Workspace { id: canvas; objectName: "workspace"; anchors.fill: parent; document: project; registration: typeof cameraRegistration!=="undefined" ? cameraRegistration : null; simulator: machine; preview: win.page === 1; dark: win.night; imperial: win.lengthUnit==="in" }
                        Rectangle {
                            anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.margins: 20
                            width: win.lengthUnit==="in" ? 190 : 168; height: 30; radius: 15; color: win.panel; border.color: win.line
                            Text { objectName: "editorBedSize"; anchors.centerIn: parent; width: parent.width-14; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight; textFormat: Text.PlainText; text: win.page === 0 ? project.pages[project.activePage]+" · "+(win.lengthUnit==="in" ? (project.bedWidth/25.4).toFixed(3)+" × "+(project.bedHeight/25.4).toFixed(3)+" in" : project.bedWidth+" × "+project.bedHeight+" mm") : "Berechnete Bearbeitungswege"; color: win.muted; font.pixelSize: 12 }
                        }
                        ToolCombo {
                            objectName: "editorUnitChoice"; dark: win.night; width: 76; height: 30
                            anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 20
                            model: ["mm","in"]
                            currentIndex: win.lengthUnit==="in" ? 1 : 0
                            enabled: typeof appServices!=="undefined" && appServices!==null
                            onActivated: index=>{if(!appServices.setLengthUnit(index===1?"in":"mm"))project.notify(appServices.message);else project.notify(index===1?"Maßeinheit für Längen: Zoll (in).":"Maßeinheit für Längen: Millimeter (mm).");currentIndex=Qt.binding(function(){return win.lengthUnit==="in"?1:0})}
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
                                    ActionButton { text: machine.connected ? "Trennen" : "Verbinden"; dark: win.night; onClicked: machine.toggleConnection() }
                                }
                                Text { text: typeof machine.simulation === "undefined" || machine.simulation ? "Simulierter Blaze X M3 · Keine Hardwarebefehle." : "GRBL / Blaze-X ESP32 · echte Hardwareverbindung · S-Max 1000 / Lasermodus werden geprüft."; color: win.muted; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                                RowLayout {
                                    visible: typeof machine.simulation !== "undefined"
                                    Layout.fillWidth: true
                                    ComboBox { HelpBubble { target: parent }
                                        id: serialPortChoice; objectName: "serialPortChoice"; Layout.fillWidth: true
                                        model: typeof machine.ports !== "undefined" ? machine.ports : []; displayText: count ? currentText : "Kein serielles Gerät gefunden"
                                        contentItem: Text { text: serialPortChoice.displayText; color: win.ink; font: serialPortChoice.font; verticalAlignment: Text.AlignVCenter; rightPadding: 20 }
                                        indicator: Text { x: serialPortChoice.width-width-8; anchors.verticalCenter: parent.verticalCenter; text: "▾"; color: win.ink }
                                    }
                                    ActionButton { text: "Suchen"; dark: win.night; onClicked: machine.refreshPorts() }
                                    ActionButton { text: "GRBL verbinden"; dark: win.night; enabled: !machine.busy && serialPortChoice.count > 0; onClicked: machine.connectHardware(serialPortChoice.currentText) }
                                    ActionButton { text: "Simulator"; dark: win.night; onClicked: machine.useSimulator() }
                                }
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
                                            ActionButton { visible: typeof machine.simulation !== "undefined" && !machine.simulation; text: "Arbeitsnullpunkt hier setzen"; dark: win.night; enabled: machine.connected && !machine.busy; onClicked: machine.setWorkZero() }
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
                                            Text { text: machine.firing ? project.deviceInfo.focusPower+" % angefordert" : "Fokuspunkt mit "+project.deviceInfo.focusPower+" %"; color: machine.firing ? "#d58d39" : win.ink; font.pixelSize: 17; font.weight: Font.DemiBold }
                                            Text { text: typeof machine.simulation === "undefined" || machine.simulation ? "Gedrückt halten · max. 3 Sekunden\nFokus manuell einstellen; Z-Achse laut Profil aus." : "Ein 0,2-s-Puls bei "+project.deviceInfo.focusPower+" % · Loslassen bricht ab.\nBei Verbindungsverlust ist Laser-Aus nicht garantiert."; color: win.muted; font.pixelSize: 12 }
                                        }
                                        ActionButton { objectName: "focusHold"; text: "◎  Halten für "+project.deviceInfo.focusPower+" %"; primary: true; implicitHeight: 44; enabled: machine.ready || machine.firing; onPressedChanged: machine.focus(pressed) }
                                    }
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true; spacing: 6
                                    SectionLabel { text: "RAHMENFAHRT · LASER AUS"; dark: win.night }
                                    ToolCombo {
                                        id: frameTargetChoice; objectName: "frameTargetChoice"; dark: win.night; Layout.fillWidth: true
                                        textRole: "label"; valueRole: "scope"
                                        model: [{label:"Laserauftrag",scope:"job"},{label:"Sichtbare Auswahl",scope:"selection"},{label:"Hilfsformen T1 + T2",scope:"tools"},{label:"Nur Hilfsformen T1",scope:"t1"},{label:"Nur Hilfsformen T2",scope:"t2"}]
                                        currentIndex: model.findIndex(row=>row.scope===win.frameScope)
                                        enabled: !machine.busy && !machine.firing
                                        onActivated: index=>win.frameScope=currentValue
                                    }
                                    Text {
                                        objectName: "frameRegionSummary"; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: win.muted; font.pixelSize: 11
                                        text: {let r=win.frameRegion;if(!r.valid)return r.reason;let f=win.lengthUnit==="in" ? 25.4 : 1;let digits=win.lengthUnit==="in" ? 4 : 2;return "X "+(r.x/f).toFixed(digits)+" · Y "+(r.y/f).toFixed(digits)+" · "+(r.width/f).toFixed(digits)+" × "+(r.height/f).toFixed(digits)+" "+win.lengthUnit}
                                    }
                                    Text { text: win.frameScope==="job" ? "Geplante Bearbeitungswege einschließlich Überlauf. Hilfsformen bleiben außerhalb des Laserauftrags." : "Sichtbare Objektgrenzen des aktiven Blatts. Die Rahmenwahl aktiviert keine Laserausgabe."; color: win.muted; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                                }
                                RowLayout {
                                    Layout.fillWidth: true; spacing: 8
                                    ActionButton { objectName: "frameJobButton"; text: "□  Rahmenfahrt"; Layout.fillWidth: true; dark: win.night; enabled: machine.ready && !project.planningBusy && win.frameRegion.valid; onClicked: machine.frame(project.framingBounds(win.frameScope)) }
                                    ActionButton { objectName: "pauseJobButton"; text: machine.paused ? "▶  Fortsetzen" : "Ⅱ  Pause"; Layout.fillWidth: true; dark: win.night; enabled: machine.running || machine.paused; onClicked: machine.pause() }
                                    ActionButton { objectName: "stopJobButton"; text: "■  Stop"; Layout.fillWidth: true; dark: win.night; destructive: true; onClicked: machine.stop() }
                                }
                                ProgressBar { Layout.fillWidth: true; value: machine.progress; visible: machine.busy }
                                RowLayout {
                                    visible: !win.basic && typeof machine.setOverrides!=="undefined"
                                    NumberField { id: feedOverride; label: "v"; value: 100; unit: "%"; Layout.fillWidth: true; dark: win.night; onEdited: n=>value=n }
                                    NumberField { id: powerOverride; label: "P"; value: 100; unit: "%"; Layout.fillWidth: true; dark: win.night; onEdited: n=>value=n }
                                    ActionButton { text: "Live übernehmen"; dark: win.night; enabled: !win.simulationMode && machine.connected; onClicked: {if(!machine.setOverrides(feedOverride.value,powerOverride.value))project.notify("Override abgelehnt: Statusrückmeldung Ov erforderlich; v 10–200 %, P 10–100 %.")} }
                                }
                                Repeater { model: !win.basic && typeof machine.capabilities!=="undefined" ? machine.capabilities : []
                                    Text { required property var modelData; text: modelData.name+": "+modelData.status+" · "+modelData.detail; color: win.muted; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                                }
                                SectionLabel { text: "EREIGNISSE"; dark: win.night; Layout.topMargin: 6 }
                                Repeater { model: machine.log.slice(0,6); delegate: Text { required property string modelData; text: modelData; color: win.muted; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true } }
                                ActionButton { text: "Verbindungsfehler testen"; visible: !win.basic && (typeof machine.simulation === "undefined" || machine.simulation); dark: win.night; compact: true; enabled: machine.connected; onClicked: machine.disconnectFault() }
                            }
                            Item { height: 20 }
                        }
                    }
                    ReliefPanel { dark: win.night; onImportRequested: reliefDialog.open(); onPhotoRequested: photoDepth.open() }
                    ToolsPanel { selectedGuideId: canvas.selectedGuideId; onGuideSelected: id=>canvas.selectedGuideId=id; dark: win.night; operation: win.operation; onOperationSelected: index=>win.operation=index; onNodesRequested: {win.page=0;canvas.nodes=true} onImportRequested: universalImport.open(); onExportRequested: vectorExport.open(); onImageExportRequested: processed=>{imageExport.processed=processed;imageExport.imageId=project.selection.id;imageExport.revision=project.historyRevision;imageExport.open()} onCsvRequested: csvImport.open(); onCameraRequested: cameraPhoto.open() }
                }
                Rectangle {
                    Layout.fillWidth: true; Layout.preferredHeight: 39; color: win.panel
                    Rectangle { height: 1; width: parent.width; color: win.line }
                    RowLayout {
                        anchors.fill: parent; anchors.leftMargin: 18; anchors.rightMargin: 18
                        Text { objectName: "activePageFooter"; text: "●  " + (win.page === 2 ? machine.state : project.pages[project.activePage]); textFormat: Text.PlainText; color: "#209b76"; font.pixelSize: 12; Layout.fillWidth: true; Layout.minimumWidth: 0; elide: Text.ElideRight }
                        Text { objectName: "workspaceHint"; text: win.page === 2 ? "X " + machine.x.toFixed(1) + "  /  Y " + machine.y.toFixed(1) + " mm" : "Ziehen: "+(win.lengthUnit==="in" ? "0.025 in" : "0,5 mm")+"   ·   Mausrad: Zoom   ·   Mitteltaste: Verschieben"; color: win.muted; font.pixelSize: 11 }
                    }
                }
            }
            // Right sidebar: inspector and operation settings.
            Rectangle {
                Layout.preferredWidth: 278; Layout.fillHeight: true; color: win.panel
                Rectangle { width: 1; height: parent.height; color: win.line }
                ScrollView {
                    id: inspectorScroll
                    objectName: "inspectorScroll"
                    anchors.fill: parent; clip: true; contentWidth: availableWidth
                    ColumnLayout {
                        width: inspectorScroll.availableWidth; spacing: 15
                        ColumnLayout {
                            Layout.fillWidth: true; Layout.margins: 18; spacing: 15
                            RowLayout {
                                SectionLabel { text: "EINSTELLUNGEN"; dark: win.night; Layout.fillWidth: true }
                                Rectangle { width: 53; height: 21; radius: 4; color: win.bg; Text { anchors.centerIn: parent; text: win.basic ? "Basic" : "Advanced"; color: win.muted; font.pixelSize: 11 } }
                            }
                            Text { objectName: "basicValues"; visible: win.basic && !(project.selection.toolLayer>0); text: "Übernommene Bearbeitung\n"+win.currentLayer.name+"\nGeschwindigkeit: "+win.currentLayer.speed+" mm/min\nLeistung: "+win.currentLayer.power+" %\nDurchgänge: "+win.currentLayer.passes+(win.currentLayer.mode==="Schnitt" ? "" : "\nLinienabstand: "+Number(win.currentLayer.interval).toFixed(3)+" mm"); color: win.ink; wrapMode: Text.WordWrap; Layout.fillWidth: true; font.pixelSize: 12 }
                            Text { visible: !win.basic; text: project.selection.toolLayer>0 ? "Hilfsform T"+project.selection.toolLayer : win.currentLayer.name; color: win.ink; font.pixelSize: 17; font.weight: Font.DemiBold }
                            Rectangle {
                                visible: !win.basic && !(project.selection.toolLayer>0); Layout.fillWidth: true; height: 31; radius: 5; color: win.bg
                                RowLayout {
                                    anchors.fill: parent; anchors.leftMargin: 9; anchors.rightMargin: 9
                                    Text { text: "Operation ausgeben"; color: win.ink; font.pixelSize: 11; Layout.fillWidth: true }
                                    Switch { HelpBubble { target: parent } checked: win.currentLayer.enabled; implicitHeight: 24; onToggled: project.setLayerSetting(win.operation,"enabled",checked) }
                                }
                            }
                            NumberField { visible: !win.basic && !(project.selection.toolLayer>0) && project.selection.kind !== "relief"; label: "v"; value: win.currentLayer.speed; unit: "mm/min"; decimals: 0; dark: win.night; Layout.fillWidth: true; enabled: !win.basic; onEdited: n => project.setLayerSetting(win.operation,"speed",n) }
                            NumberField { visible: !win.basic && !(project.selection.toolLayer>0) && project.selection.kind !== "relief"; label: "P"; value: win.currentLayer.power; unit: "%"; decimals: 1; dark: win.night; Layout.fillWidth: true; enabled: !win.basic; onEdited: n => project.setLayerSetting(win.operation,"power",n) }
                            NumberField { visible: !win.basic && !(project.selection.toolLayer>0) && project.selection.kind !== "relief"; label: "×"; value: win.currentLayer.passes; unit: "Mal"; decimals: 0; dark: win.night; Layout.fillWidth: true; enabled: !win.basic; onEdited: n => project.setLayerSetting(win.operation,"passes",n) }
                            NumberField { visible: !win.basic && !(project.selection.toolLayer>0) && win.operation === 0 && project.selection.kind !== "relief"; label: "↕"; value: win.currentLayer.interval; unit: "mm"; decimals: 3; dark: win.night; Layout.fillWidth: true; enabled: !win.basic; onEdited: n => project.setLayerSetting(win.operation,"interval",n) }
                            Text { text: project.selection.toolLayer>0 ? "Diese Form dient der Platzierung und wird niemals gelasert." : project.selection.kind === "relief" ? "Relief: eigene Objektwerte unten; Schichten und Raster im Bereich Relief." : "Profilwerte · Materialtest erforderlich"; color: win.muted; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                            Rectangle { Layout.fillWidth: true; height: 1; color: win.line }
                            SectionLabel { text: "AUSGEWÄHLTES OBJEKT"; dark: win.night }
                            Text { objectName: "toolShapeStatus"; visible: project.selection.toolLayer>0; text: "T"+project.selection.toolLayer+" · Hilfsform · keine Laserausgabe"; color: win.ink; wrapMode: Text.WordWrap; Layout.fillWidth: true; font.pixelSize: 12 }
                            TextField { HelpBubble { target: parent }
                                objectName: "objectNameInput"
                                Layout.fillWidth: true; enabled: project.selected >= 0 && project.selection.kind !== "selection"
                                text: project.selection.name || "Nichts ausgewählt"
                                color: win.ink; font.pixelSize: 12; implicitHeight: 34; selectByMouse: true
                                background: Rectangle { radius: 5; color: win.bg; border.color: win.line }
                                onEditingFinished: project.setProperty("name",text)
                            }
                            ColumnLayout {
                                visible: !win.basic && !!project.selection.customSettings
                                Layout.fillWidth: true
                                Text { text: "Eigene Objektwerte"; color: win.muted; font.pixelSize: 12 }
                                NumberField { label: "v"; value: project.selection.speed || 0; unit: "mm/min"; dark: win.night; Layout.fillWidth: true; onEdited: n => project.setObjectSetting("speed",n) }
                                NumberField { label: "P"; value: project.selection.power || 0; unit: "%"; dark: win.night; Layout.fillWidth: true; onEdited: n => project.setObjectSetting("power",n) }
                            }
                            GridLayout {
                                Layout.fillWidth: true; columns: win.basic ? 1 : 2; columnSpacing: 8; rowSpacing: 9
                                enabled: project.selected >= 0
                                NumberField { objectName: "objectXInput"; label: win.basic ? "Links" : "X"; value: project.selection.x || 0; dark: win.night; Layout.fillWidth: true; onEdited: n => project.setProperty("x",n) }
                                NumberField { objectName: "objectYInput"; label: win.basic ? "Unten" : "Y"; value: project.selection.y || 0; dark: win.night; Layout.fillWidth: true; onEdited: n => project.setProperty("y",n) }
                                NumberField { objectName: "objectWidthInput"; label: win.basic ? "Breite" : "B"; value: project.selection.width || 0; dark: win.night; Layout.fillWidth: true; onEdited: n => project.setProperty("width",n) }
                                NumberField { objectName: "objectHeightInput"; label: win.basic ? "Höhe" : "H"; value: project.selection.height || 0; dark: win.night; Layout.fillWidth: true; onEdited: n => project.setProperty("height",n) }
                            }
                            Text { visible: project.selection.kind==="image" || project.selection.kind==="relief"; text: project.selection.imageWidth+" × "+project.selection.imageHeight+" px · Quelle ca. "+Math.round(project.selection.imageWidth/Math.max(.1,project.selection.width)*25.4)+" dpi\nAusgabe-Raster in den Operationswerten prüfen."; color: win.muted; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                            Text { visible: project.issues.length > 0; text: project.issues.join("\n"); color: "#cf645b"; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                            Text { text: "Skalieren · 100 % = aktuelle Größe"; color: win.muted; font.pixelSize: 11; Layout.fillWidth: true }
                            NumberField { objectName: "objectScaleInput"; label: "S"; value: 100; unit: "%"; dark: win.night; Layout.fillWidth: true; enabled: project.selected >= 0; onEdited: n => project.scaleSelection(n) }
                            Text { text: "Koordinaten: links unten · Y nach oben"; color: win.muted; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                            TextField { HelpBubble { target: parent }
                                objectName: "objectTextInput"
                                visible: project.selection.kind === "text"; Layout.fillWidth: true
                                text: project.selection.text || ""; color: win.ink; font.pixelSize: 12; selectByMouse: true
                                background: Rectangle { radius: 5; color: win.bg; border.color: win.line }
                                onEditingFinished: project.setProperty("text",text)
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                ActionButton { objectName: "engraveObjectButton"; text: "Gravieren"; dark: win.night; compact: true; Layout.fillWidth: true; selected: project.selection.toolLayer===0 && project.selection.layer === 0; enabled: project.selected >= 0 && (!win.basic || project.layers[0].enabled); onClicked: project.setLayer(0) }
                                ActionButton { objectName: "cutObjectButton"; text: "Schneiden"; dark: win.night; compact: true; Layout.fillWidth: true; selected: project.selection.toolLayer===0 && project.selection.layer === 1; enabled: project.selected >= 0 && (!win.basic || project.layers[1].enabled); onClicked: project.setLayer(1) }
                            }
                            ActionButton { text: project.selection.visible ? "Objekt ausblenden" : "Objekt einblenden"; dark: win.night; compact: true; Layout.fillWidth: true; enabled: project.selected >= 0; onClicked: project.toggleVisible(project.selected) }
                            Rectangle { visible: !win.basic; Layout.fillWidth: true; height: 1; color: win.line }
                            SectionLabel { visible: !win.basic; text: "GERÄTEPROFIL"; dark: win.night }
                            Repeater {
                                model: win.basic ? [] : [{k:"Verbindung",v:project.deviceInfo.controller+" · seriell"},{k:"Baudrate",v:String(project.deviceInfo.baudRate)},{k:"S-Max",v:String(project.deviceInfo.powerScale)},{k:"Fokuspunkt",v:project.deviceInfo.focusPower+" % / S"+Math.round(project.deviceInfo.focusPower*project.deviceInfo.powerScale/100)},{k:"Z-Achse",v:project.deviceInfo.zEnabled ? "Aktiviert" : "Deaktiviert"}]
                                delegate: RowLayout {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    Text { text: modelData.k; color: win.muted; font.pixelSize: 12; Layout.fillWidth: true }
                                    Text { text: modelData.v; color: win.ink; font.pixelSize: 12 }
                                }
                            }
                            Text { visible: !win.basic; text: "Konfiguration aus LightBurn übernommen.\nNoch nicht live am Gerät verifiziert."; color: win.muted; font.pixelSize: 11; lineHeight: 1.4; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        }
                    }
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 29; color: "#17232e"
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 18; anchors.rightMargin: 18
                Text { text: project.message; textFormat: Text.PlainText; color: "#a6bac8"; font.pixelSize: 11; elide: Text.ElideRight; Layout.fillWidth: true }
                Text { text: win.simulationMode ? "SIMULATION · KEINE LASEREMISSION" : "HARDWARE · " + machine.state; color: "#81c9b3"; font.pixelSize: 8; font.letterSpacing: .8 }
            }
        }
    }
}
