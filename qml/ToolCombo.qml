import QtQuick
import QtQuick.Controls
ComboBox {
    id: control
    property bool dark: false
    implicitHeight: 36
    contentItem: Text { text: control.displayText; color: control.dark ? "#e2eaf0" : "#263b4a"; font: control.font; verticalAlignment: Text.AlignVCenter; rightPadding: 22; elide: Text.ElideRight }
    indicator: Text { x: control.width-width-9; anchors.verticalCenter: parent.verticalCenter; text: "▾"; color: control.dark ? "#c3d4df" : "#526d7d" }
    background: Rectangle { radius: 5; color: control.dark ? "#202e39" : "#f5f8fa"; border.color: control.activeFocus ? "#209b76" : control.dark ? "#40525f" : "#dce4e9" }
}
