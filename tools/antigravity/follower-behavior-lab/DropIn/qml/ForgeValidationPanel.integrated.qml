import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

// ForgeValidationPanel.integrated.qml
// Character Forge validation inspector integrated with Follower Behavior & Voice Intelligence Studio.
// Retains NativeFaceGenUnavailable export lock while presenting real voice evidence and AI behavior diagnostics.
ColumnLayout {
    id: root
    required property CharacterForgeViewModel forge
    readonly property var result: forge.validation
    readonly property var voiceRes: (root.result && root.result.voiceAnalysis) ? root.result.voiceAnalysis : null

    Text {
        text: qsTr("Validación de Personaje y Comportamiento")
        color: Theme.text
        font.pixelSize: Theme.title
        font.bold: true
    }

    Text {
        Layout.fillWidth: true
        text: root.forge.busy
            ? qsTr("Operación en curso…")
            : root.result.technicallyExportable
                ? qsTr("Registros y enlaces validados · FaceGen pendiente")
                : qsTr("Borrador con incidencias · valida antes de continuar")
        color: Theme.text
        wrapMode: Text.WordWrap
    }

    Repeater {
        model: [
            {
                label: qsTr("Modelo"),
                status: root.forge.workspace.model ? (root.forge.workspace.model.metadataInspected ? qsTr("Metadatos inspeccionados") : qsTr("Seleccionado, no inspeccionado")) : qsTr("No comprobado")
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
                status: qsTr("Pendiente de generación nativa (NIF/DDS)")
            },
            {
                label: qsTr("Voz y Diálogo de Seguidor"),
                status: root.voiceRes
                    ? qsTr("%1 · %2").arg(root.voiceRes.followerDialogueClassification).arg(root.voiceRes.voiceEditorId || root.voiceRes.voiceFormKey)
                    : qsTr("Pendiente de validación")
            },
            {
                label: qsTr("Evidencia de Matrimonio"),
                status: root.voiceRes
                    ? qsTr("%1 · %2").arg(root.voiceRes.marriageDialogueClassification).arg(root.voiceRes.hasStaticMarriageListEvidence ? qsTr("En lista VoicesMarriage") : qsTr("Sin evidencia en lista"))
                    : qsTr("Pendiente de validación")
            },
            {
                label: qsTr("Equipamiento"),
                status: root.result.outfit ? (root.result.outfit.exportable ? qsTr("Referencias y slots comprobados") : qsTr("Errores de compatibilidad")) : qsTr("No comprobado")
            },
            {
                label: qsTr("Comportamiento y Paquetes IA"),
                status: qsTr("REQUIRES_IN_GAME_TEST · Las condiciones de misión y diálogo exigen verificación en Skyrim SE 1.6.1170.")
            },
            {
                label: qsTr("Dependencias"),
                status: root.result.masters ? qsTr("%1 masters referenciados").arg(root.result.masters.length) : qsTr("No comprobado")
            },
            {
                label: qsTr("Exportación final"),
                status: qsTr("Bloqueada por FaceGen nativo (NativeFaceGenUnavailable)")
            }
        ]

        ColumnLayout {
            required property var modelData
            Layout.fillWidth: true
            spacing: 2

            Text {
                text: modelData.label
                color: Theme.text
                font.bold: true
            }
            Text {
                Layout.fillWidth: true
                text: modelData.status
                color: modelData.label.indexOf(qsTr("Exportación final")) >= 0
                    ? Theme.error
                    : (modelData.status.indexOf("REQUIRES_IN_GAME_TEST") >= 0 ? Theme.warn : Theme.textDim)
                wrapMode: Text.WordWrap
            }
        }
    }

    Text {
        Layout.fillWidth: true
        text: qsTr("Aviso de seguridad: la compatibilidad estática con listas FLST no equivale a 'VERIFICADO_EN_SKYRIM'. Las condiciones de DialogueFollower y RelationshipMarriage se deben probar en ejecución.")
        color: Theme.warn
        font.pixelSize: 11
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
        text: qsTr("Mostrar detalle de advertencias y errores")
    }

    Repeater {
        model: details.checked ? (root.result.issues || []) : []
        Text {
            required property var modelData
            Layout.fillWidth: true
            text: (modelData.severity ? "[" + modelData.severity.toUpperCase() + "] " : "") + (modelData.code ? modelData.code + ": " : "") + modelData.message
            color: (modelData.severity === "error" || modelData.severity === "ERROR") ? Theme.error : Theme.textDim
            wrapMode: Text.WordWrap
        }
    }
}
