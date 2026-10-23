import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

// A tool action with a glyph and a label. When the row is narrow the label gives way (`showLabel: false`)
// but the name never does: it stays as the accessible name and in the tooltip, with its shortcut. The
// target keeps Theme.controlHeight whatever the scale, and the states (hover, pressed, checked, disabled,
// keyboard focus) are visible.
ToolButton {
    id: button
    property string glyph: ""
    property string label: ""
    property bool showLabel: true
    property string tip: ""             // tooltip body; the label when empty
    property string shortcutText: ""    // appended to the tooltip: "Ctrl+B"
    property color tone: Theme.text     // colour of the glyph and label when enabled
    property bool emphasized: false     // accent background (the main action of a toolbar)

    text: showLabel ? label : ""
    implicitHeight: Theme.controlHeight
    implicitWidth: Math.max(Theme.controlHeight, implicitContentWidth + leftPadding + rightPadding)
    leftPadding: text.length > 0 ? Theme.sp(10) : Theme.sp(6)
    rightPadding: leftPadding
    topPadding: 0
    bottomPadding: 0
    Accessible.name: label
    Accessible.description: tip
    ToolTip.visible: hovered && (tip.length > 0 || !showLabel || shortcutText.length > 0)
    ToolTip.delay: 450
    ToolTip.text: (tip.length > 0 ? tip : label) + (shortcutText.length > 0 ? "  ·  " + shortcutText : "")

    readonly property color fg: !enabled ? Theme.textMuted : emphasized ? Theme.onAccent : checked ? Theme.accent : tone

    contentItem: RowLayout {
        spacing: Theme.sp(6)
        Icon {
            visible: button.glyph.length > 0
            name: button.glyph
            fallback: button.text.length > 0 ? "" : button.label.substring(0, 2)
            color: button.fg
            Layout.alignment: Qt.AlignVCenter
        }
        Text {
            visible: button.text.length > 0
            text: button.text
            color: button.fg
            font.family: Theme.fontFamily
            font.pixelSize: Theme.normal
            font.bold: button.font.bold
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
        }
    }
    background: Rectangle {
        radius: Theme.radius
        color: button.emphasized ? (button.down ? Theme.accentPressed : button.hovered ? Theme.accentHover : Theme.accent)
             : button.down ? Theme.accentDim
             : button.checked ? Theme.accentSubtle
             : button.hovered ? Theme.panelAlt : "transparent"
        border.width: button.visualFocus ? Theme.focusWidth : button.checked ? 1 : 0
        border.color: button.visualFocus ? Theme.borderFocus : Theme.accentBorder
        opacity: button.enabled ? 1 : 0.55
    }
}
