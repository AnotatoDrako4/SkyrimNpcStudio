import QtQuick
import SkyrimNpcStudio

// A vector glyph from the icon font Windows ships (Theme.iconFamily: Segoe Fluent Icons, else Segoe MDL2
// Assets). `name` picks a glyph from the table; when the font is missing, `fallback` (a short text) shows
// instead, so an action never turns into an empty square. Emoji are not used: their width and colour vary.
Text {
    id: icon
    property string name
    property string fallback: ""
    property int size: Theme.iconSize

    // Code points shared by Segoe MDL2 Assets and Segoe Fluent Icons.
    readonly property var glyphs: ({
        "person": "", "people": "", "tag": "", "forge": "",
        "folder-open": "", "folder": "", "save": "", "document": "",
        "fullscreen": "", "exit-fullscreen": "", "layout": "",
        "panel-left": "", "panel-right": "", "panel-bottom": "",
        "settings": "", "export": "", "search": "", "filter": "",
        "clear": "", "more": "", "camera": "", "light": "",
        "diagnostic": "", "info": "", "warning": "", "ok": "", "error": "",
        "chevron-down": "", "chevron-up": "", "chevron-left": "", "chevron-right": "",
        "undo": "", "redo": "", "add": "", "remove": "", "close": "",
        "eye": "", "copy": "", "keyboard": "", "console": "", "refresh": "",
        "switch": "", "library": "", "list": "", "grid": "", "pin": "",
        "help": "", "link": "", "bulb": "", "accept": "", "lock": "",
        "play": "", "zoom-in": "", "zoom-out": "", "home": ""
    })
    function glyphOf(n) { return glyphs[n] ?? "" }
    readonly property bool available: Theme.iconFamily.length > 0 && glyphOf(name).length > 0

    text: available ? glyphOf(name) : fallback
    font.family: available ? Theme.iconFamily : Theme.fontFamily
    font.pixelSize: size
    color: Theme.text
    verticalAlignment: Text.AlignVCenter
    horizontalAlignment: Text.AlignHCenter
    renderType: Text.NativeRendering
    Accessible.ignored: true
}
