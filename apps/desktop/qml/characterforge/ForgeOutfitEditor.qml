import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

ColumnLayout {
    id: root
    required property CharacterForgeViewModel forge
    Text {
        text: qsTr("Atuendo inicial e inventario")
        color: Theme.text
        font.bold: true
        font.pixelSize: Theme.title
    }
    CharacterInventory {
        Layout.fillWidth: true
        forge: root.forge
    }
    Text {
        Layout.fillWidth: true
        text: qsTr("SMP/FSMP permanece en los recursos externos. Aquí no se simula física ni se redistribuyen mallas de otros autores.")
        color: Theme.textMuted
        wrapMode: Text.WordWrap
    }
}
