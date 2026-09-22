import QtQuick
import QtQuick.Controls.Basic
Button {
    id: control
    property bool primary: false
    property bool selected: false
    property string hint: text
    Theme { id: theme }
    implicitHeight: 40
    implicitWidth: Math.max(90, contentItem.implicitWidth + 28)
    font.pixelSize: 13
    focusPolicy: Qt.StrongFocus
    Accessible.name: hint
    ToolTip.visible: hovered && hint.length > 0
    ToolTip.text: hint
    ToolTip.delay: 700
    // Basic Button renders and tints SVG icons with the same foreground as text.
    palette.buttonText: enabled ? theme.text : theme.muted
    icon.color: enabled ? theme.text : theme.muted
    icon.width: 20
    icon.height: 20
    background: Rectangle {
        radius: 7
        color: control.down ? "#315785" : control.primary ? "#286AC7"
             : control.selected || control.hovered ? theme.elevated : theme.surface
        border.width: control.visualFocus ? 2 : 1
        border.color: control.visualFocus || control.selected ? theme.accent : theme.border
        opacity: control.enabled ? 1 : 0.5
    }
}
