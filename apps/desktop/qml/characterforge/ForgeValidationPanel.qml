import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

ColumnLayout {
    id: root
    required property CharacterForgeViewModel forge
    Text {
        Layout.fillWidth: true
        text: root.forge.busy ? qsTr("Operación en curso…") : root.forge.validation.technicallyExportable ? qsTr("Registros validados · FaceGen pendiente") : qsTr("Borrador · valida antes de exportar")
        color: Theme.text
        font.bold: true
        wrapMode: Text.WordWrap
    }
    Text {
        Layout.fillWidth: true
        text: qsTr("La exportación final está bloqueada: faltan FaceGen NIF y FaceTint DDS generados y verificados para este personaje. Un plugin técnico no está listo para jugar.")
        color: Theme.textDim
        wrapMode: Text.WordWrap
    }
    Repeater {
        model: root.forge.validation.issues || []
        Text {
            required property var modelData
            Layout.fillWidth: true
            text: modelData.message
            color: modelData.severity === "error" ? Theme.error : Theme.textDim
            wrapMode: Text.WordWrap
        }
    }
    Text {
        visible: root.forge.lastError.length > 0
        Layout.fillWidth: true
        text: root.forge.lastError
        color: Theme.textDim
        wrapMode: Text.WrapAnywhere
    }
}
