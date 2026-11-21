import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    objectName: "artLibraryDialog"
    property var library: null
    property var controller: null
    property var document: null
    property bool dark: false
    property string selectedAssetId: ""
    property string pendingDeleteId: ""
    property string lastReviewToken: ""
    readonly property color ink: dark ? "#e2eaf0" : "#263b4a"
    readonly property bool available: !!controller && !!library
    readonly property bool editable: available && controller.blockReason.length === 0
    readonly property var captureReview: controller ? controller.review : ({})
    readonly property var results: {
        if (!library) return []
        let revision = library.entries
        return library.search(searchInput.text)
    }
    readonly property var selectedAsset: results.find(row => row.id === selectedAssetId) || ({})
    readonly property string insertionError: {
        if (!selectedAssetId) return "Zuerst eine Grafik aus der Bibliothek auswählen."
        const x = insertX.numericValue(), y = insertY.numericValue()
        if (!Number.isFinite(x) || !Number.isFinite(y)) return "Gültige Längen für Links und Unten eingeben; ungültige Eingaben werden nicht durch alte Werte ersetzt."
        if (!document || x < 0 || y < 0 || x + Number(selectedAsset.widthMm) > document.bedWidth || y + Number(selectedAsset.heightMm) > document.bedHeight) return "Die Vorlage passt an dieser Position nicht vollständig auf die Arbeitsfläche. Position ändern oder ein passendes Geräteprofil verwenden."
        return ""
    }
    signal inserted()
    function tags(value) { return value.split(",").map(tag => tag.trim()).filter(tag => tag.length > 0) }
    function length(value) {
        const inches = typeof appServices !== "undefined" && appServices.lengthUnit === "in"
        return (Number(value)/(inches ? 25.4 : 1)).toFixed(inches ? 4 : 2) + (inches ? " in" : " mm")
    }
    function choose(id) {
        selectedAssetId = id; pendingDeleteId = ""
        const asset = results.find(row => row.id === id)
        editName.text = asset ? asset.name : ""
        editTags.text = asset ? asset.tags.join(", ") : ""
    }
    onResultsChanged: if (selectedAssetId && !results.some(row => row.id === selectedAssetId)) choose("")
    onCaptureReviewChanged: {
        const token = captureReview.token || ""
        if (token !== lastReviewToken) { conversionConsent.checked = false; lastReviewToken = token }
    }
    onClosed: { if (controller) controller.clearReview(); pendingDeleteId = "" }
    title: "Grafikbibliothek"
    anchors.centerIn: parent; modal: true; focus: true
    width: Math.min(parent.width - 48, 980)
    height: Math.min(parent.height - 48, 710)
    background: Rectangle { color: dialog.dark ? "#202e39" : "white"; radius: 9; border.color: "#708290" }
    contentItem: ColumnLayout {
        spacing: 10
        Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: dialog.ink; text: "Grafiken wiederverwenden · keine Bearbeitungsparameter. Eingefügte Vorlagen bleiben ohne Ausgabe, bis du eine lokale Bearbeitung zuweist." }
        TabBar {
            id: tabs; objectName: "artLibraryTabs"; Layout.fillWidth: true
            TabButton { objectName: "artLibraryBrowseTab"; text: "Bibliothek" }
            TabButton { objectName: "artLibraryCaptureTab"; text: "Auswahl speichern" }
        }
        StackLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; currentIndex: tabs.currentIndex
            RowLayout {
                spacing: 16
                ColumnLayout {
                    Layout.fillWidth: true; Layout.fillHeight: true; Layout.preferredWidth: 380
                    DialogField { id: searchInput; objectName: "artLibrarySearch"; dark: dialog.dark; Layout.fillWidth: true; placeholderText: "Namen oder Tags suchen" }
                    ListView {
                        id: assetList; objectName: "artLibraryList"; Layout.fillWidth: true; Layout.fillHeight: true
                        clip: true; model: dialog.results; spacing: 5
                        delegate: ItemDelegate {
                            id: assetRow
                            required property var modelData
                            required property int index
                            objectName: "artLibraryRow" + index
                            width: assetList.width; height: 92
                            highlighted: dialog.selectedAssetId === modelData.id
                            onClicked: dialog.choose(modelData.id)
                            background: Rectangle { radius: 5; color: assetRow.highlighted ? (dialog.dark ? "#284e47" : "#dceee9") : (dialog.dark ? "#16232c" : "#f1f5f7") }
                            contentItem: ColumnLayout {
                                Label { Layout.fillWidth: true; text: assetRow.modelData.name; textFormat: Text.PlainText; color: dialog.ink; elide: Text.ElideRight }
                                Label { Layout.fillWidth: true; text: assetRow.modelData.tags.join(" · "); textFormat: Text.PlainText; color: dialog.ink; elide: Text.ElideRight; font.pixelSize: 11 }
                                Label { Layout.fillWidth: true; text: dialog.length(assetRow.modelData.widthMm) + " × " + dialog.length(assetRow.modelData.heightMm) + " · " + assetRow.modelData.objectCount + " Objekte"; color: dialog.ink; font.pixelSize: 11 }
                            }
                        }
                        ScrollBar.vertical: ScrollBar {}
                        Label { anchors.centerIn: parent; visible: dialog.results.length === 0; width: parent.width - 12; text: searchInput.text.length ? "Keine passende Grafik gefunden." : "Noch keine Grafiken gespeichert. Mit »Auswahl speichern« beginnen."; color: dialog.ink; wrapMode: Text.WordWrap }
                    }
                }
                ScrollView {
                    id: detailScroll; Layout.fillWidth: true; Layout.fillHeight: true; Layout.preferredWidth: 480
                    clip: true; contentWidth: availableWidth
                    ColumnLayout {
                        width: detailScroll.availableWidth; spacing: 10
                        Label { Layout.fillWidth: true; text: dialog.selectedAsset.name || "Grafik auswählen"; textFormat: Text.PlainText; color: dialog.ink; wrapMode: Text.WordWrap; font.bold: true }
                        Image { objectName: "artLibraryThumbnail"; Layout.alignment: Qt.AlignHCenter; Layout.preferredWidth: 160; Layout.preferredHeight: 160; fillMode: Image.PreserveAspectFit; source: dialog.editable && dialog.selectedAssetId ? dialog.controller.thumbnailSource(dialog.selectedAssetId) : "" }
                        Label { Layout.fillWidth: true; text: dialog.selectedAsset.conversionNotice || "Die Vorschau zeigt die gespeicherte Kontur-/PNG-Kopie, keine Werkzeugwege."; textFormat: Text.PlainText; wrapMode: Text.WordWrap; color: dialog.ink; font.pixelSize: 12 }
                        Label { text: "Name (höchstens 100 Zeichen)"; color: dialog.ink }
                        DialogField { id: editName; objectName: "artLibraryEditName"; dark: dialog.dark; Layout.fillWidth: true }
                        Label { text: "Tags, durch Komma getrennt"; color: dialog.ink }
                        DialogField { id: editTags; objectName: "artLibraryEditTags"; dark: dialog.dark; Layout.fillWidth: true }
                        RowLayout {
                            ActionButton { objectName: "artLibrarySaveMetadata"; text: "Name / Tags speichern"; dark: dialog.dark; enabled: dialog.editable && !!dialog.selectedAssetId; onClicked: if (dialog.controller.updateMetadata(dialog.selectedAssetId,editName.text,dialog.tags(editTags.text))) Qt.callLater(() => dialog.choose(dialog.selectedAssetId)) }
                            ActionButton { objectName: "artLibraryDelete"; text: dialog.pendingDeleteId === dialog.selectedAssetId && dialog.selectedAssetId ? "Löschen bestätigen" : "Löschen"; destructive: true; dark: dialog.dark; enabled: dialog.editable && !!dialog.selectedAssetId; onClicked: { if (dialog.pendingDeleteId !== dialog.selectedAssetId) { dialog.pendingDeleteId = dialog.selectedAssetId; return } if (dialog.controller.remove(dialog.selectedAssetId)) dialog.choose("") } }
                        }
                        Label { visible: dialog.pendingDeleteId.length > 0; Layout.fillWidth: true; wrapMode: Text.WordWrap; text: "Nur den Bibliothekseintrag löschen. Bereits eingefügte Objekte und Quelldateien bleiben erhalten."; color: dialog.ink }
                        Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: "Einfügepunkt: linke untere Ecke. Originalgröße bleibt erhalten; das Programm verschiebt oder verkleinert nichts automatisch, um es passend zu machen."; color: dialog.ink }
                        RowLayout {
                            NumberField { id: insertX; objectName: "artLibraryInsertX"; label: "Links"; value: 0; dark: dialog.dark; resetOnFinish: false; Layout.fillWidth: true; onEdited: n => { value = n; resetInput() } }
                            NumberField { id: insertY; objectName: "artLibraryInsertY"; label: "Unten"; value: 0; dark: dialog.dark; resetOnFinish: false; Layout.fillWidth: true; onEdited: n => { value = n; resetInput() } }
                        }
                        Label { objectName: "artLibraryInsertionError"; visible: dialog.insertionError.length > 0; Layout.fillWidth: true; wrapMode: Text.WordWrap; textFormat: Text.PlainText; color: dialog.ink; text: dialog.insertionError }
                        ActionButton { objectName: "artLibraryInsert"; text: "Als Vorlagen einfügen"; primary: true; Layout.fillWidth: true; enabled: dialog.editable && !!dialog.selectedAssetId && dialog.insertionError.length === 0; onClicked: { if (!enabled) return; if (dialog.controller.insert(dialog.selectedAssetId,insertX.numericValue(),insertY.numericValue())) { dialog.inserted(); dialog.close() } } }
                    }
                }
            }
            ScrollView {
                id: captureScroll; clip: true; contentWidth: availableWidth
                ColumnLayout {
                    width: captureScroll.availableWidth; spacing: 12
                    Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: dialog.ink; text: "Die sichtbare Auswahl des aktiven Blatts wird als Geometriekopie gespeichert. Quellprojekt, Material, Leistung, Geschwindigkeit und Gerätezuordnung werden nicht übernommen." }
                    ActionButton { objectName: "artLibraryReviewSelection"; text: "Aktuelle Auswahl prüfen"; dark: dialog.dark; enabled: dialog.editable; onClicked: { dialog.controller.reviewSelection(); if (!captureName.text) captureName.text = dialog.document && dialog.document.selection.name ? dialog.document.selection.name : "Auswahl" } }
                    Label { Layout.fillWidth: true; visible: !!dialog.captureReview.valid; text: dialog.length(dialog.captureReview.widthMm) + " × " + dialog.length(dialog.captureReview.heightMm) + " · " + dialog.captureReview.objectCount + " Objekte"; color: dialog.ink }
                    Image { Layout.alignment: Qt.AlignHCenter; Layout.preferredWidth: 150; Layout.preferredHeight: 150; visible: !!dialog.captureReview.valid; fillMode: Image.PreserveAspectFit; source: dialog.captureReview.thumbnailSource || "" }
                    Repeater {
                        model: dialog.captureReview.issues || []
                        delegate: Label { required property var modelData; Layout.fillWidth: true; wrapMode: Text.WordWrap; textFormat: Text.PlainText; text: modelData.label || modelData.message || String(modelData); color: dialog.ink }
                    }
                    Label { visible: !!dialog.captureReview.error; Layout.fillWidth: true; wrapMode: Text.WordWrap; textFormat: Text.PlainText; text: dialog.captureReview.error || ""; color: "#c05242" }
                    Label { text: "Name (höchstens 100 Zeichen)"; color: dialog.ink }
                    DialogField { id: captureName; objectName: "artLibraryCaptureName"; dark: dialog.dark; Layout.fillWidth: true }
                    Label { text: "Tags, durch Komma getrennt"; color: dialog.ink }
                    DialogField { id: captureTags; objectName: "artLibraryCaptureTags"; dark: dialog.dark; Layout.fillWidth: true }
                    CheckBox {
                        id: conversionConsent; objectName: "artLibraryAcceptConversion"; Layout.fillWidth: true
                        text: "Die genannten Darstellungs- und Bearbeitungsverluste für diese Kopie übernehmen"
                        contentItem: Text { text: conversionConsent.text; textFormat: Text.PlainText; color: dialog.ink; wrapMode: Text.WordWrap; leftPadding: conversionConsent.indicator.width + conversionConsent.spacing; verticalAlignment: Text.AlignVCenter }
                    }
                    ActionButton {
                        objectName: "artLibrarySaveSelection"; text: "Geprüfte Kopie speichern"; primary: true; Layout.fillWidth: true
                        enabled: dialog.editable && !!dialog.captureReview.valid && (!dialog.captureReview.conversionRequired || conversionConsent.checked)
                        onClicked: {
                            if (!enabled) return
                            const id = dialog.controller.saveSelection(captureName.text,dialog.tags(captureTags.text),dialog.captureReview.token || "",conversionConsent.checked)
                            if (id) { searchInput.text = ""; tabs.currentIndex = 0; Qt.callLater(() => dialog.choose(id)) }
                        }
                    }
                }
            }
        }
        Label { objectName: "artLibraryStatus"; Layout.fillWidth: true; wrapMode: Text.WordWrap; textFormat: Text.PlainText; color: dialog.ink; text: !dialog.available ? "Grafikbibliothek in dieser Ansicht nicht verfügbar." : dialog.controller.blockReason || dialog.controller.message || dialog.library.message }
    }
    footer: RowLayout {
        Item { Layout.fillWidth: true }
        ActionButton { objectName: "artLibraryClose"; text: "Schließen"; dark: dialog.dark; onClicked: dialog.close() }
    }
}
