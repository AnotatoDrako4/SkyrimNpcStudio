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
    // Vector glyphs from the icon font Windows ships (never distributed with the studio); Icon.qml falls
    // back to a text label when neither family is installed.
    readonly property string iconFamily: Qt.fontFamilies().indexOf("Segoe Fluent Icons") >= 0 ? "Segoe Fluent Icons"
                                       : Qt.fontFamilies().indexOf("Segoe MDL2 Assets") >= 0 ? "Segoe MDL2 Assets" : ""

    // ---------------------------------------------------------------- Scale (View menu, Ctrl + / - / 0)
    // User preferences only: Qt already applies the Windows display scale, so it is never applied here.
    // uiFactor grows controls, margins and panels; text follows uiFactor * the font preference, the way
    // a browser or VS Code zoom does. Nothing here reaches the 3D scene or the camera.
    readonly property real uiFactor: Workbench.uiScale / 100
    readonly property real densityFactor: Workbench.density === Workbench.Compact ? 0.85
                                          : Workbench.density === Workbench.Comfortable ? 1.2 : 1.0
    readonly property real fontFactor: uiFactor * Workbench.fontScale / 100

    // Text: base px at 100 %, scaled by the preferences but never below the legible minimum of its role
    // (WorkbenchState::legibleFontPixels: body 11 px, secondary 10, captions 9, titles 12). The preference
    // is kept as chosen; `legibilityFloor` tells the window to say that text is being held up.
    function fs(base) { return Workbench.fontPixels(base, Workbench.uiScale, Workbench.fontScale) }
    function px(base) { return Math.round(base * uiFactor) }                       // controls and panels
    function sp(base) { return Math.round(base * uiFactor * densityFactor) }       // spacing and padding
    readonly property bool legibilityFloor: Workbench.legibilityFloorActive

    // ---------------------------------------------------------------- Font Sizes
    // Body text 13 px at 100 % (names, fields, buttons); secondary 11; captions 10; titles 15–20.
    readonly property int micro: fs(10)
    readonly property int small: fs(11)
    readonly property int normal: fs(13)
    readonly property int large: fs(15)
    readonly property int title: fs(17)
    readonly property int heading: fs(20)
    readonly property int iconSize: fs(14)                                          // glyphs follow the text

    // ---------------------------------------------------------------- Control metrics
    // Frequent actions get 32 px targets on a desktop (28 px in the compact density), whatever the scale says.
    readonly property int controlHeight: Math.max(px(Workbench.density === Workbench.Compact ? 28 : 32), normal + sp(14))
    readonly property int toolbarHeight: Math.max(px(44), controlHeight + sp(12))
    readonly property int statusHeight: Math.max(px(28), small + sp(14))
    readonly property int splitHandle: 8                                     // grab strip of a panel divider (workbench_check.cpp kHandle)
    readonly property int rowHeight: Math.max(px(32), normal + sp(18))       // list rows
    readonly property int focusWidth: 2                                      // focus ring of keyboard-focused controls

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
