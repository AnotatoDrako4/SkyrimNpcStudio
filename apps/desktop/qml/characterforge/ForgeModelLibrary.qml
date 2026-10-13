import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

ColumnLayout {
    id: root
    required property CharacterForgeViewModel forge
    Text {
        text: qsTr("Biblioteca del proyecto")
        color: Theme.text
        font.bold: true
        font.pixelSize: Theme.title
    }
    Text {
        Layout.fillWidth: true
        text: qsTr("Archivos que seleccionaste. Se conservan referencias locales; no se copian recursos al proyecto ni se garantiza su disponibilidad en otra máquina.")
        color: Theme.textDim
        wrapMode: Text.WordWrap
    }
    Text {
        visible: !(root.forge.workspace.library || []).length
        text: qsTr("La biblioteca está vacía.")
        color: Theme.textMuted
    }
    Repeater {
        model: root.forge.workspace.library || []
        ItemDelegate {
            required property var modelData
            Layout.fillWidth: true
            text: modelData.name + " · " + modelData.format.toUpperCase()
            onClicked: root.forge.selectModelFile(modelData.path)
            enabled: !root.forge.busy
        }
    }
}
