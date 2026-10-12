import QtQuick
import QtQuick.Layouts
import SkyrimNpcStudio

Rectangle {
    id: root
    property string title
    property string detail
    Layout.fillWidth: true
    implicitHeight: 26
    color: Theme.header
    border.color: Theme.border

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        Text {
            text: root.title
            color: Theme.text
            font.pixelSize: Theme.normal
            font.bold: true
        }
        Item { Layout.fillWidth: true }
        Text {
            text: root.detail
            color: Theme.textDim
            font.pixelSize: Theme.small
        }
    }
}
