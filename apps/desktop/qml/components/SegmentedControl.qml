import QtQuick
import QtQuick.Controls
import SkyrimNpcStudio

// A row of exclusive options (list / gallery, all / clothing / light / heavy): one piece, one selection,
// arrow keys move it, and every option keeps a readable label or a tooltip.
// model: ["Lista", "Galería"] or [{ label, glyph, tip }].
Rectangle {
    id: control
    property var model: []
    property int currentIndex: 0
    property bool compact: false   // glyph only when an option has one
    property bool stretch: false   // options share the whole width equally (when the control fills a row)
    signal activated(int index)

    implicitHeight: Theme.controlHeight
    // Stretched segments follow the real width, so the implicit width must not come from them (binding loop).
    implicitWidth: stretch ? Theme.px(200) : row.implicitWidth + 2
    radius: Theme.radius
    color: Theme.control
    border.color: control.activeFocus ? Theme.borderFocus : Theme.border
    border.width: control.activeFocus ? Theme.focusWidth : 1
    activeFocusOnTab: true
    Accessible.role: Accessible.PageTabList

    function labelOf(entry) { return typeof entry === "string" ? entry : (entry.label ?? "") }
    function glyphOf(entry) { return typeof entry === "string" ? "" : (entry.glyph ?? "") }
    function tipOf(entry) { return typeof entry === "string" ? "" : (entry.tip ?? "") }
    // The owner binds currentIndex and changes its own state in onActivated, so the binding is never broken.
    function select(index) {
        if (index < 0 || index >= model.length || index === currentIndex) return
        activated(index)
    }
    Keys.onLeftPressed: select(currentIndex - 1)
    Keys.onRightPressed: select(currentIndex + 1)

    Row {
        id: row
        anchors.fill: parent
        anchors.margins: 1
        spacing: 0
        Repeater {
            model: control.model
            delegate: AbstractButton {
                id: segment
                required property var modelData
                required property int index
                readonly property bool current: control.currentIndex === index
                readonly property string label: control.labelOf(modelData)
                readonly property string glyph: control.glyphOf(modelData)
                height: row.height
                readonly property real natural: Math.max(Theme.controlHeight - 2, contentItem.implicitWidth + Theme.sp(16))
                width: control.stretch && control.model.length > 0 ? Math.max(natural, row.width / control.model.length) : natural
                checkable: false
                Accessible.name: label
                Accessible.role: Accessible.PageTab
                ToolTip.visible: hovered && (control.compact || control.tipOf(modelData).length > 0)
                ToolTip.delay: 450
                ToolTip.text: control.tipOf(modelData).length > 0 ? control.tipOf(modelData) : label
                onClicked: { control.forceActiveFocus(); control.select(index) }
                // The control sizes contentItem itself (anchors on it are ignored): an Item fills the segment and
                // centres the glyph and label inside it, also when the segments are stretched.
                contentItem: Item {
                    implicitWidth: segmentRow.implicitWidth
                    implicitHeight: segmentRow.implicitHeight
                    Row {
                        id: segmentRow
                        spacing: Theme.sp(5)
                        anchors.centerIn: parent
                        Icon {
                            visible: segment.glyph.length > 0
                            name: segment.glyph
                            color: segment.current ? Theme.onAccent : Theme.textDim
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            visible: !control.compact || segment.glyph.length === 0
                            text: segment.label
                            color: segment.current ? Theme.onAccent : segment.hovered ? Theme.text : Theme.textDim
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.normal
                            font.bold: segment.current
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                }
                background: Rectangle {
                    radius: Theme.radius - 1
                    color: segment.current ? Theme.accent : segment.down ? Theme.accentDim : segment.hovered ? Theme.panelAlt : "transparent"
                }
            }
        }
    }
}
