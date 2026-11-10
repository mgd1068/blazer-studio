import QtQuick
import QtQuick.Controls
import "Help.js" as Help
Item {
    id: bubble
    objectName: "helpBubble"
    property var target: parent
    property string description: Help.describe(target)
    width: 0; height: 0
    Component.onCompleted: if(target && typeof target.hoverEnabled!=="undefined")target.hoverEnabled=true
    ToolTip {
        id: helpTip
        objectName: "helpTooltip"
        parent: bubble.target
        visible: bubble.target && (bubble.target.hovered || (bubble.target.activeFocus && (bubble.target.focusReason===Qt.TabFocusReason || bubble.target.focusReason===Qt.BacktabFocusReason)))
        text: bubble.description; delay: 650; timeout: 12000
        padding: 12
        background: Rectangle { radius: 7; color: helpTip.palette.window; border.color: helpTip.palette.mid }
        contentItem: Text { text: helpTip.text; color: helpTip.palette.text; font.pixelSize: 13; wrapMode: Text.WordWrap }
        width: Math.min(380,implicitWidth)
    }
}
