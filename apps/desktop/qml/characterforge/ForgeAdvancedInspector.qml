import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

ColumnLayout {
    id: root
    required property CharacterForgeViewModel forge
    Text {
        text: qsTr("Inspector avanzado")
        color: Theme.text
        font.bold: true
    }
    TextArea {
        Layout.fillWidth: true
        readOnly: true
        selectByMouse: true
        wrapMode: TextEdit.WrapAnywhere
        text: JSON.stringify({
            npc: root.forge.npc,
            model: root.forge.workspace.model || null,
            validation: root.forge.validation
        }, null, 2)
        font.family: Theme.mono
        font.pixelSize: Theme.small
    }
}
