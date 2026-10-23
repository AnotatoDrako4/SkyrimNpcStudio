import QtQuick
import QtQuick.Controls
import SkyrimNpcStudio

// The divider between two panels: a 1 px line in a strip (Theme.splitHandle, 8 px) wide enough to grab,
// lit on hover and drag, with a short grip in the middle so it can be found. A double click asks the
// owner to put the neighbouring panel back to its default size (resetRequested).
Rectangle {
    id: bar
    property bool vertical: false   // true for the divider of a vertical SplitView (a horizontal bar)
    signal resetRequested()

    implicitWidth: vertical ? 0 : Theme.splitHandle
    implicitHeight: vertical ? Theme.splitHandle : 0
    color: SplitHandle.pressed ? Theme.accentPressed : SplitHandle.hovered ? Theme.accentDim : Theme.bg
    Accessible.role: Accessible.Grip
    Accessible.name: vertical ? qsTr("Divisor de la consola") : qsTr("Divisor de panel")
    Accessible.description: qsTr("Arrastra para cambiar el tamaño; doble clic para restablecerlo")

    Rectangle {
        anchors.centerIn: parent
        width: bar.vertical ? parent.width : 1
        height: bar.vertical ? 1 : parent.height
        color: bar.SplitHandle.hovered || bar.SplitHandle.pressed ? Theme.accent : Theme.borderSubtle
    }
    // Grip: always faintly there, clear on hover.
    Rectangle {
        anchors.centerIn: parent
        width: bar.vertical ? Theme.px(36) : 4
        height: bar.vertical ? 4 : Theme.px(36)
        radius: 2
        color: bar.SplitHandle.hovered || bar.SplitHandle.pressed ? Theme.accent : Theme.border
        opacity: bar.SplitHandle.hovered || bar.SplitHandle.pressed ? 1 : 0.7
    }
    HoverHandler { cursorShape: bar.vertical ? Qt.SplitVCursor : Qt.SplitHCursor }
    // Double click: SplitView takes the exclusive grab of the press on its handle, so a TapHandler here never
    // sees the second tap. SplitHandle.pressed is kept by SplitView itself: two releases without movement
    // inside the system double-click interval are a double click.
    readonly property bool pressedNow: SplitHandle.pressed
    property real _pressX: 0
    property real _pressY: 0
    property double _lastClick: 0
    onPressedNowChanged: {
        if (pressedNow) {
            _pressX = x
            _pressY = y
            return
        }
        if (Math.abs(x - _pressX) > 1 || Math.abs(y - _pressY) > 1) {   // a drag, not a click
            _lastClick = 0
            return
        }
        const now = Date.now()
        if (_lastClick > 0 && now - _lastClick <= Qt.styleHints.mouseDoubleClickInterval) {
            _lastClick = 0
            resetRequested()
        } else {
            _lastClick = now
        }
    }
    // A divider hidden with its panel (F11, Ctrl+B) never gets a hover-leave: without `visible` the tip would stay up.
    ToolTip.visible: visible && SplitHandle.hovered && !SplitHandle.pressed
    ToolTip.delay: 900
    ToolTip.text: qsTr("Arrastra para redimensionar · doble clic para restablecer")
}
