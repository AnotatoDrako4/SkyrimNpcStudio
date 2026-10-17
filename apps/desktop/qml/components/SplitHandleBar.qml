import QtQuick
import QtQuick.Controls
import SkyrimNpcStudio

// The divider between two panels: a 1 px line in a strip wide enough to grab, lit on hover and drag.
Rectangle {
    id: bar
    property bool vertical: false   // true for the divider of a vertical SplitView (a horizontal bar)

    implicitWidth: vertical ? 0 : Theme.splitHandle
    implicitHeight: vertical ? Theme.splitHandle : 0
    color: SplitHandle.pressed ? Theme.accentPressed : SplitHandle.hovered ? Theme.accentDim : Theme.bg

    Rectangle {
        anchors.centerIn: parent
        width: bar.vertical ? parent.width : 1
        height: bar.vertical ? 1 : parent.height
        color: bar.SplitHandle.hovered || bar.SplitHandle.pressed ? Theme.accent : Theme.borderSubtle
    }
}
