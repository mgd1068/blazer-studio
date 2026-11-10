import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BlazerStudio

ScrollView {
    id: relief
    objectName: "reliefPanel"
    property bool dark: false
    readonly property color ink: dark ? "#e2eaf0" : "#263b4a"
    readonly property color muted: dark ? "#a5b9c7" : "#708594"
    readonly property bool selected: project.selection.kind === "relief"
    readonly property var settings: project.selection.relief || ({passes:16, interval:0.1, gamma:1, inverted:false, depthPerPass:0, crosshatch:false,cleanupEvery:0,cleanupPower:5})
    signal importRequested()
    signal photoRequested()
    clip: true
    contentWidth: availableWidth
    ColumnLayout {
        width: relief.availableWidth
        spacing: 12
        Item { height: 6 }
        RowLayout {
            Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20
            Text { text: "Relief · 2,5D"; color: relief.ink; font.pixelSize: 22; font.weight: Font.DemiBold; Layout.fillWidth: true }
            ActionButton { objectName: "importRelief"; text: "Tiefenkarte laden"; dark: relief.dark; onClicked: relief.importRequested() }
        }
        RowLayout {
            Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20
            ActionButton { text: "Foto → KI-Tiefenkarte"; dark: relief.dark; enabled: typeof depthService!=="undefined" && !depthService.busy; onClicked: relief.photoRequested() }
            ActionButton { text: "Abbrechen"; dark: relief.dark; visible: typeof depthService!=="undefined" && depthService.busy; onClicked: depthService.cancel() }
        }
        Text { visible: typeof depthService!=="undefined"; text: typeof depthService!=="undefined" ? depthService.message : ""; color: relief.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20 }
        Text {
            Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20
            text: "Schwarz = tief · Weiß = stehen lassen. Beim direkten Bildimport wird Helligkeit verwendet. Die optionale lokale KI schätzt räumliche Tiefe aus einem Foto; ihr Ergebnis ist keine vermessene Form."
            wrapMode: Text.WordWrap; color: relief.muted; font.pixelSize: 11
        }
        ReliefPreview {
            objectName: "reliefPreview"; Layout.fillWidth: true; Layout.preferredHeight: 260
            document: project; dark: relief.dark; slice: sliceSlider.value; threeD: threeD.checked
        }
        RowLayout {
            Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20
            CheckBox { id: threeD; objectName: "reliefThreeD"; text: "3D-Vorschau"; checked: true }
            Text { text: "Tiefe überhöht · Modell, keine Materialmessung"; color: relief.muted; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        }
        RowLayout {
            Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20
            Text { text: sliceSlider.value === 0 ? "Gesamtrelief" : "Schicht " + sliceSlider.value + "/" + relief.settings.passes; color: relief.ink; Layout.preferredWidth: 105 }
            Slider { id: sliceSlider; objectName: "reliefSlice"; Layout.fillWidth: true; from: 0; to: relief.settings.passes; stepSize: 1; snapMode: Slider.SnapAlways; enabled: relief.selected }
            ActionButton { text: "Alle"; compact: true; dark: relief.dark; onClicked: sliceSlider.value=0 }
        }
        GridLayout {
            enabled: relief.selected
            columns: 2; columnSpacing: 12; rowSpacing: 9
            Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20
            Text { text: "Schichten / max. Durchgänge"; color: relief.ink; font.pixelSize: 11 }
            SpinBox { id: passesInput; objectName: "reliefPasses"; Layout.fillWidth: true; from: 1; to: 256; value: relief.settings.passes; editable: true; onValueModified: project.setReliefSetting("passes",value) }
            Text { text: "Rasterabstand"; color: relief.ink; font.pixelSize: 11 }
            NumberField { objectName: "reliefInterval"; Layout.fillWidth: true; dark: relief.dark; decimals: 2; value: relief.settings.interval; unit: "mm"; onEdited: n => project.setReliefSetting("interval",n) }
            Text { text: "Tiefenkurve / Gamma"; color: relief.ink; font.pixelSize: 11 }
            NumberField { objectName: "reliefGamma"; Layout.fillWidth: true; dark: relief.dark; decimals: 2; value: relief.settings.gamma; unit: "γ"; onEdited: n => project.setReliefSetting("gamma",n) }
            Text { text: "Helligkeit umkehren"; color: relief.ink; font.pixelSize: 11 }
            CheckBox { objectName: "reliefInvert"; text: "Weiß tief, Schwarz unberührt"; checked: relief.settings.inverted; onClicked: project.setReliefSetting("inverted",checked) }
            Text { text: "Richtung je Schicht wechseln"; color: relief.ink; font.pixelSize: 11 }
            CheckBox { text: "0° / 90°"; checked: relief.settings.crosshatch || false; onClicked: project.setReliefSetting("crosshatch",checked) }
            Text { text: "Reinigung alle N Schichten (0 aus)"; color: relief.ink; font.pixelSize: 11 }
            SpinBox { from: 0; to: 256; value: relief.settings.cleanupEvery || 0; onValueModified: project.setReliefSetting("cleanupEvery",value) }
            Text { text: "Reinigungsleistung"; color: relief.ink; font.pixelSize: 11 }
            NumberField { Layout.fillWidth: true; dark: relief.dark; value: relief.settings.cleanupPower || 0; unit: "%"; onEdited: n=>project.setReliefSetting("cleanupPower",n) }
            Text { text: "Gemessener Abtrag / Durchgang"; color: relief.ink; font.pixelSize: 11 }
            NumberField { objectName: "reliefDepthPerPass"; Layout.fillWidth: true; dark: relief.dark; decimals: 3; value: relief.settings.depthPerPass; unit: "mm"; onEdited: n => project.setReliefSetting("depthPerPass",n) }
        }
        Text {
            objectName: "reliefDepthLabel"
            Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20
            text: !relief.selected ? "Reliefbild auswählen. Position, Maße, Leistung und Geschwindigkeit stehen im rechten Objektbereich."
                 : relief.settings.depthPerPass > 0 ? "Maximale Tiefe: ca. " + (relief.settings.depthPerPass * relief.settings.passes).toFixed(3) + " mm, aus gemessenem Abtrag geschätzt. Nur für dasselbe Material und dieselben Laserparameter gültig."
                 : "Relative Tiefe: " + relief.settings.passes + " Schichten. Abtrag 0 bedeutet: keine Kalibrierung, keine Millimeterzusage. Leistung und Geschwindigkeit rechts einstellen."
            wrapMode: Text.WordWrap; color: relief.muted; font.pixelSize: 11
        }
        Text {
            visible: relief.selected
            Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20
            text: project.jobSummary.valid ? "Gesamtjob: " + project.jobSummary.segments + " Segmente · geometrisch ca. " + Math.ceil(project.jobSummary.seconds / 60) + " min. Pfade unter Vorschau prüfen."
                                          : project.jobSummary.errors.join("\n")
            wrapMode: Text.WordWrap; color: project.jobSummary.valid ? relief.muted : "#cf645b"; font.pixelSize: 11
        }
        Text {
            Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20
            text: "Die Berechnung erzeugt XY-Durchgänge ohne Z-Nachführung. Kieselkrümmung und Materialabtrag werden nicht gemessen. Fühlbares Relief mit dem vorhandenen M3 ist noch nicht erprobt. Importieren startet keinen Laser."
            wrapMode: Text.WordWrap; color: relief.muted; font.pixelSize: 10
        }
        Item { height: 15 }
    }
}
