import QtQuick
import QtQuick.Controls

TextField {
    id: field
    property bool dark: false
    implicitHeight: 38
    selectByMouse: true
    color: dark ? "#e2eaf0" : "#263b4a"
    placeholderTextColor: dark ? "#a2b8c7" : "#667e90"
    background: Rectangle {
        radius: 5
        color: field.dark ? "#25323d" : "#f7f9fb"
        border.color: field.activeFocus ? "#209b76" : field.dark ? "#526675" : "#c5d2db"
    }
}
