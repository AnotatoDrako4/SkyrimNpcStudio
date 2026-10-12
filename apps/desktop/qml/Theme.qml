pragma Singleton
import QtQuick

QtObject {
    readonly property color bg: "#1b1d22"
    readonly property color panel: "#22252b"
    readonly property color panelAlt: "#272b32"
    readonly property color header: "#2a2e36"
    readonly property color border: "#363a43"
    readonly property color text: "#dcdfe4"
    readonly property color textDim: "#98a0ab"
    readonly property color accent: "#4f8cc9"
    readonly property color accentDim: "#2f4f72"
    readonly property color ok: "#5bb974"
    readonly property color warn: "#e0a43a"
    readonly property color error: "#e05d5d"

    readonly property int gap: 8
    readonly property int small: 11
    readonly property int normal: 12
    readonly property int large: 16
    readonly property string mono: "Consolas"

    function severityColor(severity) {
        return severity === "error" ? error : severity === "warning" ? warn : textDim
    }
}
