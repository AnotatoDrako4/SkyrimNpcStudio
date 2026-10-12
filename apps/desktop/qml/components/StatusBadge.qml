import QtQuick
import SkyrimNpcStudio

Rectangle {
    id: root
    property string text
    property color tone: Theme.textDim
    implicitWidth: label.implicitWidth + 12
    implicitHeight: 18
    radius: 9
    color: Qt.rgba(tone.r, tone.g, tone.b, 0.18)
    border.color: tone

    Text {
        id: label
        anchors.centerIn: parent
        text: root.text
        color: root.tone
        font.pixelSize: Theme.small - 1
        font.bold: true
    }
}
