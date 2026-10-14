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
        text: qsTr("Los modelos externos son enlaces locales. Los modelos copiados viven dentro de la carpeta .sncf y conservan sus recursos glTF validados.")
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
            required property int index
            required property var modelData
            Layout.fillWidth: true
            text: modelData.name + " · " + modelData.format.toUpperCase() + " · " +
                  (modelData.storage === "managed" ? qsTr("en proyecto") : qsTr("enlace externo")) +
                  (modelData.available ? "" : qsTr(" · falta el archivo"))
            onClicked: root.forge.selectLibraryModel(index)
            enabled: !root.forge.busy
        }
    }
}
