pragma Singleton
import QtQuick

QtObject {
    // Colors come from the theme and accent chosen in Configuración (AppSettings.palette, C++). The names stay the
    // same for every consumer; the default ("Grafito oscuro" + "Azul acero") is the original palette value for value.
    // Nothing here reaches textures, models or the 3D scene.
    readonly property var pal: AppSettings.palette

    // ---------------------------------------------------------------- Surfaces & Backgrounds
    readonly property color bg: pal.bg                     // Deep carbon/graphite
    readonly property color surface: pal.surface           // Viewport & canvas
    readonly property color panel: pal.panel               // Sidebars & card backgrounds
    readonly property color panelAlt: pal.panelAlt         // Hovered cards & elevated panels
    readonly property color control: pal.control           // Form inputs & control rectangles
    readonly property color selection: pal.selection       // Selected list items
    readonly property color panelHeader: pal.panelHeader   // Internal section headers
    readonly property color header: pal.header             // Main window header & menubar
    readonly property color border: pal.border             // Clean subtle borders
    readonly property color borderSubtle: pal.borderSubtle // Secondary dividers
    readonly property color borderFocus: pal.borderFocus   // Focus outline

    // ---------------------------------------------------------------- Accent
    readonly property color accent: pal.accent
    readonly property color accentHover: pal.accentHover
    readonly property color accentPressed: pal.accentPressed
    readonly property color accentDim: pal.accentDim
    readonly property color accentSubtle: pal.accentSubtle
    readonly property color accentBorder: pal.accentBorder
    readonly property color onAccent: pal.onAccent         // text drawn on the accent

    // ---------------------------------------------------------------- Typography & Content
    readonly property color text: pal.text
    readonly property color textDim: pal.textDim
    readonly property color textMuted: pal.textMuted
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
    // Semantic colors: each theme has its own legible shade; the accent never replaces them.
    readonly property color ok: pal.ok
    readonly property color okDim: pal.okDim
    readonly property color warn: pal.warn
    readonly property color warnDim: pal.warnDim
    readonly property color error: pal.error
    readonly property color errorDim: pal.errorDim

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
