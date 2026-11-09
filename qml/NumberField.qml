import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
RowLayout {
    id: field
    property string label: ""
    property real value: 0
    property string unit: "mm"
    property bool dark: false
    property int decimals: 1
    property bool resetOnFinish: true
    signal edited(real newValue)
    property alias inputText: input.text
    function resetInput() { input.text = Qt.binding(function() { return field.value.toFixed(field.decimals) }) }
    function numericValue() {
        let text = input.text.trim().replace(",", ".")
        return /^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?$/.test(text) ? Number(text) : NaN
    }
    spacing: 6
    Text { text: field.label; color: field.dark ? "#a8b7c2" : "#708290"; font.pixelSize: 12; Layout.preferredWidth: 14 }
    TextField {
        id: input
        Layout.fillWidth: true
        implicitHeight: 34
        text: field.value.toFixed(field.decimals)
        color: field.dark ? "#e1e8ed" : "#293b49"
        selectByMouse: true
        font.pixelSize: 12
        leftPadding: 9
        rightPadding: 36
        background: Rectangle { color: field.dark ? "#202d37" : "#f7f9fa"; radius: 5; border.color: input.activeFocus ? "#209b76" : field.dark ? "#3b4b58" : "#dce4ea" }
        Text { anchors.right: parent.right; anchors.rightMargin: 9; anchors.verticalCenter: parent.verticalCenter; text: field.unit; color: field.dark ? "#91a3b1" : "#9aa8b2"; font.pixelSize: 10 }
        onEditingFinished: {
            let n = field.numericValue()
            if (text.trim() !== "" && isFinite(n)) field.edited(n)
            if (field.resetOnFinish) field.resetInput()
        }
    }
}
