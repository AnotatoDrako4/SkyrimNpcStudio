import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

ColumnLayout {
    id: root
    required property CharacterForgeViewModel forge
    readonly property var result: forge.validation
    Text {
        text: qsTr("Validación")
        color: Theme.text
        font.pixelSize: Theme.title
        font.bold: true
    }
    Text {
        Layout.fillWidth: true
        text: root.forge.busy ? qsTr("Operación en curso…") : root.result.technicallyExportable ? qsTr("Registros validados · FaceGen pendiente") : qsTr("Borrador · valida antes de exportar")
        color: Theme.text
        wrapMode: Text.WordWrap
    }
    Repeater {
        model: [
            {
                label: qsTr("Modelo"),
                status: root.forge.workspace.model ? root.forge.workspace.model.metadataInspected ? qsTr("Metadatos inspeccionados") : qsTr("Seleccionado, no inspeccionado") : qsTr("No comprobado")
            },
            {
                label: qsTr("Materiales"),
                status: qsTr("Borrador, no exportable")
            },
            {
                label: qsTr("Esqueleto"),
                status: qsTr("No inspeccionado")
            },
            {
                label: qsTr("FaceGen"),
                status: qsTr("Pendiente")
            },
            {
                label: qsTr("Equipamiento"),
                status: root.result.outfit ? root.result.outfit.exportable ? qsTr("Referencias y slots comprobados") : qsTr("Errores de compatibilidad") : qsTr("No comprobado")
            },
            {
                label: qsTr("Dependencias"),
                status: root.result.masters ? qsTr("%1 masters referenciados").arg(root.result.masters.length) : qsTr("No comprobado")
            },
            {
                label: qsTr("Exportación final"),
                status: qsTr("Bloqueada por FaceGen")
            }
        ]
        ColumnLayout {
            required property var modelData
            Layout.fillWidth: true
            Text {
                text: modelData.label
                color: Theme.text
                font.bold: true
            }
            Text {
                Layout.fillWidth: true
                text: modelData.status
                color: Theme.textDim
                wrapMode: Text.WordWrap
            }
        }
    }
    Text {
        Layout.fillWidth: true
        text: qsTr("La generación facial y los recursos reales requieren validación local. Un plugin técnico no está listo para jugar.")
        color: Theme.warn
        wrapMode: Text.WordWrap
    }
    Text {
        Layout.fillWidth: true
        visible: (root.result.missingReferences || []).length > 0
        text: qsTr("Hay referencias ausentes o del tipo incorrecto. Revisa los selectores del NPC.")
        color: Theme.error
        wrapMode: Text.WordWrap
    }
    CheckBox {
        id: details
        text: qsTr("Mostrar errores y advertencias")
    }
    Repeater {
        model: details.checked ? root.result.issues || [] : []
        Text {
            required property var modelData
            Layout.fillWidth: true
            text: modelData.message
            color: modelData.severity === "error" ? Theme.error : Theme.textDim
            wrapMode: Text.WordWrap
        }
    }
}
