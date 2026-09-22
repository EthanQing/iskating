import QtQuick
QtObject {
    readonly property color background: "#10151D"
    readonly property color surface: "#151B25"
    readonly property color elevated: "#202A38"
    readonly property color border: "#2C394B"
    readonly property color text: "#E8EEF7"
    readonly property color muted: "#94A5BC"
    readonly property color accent: "#4F92FF"
    readonly property color warning: "#E8B35A"
    readonly property color success: "#67CDA8"
    function stateColor(state: string): color {
        return state === "error" ? "#FF8188" : state === "warning" ? warning
             : state === "online" ? success : muted
    }
}
