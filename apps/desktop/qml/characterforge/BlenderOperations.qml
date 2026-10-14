import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

ColumnLayout {
    id: root
    required property CharacterForgeViewModel forge
    readonly property var modelInfo: forge.workspace.model || ({})
    Button {
        Layout.fillWidth: true
        text: qsTr("Convertir modelo seleccionado a GLB")
        enabled: forge.hasProject && forge.projectPath.length > 0 && !forge.busy &&
                 !!forge.workspace.blender && modelInfo.storage !== "managed" &&
                 (modelInfo.format === "fbx" || modelInfo.format === "obj" || modelInfo.format === "blend")
        onClicked: forge.convertSelectedModelWithBlender()
    }
    Button {
        Layout.fillWidth: true
        text: qsTr("Cancelar conversión")
        enabled: forge.blenderBusy
        onClicked: forge.cancelOperation()
    }
    Repeater {
        model: [qsTr("Abrir escena"), qsTr("Sincronizar modelos"), qsTr("Conectar MCP")]
        Button {
            required property var modelData
            Layout.fillWidth: true
            text: modelData
            enabled: false
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Esta operación todavía no está implementada.")
        }
    }
}
