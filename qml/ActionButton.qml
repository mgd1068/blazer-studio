import QtQuick
import QtQuick.Controls

Button {
    id: control
    property bool primary: false
    property bool destructive: false
    property bool selected: false
    property bool compact: false
    property bool dark: false
    property string hint: ""
    implicitHeight: compact ? 30 : 36
    implicitWidth: Math.max(compact ? 32 : 40, contentItem.implicitWidth + 24)
    hoverEnabled: true
    padding: 8
    font.pixelSize: 12
    font.weight: Font.Medium
    contentItem: Text {
        text: control.text
        font: control.font
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        color: !control.enabled ? (control.dark ? "#65717b" : "#9ba5ac") : control.primary ? "white" : control.destructive ? "#cf645b" : control.selected ? (control.dark ? "#8adcc2" : "#138968") : control.dark ? "#dce5ec" : "#394b59"
    }
    background: Rectangle {
        radius: 6
        color: !control.enabled ? (control.dark ? "#25303a" : "#f0f2f4") : control.primary ? (control.pressed ? "#147454" : control.hovered ? "#198766" : "#209b76") : control.selected ? (control.dark ? "#24493f" : "#e5f3ee") : control.pressed ? (control.dark ? "#394a57" : "#dfe6ea") : control.hovered ? (control.dark ? "#303e4a" : "#edf2f5") : (control.dark ? "#25323d" : "#ffffff")
        border.color: control.primary || control.selected ? "transparent" : control.dark ? "#3b4955" : "#dce3e8"
    }
    ToolTip.visible: hovered && hint.length > 0
    ToolTip.text: hint
    ToolTip.delay: 700
}
