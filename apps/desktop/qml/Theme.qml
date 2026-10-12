pragma Singleton
import QtQuick

QtObject {
    // ---------------------------------------------------------------- Surfaces & Backgrounds
    readonly property color bg: "#16171a"           // Deep carbon/graphite
    readonly property color surface: "#1c1e22"      // Viewport & canvas
    readonly property color panel: "#22252a"        // Sidebars & card backgrounds
    readonly property color panelAlt: "#282c33"     // Hovered cards & elevated panels
    readonly property color control: "#282c33"      // Form inputs & control rectangles
    readonly property color selection: "#2f4f72"    // Selected list items
    readonly property color panelHeader: "#1a1c20"  // Internal section headers
    readonly property color header: "#181a1d"       // Main window header & menubar
    readonly property color border: "#30343d"       // Clean subtle borders
    readonly property color borderSubtle: "#262930" // Secondary dividers
    readonly property color borderFocus: "#4382c4"  // Focus outline

    // ---------------------------------------------------------------- Steel Blue Accent
    readonly property color accent: "#4382c4"
    readonly property color accentHover: "#5595db"
    readonly property color accentPressed: "#376da6"
    readonly property color accentDim: "#1d3248"
    readonly property color accentSubtle: "#264382c4"
    readonly property color accentBorder: "#3b6fa7"

    // ---------------------------------------------------------------- Typography & Content
    readonly property color text: "#e1e4ea"
    readonly property color textDim: "#8c94a2"
    readonly property color textMuted: "#616875"
    readonly property string fontFamily: "Segoe UI, sans-serif"
    readonly property string mono: "Consolas, monospace"

    // ---------------------------------------------------------------- Font Sizes
    readonly property int micro: 10
    readonly property int small: 11
    readonly property int normal: 12
    readonly property int large: 14
    readonly property int title: 16
    readonly property int heading: 18

    // ---------------------------------------------------------------- Spacings & Radii
    readonly property int gap: 8
    readonly property int spacingXs: 4
    readonly property int spacingSm: 6
    readonly property int spacingMd: 8
    readonly property int spacingLg: 12
    readonly property int spacingXl: 16
    readonly property int radiusSmall: 3
    readonly property int radius: 4
    readonly property int radiusLarge: 6

    // ---------------------------------------------------------------- Status & Physics
    readonly property color ok: "#48b26e"
    readonly property color okDim: "#1c3826"
    readonly property color warn: "#d49638"
    readonly property color warnDim: "#3d2e18"
    readonly property color error: "#d9534f"
    readonly property color errorDim: "#3d1e1e"

    // SMP Physics Badge (subtle, non-distracting)
    readonly property color smpBadgeBg: "#281e33"
    readonly property color smpBadgeText: "#b886d4"
    readonly property color smpBadgeBorder: "#46305a"

    // ---------------------------------------------------------------- User Mode
    property bool advancedMode: false

    function severityColor(severity) {
        return severity === "error" ? error : severity === "warning" ? warn : textDim
    }
}
