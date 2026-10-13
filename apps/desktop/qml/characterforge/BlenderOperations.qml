import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

ColumnLayout {
    required property CharacterForgeViewModel forge
    Repeater {
        model: [qsTr("Abrir escena"), qsTr("Sincronizar modelos"), qsTr("Importar modelo"), qsTr("Exportar intermedio"), qsTr("Verificar operación")]
        Button {
            required property var modelData
            Layout.fillWidth: true
            text: modelData
            enabled: false
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Necesita un Blender Bridge verificado. No se ejecutan scripts arbitrarios.")
        }
    }
}
