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
    readonly property string fontFamily: Workbench.fontFamily.length > 0 ? Workbench.fontFamily : "Segoe UI"
    readonly property string mono: "Consolas, monospace"

    // ---------------------------------------------------------------- Scale (View menu, Ctrl + / - / 0)
    // User preferences only: Qt already applies the Windows display scale, so it is never applied here.
    // uiFactor grows controls, margins and panels; text follows uiFactor * the font preference, the way
    // a browser or VS Code zoom does. Nothing here reaches the 3D scene or the camera.
    readonly property real uiFactor: Workbench.uiScale / 100
    readonly property real densityFactor: Workbench.density === Workbench.Compact ? 0.85
                                          : Workbench.density === Workbench.Comfortable ? 1.2 : 1.0
    readonly property real fontFactor: uiFactor * Workbench.fontScale / 100

    function fs(base) { return Math.max(8, Math.round(base * fontFactor)) }        // text, base px at 100 %
    function px(base) { return Math.round(base * uiFactor) }                       // controls and panels
    function sp(base) { return Math.round(base * uiFactor * densityFactor) }       // spacing and padding

    // ---------------------------------------------------------------- Font Sizes
    readonly property int micro: fs(10)
    readonly property int small: fs(11)
    readonly property int normal: fs(12)
    readonly property int large: fs(14)
    readonly property int title: fs(16)
    readonly property int heading: fs(18)

    // ---------------------------------------------------------------- Control metrics
    readonly property int controlHeight: Math.max(px(28), normal + sp(14))   // buttons, fields, tool buttons
    readonly property int toolbarHeight: Math.max(px(44), controlHeight + sp(12))
    readonly property int statusHeight: Math.max(px(28), small + sp(14))
    readonly property int splitHandle: 6                                     // grab strip of a panel divider
    readonly property int rowHeight: Math.max(px(32), normal + sp(18))       // list rows

    // ---------------------------------------------------------------- Spacings & Radii
    readonly property int gap: sp(8)
    readonly property int spacingXs: sp(4)
    readonly property int spacingSm: sp(6)
    readonly property int spacingMd: sp(8)
    readonly property int spacingLg: sp(12)
    readonly property int spacingXl: sp(16)
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
