import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Help.js" as Help
RowLayout {
    id: field
    property string label: ""
    property real value: 0
    property string hint: Help.numeric(label,shownUnit,field)
    property string unit: "mm"
    property bool dark: false
    property int decimals: 1
    property bool resetOnFinish: true
    signal edited(real newValue)
    property alias inputText: input.text
    readonly property real displayFactor: unit==="mm" && typeof appServices!=="undefined" && appServices && appServices.lengthUnit==="in" ? 25.4 : 1
    readonly property int displayDecimals: unit==="mm" && displayFactor===25.4 ? Math.max(4,decimals+2) : decimals
    readonly property string shownUnit: unit==="mm" ? ((draftEdited ? draftFactor : displayFactor)===25.4 ? "in" : "mm") : unit
    property bool draftEdited: false
    property real draftFactor: 1
    property bool initialized: false
    function formattedValue() { return (value/displayFactor).toFixed(displayDecimals) }
    function resetInput() {
        draftEdited=false;draftFactor=displayFactor
        input.text=Qt.binding(function() { return field.formattedValue() })
    }
    onDisplayFactorChanged: if(initialized && !draftEdited)resetInput()
    Component.onCompleted: {initialized=true;resetInput()}
    function numericValue() {
        if(!draftEdited && input.text===formattedValue())return value
        let text = input.text.trim().replace(",", ".")
        if(unit!=="mm")return /^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?$/.test(text) ? Number(text) : NaN
        let match=/^([+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?)\s*(mm|in|")?$/i.exec(text)
        if(!match)return NaN
        let suffix=(match[2] || "").toLowerCase()
        return Number(match[1])*(suffix==="mm" ? 1 : suffix ? 25.4 : draftEdited ? draftFactor : displayFactor)
    }
    spacing: 6
    Text { id: labelText; text: field.label; color: field.dark ? "#a8b7c2" : "#708290"; font.pixelSize: 12; Layout.preferredWidth: Math.max(14,labelText.implicitWidth) }
    TextField {
        id: input
        objectName: "numericEditor"
        HelpBubble { target: input; description: field.hint.replace(/in Millimetern/g,"in der angezeigten Einheit") + (field.unit==="mm" ? " Dezimale Längen können mit mm oder in angegeben werden." : "") }
        Layout.fillWidth: true
        implicitHeight: 34
        text: field.formattedValue()
        color: field.dark ? "#e1e8ed" : "#293b49"
        selectByMouse: true
        font.pixelSize: 12
        leftPadding: 9
        rightPadding: 36
        background: Rectangle { color: field.dark ? "#202d37" : "#f7f9fa"; radius: 5; border.color: input.activeFocus ? "#209b76" : field.dark ? "#3b4b58" : "#dce4ea" }
        Text { anchors.right: parent.right; anchors.rightMargin: 9; anchors.verticalCenter: parent.verticalCenter; text: field.shownUnit; color: field.dark ? "#91a3b1" : "#9aa8b2"; font.pixelSize: 10 }
        onTextEdited: {
            if(!field.draftEdited){field.draftFactor=field.displayFactor;field.draftEdited=true}
            let current=text;text=current
        }
        onEditingFinished: {
            let n = field.numericValue()
            if ((field.draftEdited || text!==field.formattedValue()) && text.trim() !== "" && isFinite(n)) field.edited(n)
            if (field.resetOnFinish) field.resetInput()
            else {field.draftEdited=false;field.draftFactor=field.displayFactor}
        }
    }
}
