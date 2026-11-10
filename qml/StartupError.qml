import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
ApplicationWindow {
    visible: true; width: 540; height: 210; title: "Blazer Studio"
    color: "#f3f6f8"
    ColumnLayout { anchors.fill: parent; anchors.margins: 24; spacing: 16
        Text { text: "Blazer Studio kann nicht starten"; font.pixelSize: 20; color: "#263b4a" }
        Text { text: startupErrorMessage; Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 14; color: "#263b4a" }
        ActionButton { text: "Schließen"; onClicked: Qt.quit(); Layout.alignment: Qt.AlignRight }
    }
}
