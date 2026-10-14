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
            validation: root.forge.validation,
            preview: {
                state: root.forge.scene.state,
                file: root.forge.scene.filePath,
                operation: root.forge.scene.operation,
                rendered: root.forge.scene.rendered,
                inspection: root.forge.scene.inspection,
                details: root.forge.scene.details
            }
        }, null, 2)
        font.family: Theme.mono
        font.pixelSize: Theme.small
    }
}
