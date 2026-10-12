import QtQuick
import QtQuick.Layouts
import SkyrimNpcStudio

RowLayout {
    id: root
    property string label
    property string value
    property bool mono: false
    property color valueColor: Theme.text
    Layout.fillWidth: true
    spacing: 8

    Text {
        text: root.label
        color: Theme.textDim
        font.pixelSize: Theme.small
        Layout.preferredWidth: 120
        elide: Text.ElideRight
    }
    TextEdit {
        // Selectable so FormKeys and paths can be copied.
        text: root.value.length > 0 ? root.value : "—"
        color: root.valueColor
        font.pixelSize: Theme.small
        font.family: root.mono ? Theme.mono : Qt.application.font.family
        readOnly: true
        selectByMouse: true
        wrapMode: TextEdit.WrapAnywhere
        Layout.fillWidth: true
    }
}
