import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import SkyrimNpcStudio

ColumnLayout {
    id: root
    required property CharacterForgeViewModel forge
    readonly property var modelInfo: forge.workspace.model || ({})
    Text {
        text: qsTr("Importar personaje")
        color: Theme.text
        font.bold: true
        font.pixelSize: Theme.title
    }
    Text {
        Layout.fillWidth: true
        text: qsTr("Seleccionar → Inspeccionar → Previsualizar → Adaptar")
        color: Theme.textDim
        wrapMode: Text.WordWrap
    }
    Button {
        Layout.fillWidth: true
        text: qsTr("Seleccionar archivo 3D…")
        enabled: root.forge.hasProject && !root.forge.busy
        onClicked: filePicker.open()
    }
    FileDialog {
        id: filePicker
        title: qsTr("Selecciona un modelo")
        nameFilters: ["Modelos 3D (*.glb *.gltf *.fbx *.obj *.blend)"]
        onAccepted: root.forge.selectModelFile(selectedFile.toString())
    }
    Text {
        Layout.fillWidth: true
        text: root.modelInfo.name || qsTr("Sin archivo seleccionado")
        color: Theme.text
        wrapMode: Text.WordWrap
    }
    Text {
        text: root.modelInfo.format ? qsTr("Formato: %1 · %2 MiB").arg(root.modelInfo.format.toUpperCase()).arg((root.modelInfo.sizeBytes / 1048576).toFixed(2)) : ""
        color: Theme.textDim
    }
    Repeater {
        model: [
            {
                key: "meshes",
                label: qsTr("Mallas")
            },
            {
                key: "materials",
                label: qsTr("Materiales")
            },
            {
                key: "textures",
                label: qsTr("Texturas")
            },
            {
                key: "skins",
                label: qsTr("Skins / armatures")
            }
        ]
        Text {
            required property var modelData
            text: modelData.label + ": " + (root.modelInfo[modelData.key] === undefined ? qsTr("No inspeccionado") : root.modelInfo[modelData.key])
            color: Theme.textDim
        }
    }
    // Three levels, never mixed: declared (JSON), validated (data checked), rendered (viewport).
    readonly property var scene: root.forge.scene
    readonly property var inspection: scene.inspection
    Text {
        Layout.fillWidth: true
        Layout.topMargin: 6
        text: qsTr("Vista previa")
        color: Theme.text
        font.bold: true
    }
    Text {
        Layout.fillWidth: true
        visible: root.inspection.validated !== undefined
        text: root.inspection.validated === undefined ? "" : qsTr("Validado: %1 vértices · %2 triángulos · índices %3 · %4 recurso(s) externo(s)")
            .arg(root.inspection.validated.vertices).arg(root.inspection.validated.triangles)
            .arg(root.inspection.validated.indicesChecked ? qsTr("comprobados") : qsTr("sin índices"))
            .arg(root.inspection.resources.length)
        color: Theme.textDim
        wrapMode: Text.WordWrap
    }
    Text {
        Layout.fillWidth: true
        visible: root.inspection.declared !== undefined
        text: root.inspection.declared === undefined ? "" : qsTr("Declarado: %1 nodos · %2 mallas · %3 materiales · %4 texturas · %5 huesos · %6 animaciones · %7 morph targets")
            .arg(root.inspection.declared.nodes).arg(root.inspection.declared.meshes).arg(root.inspection.declared.materials)
            .arg(root.inspection.declared.textures).arg(root.inspection.declared.joints).arg(root.inspection.declared.animations)
            .arg(root.inspection.declared.morphTargets)
        color: Theme.textDim
        wrapMode: Text.WordWrap
    }
    Text {
        Layout.fillWidth: true
        text: root.scene.hasModel ? qsTr("Renderizado en el visor (Qt Quick 3D RuntimeLoader).")
            : root.scene.message.length ? root.scene.message
            : qsTr("Las vistas previas admiten GLB y glTF 2.0; FBX/OBJ/BLEND necesitan el puente de Blender.")
        color: root.scene.state === "error" ? Theme.warn : Theme.textDim
        wrapMode: Text.WordWrap
    }
    Repeater {
        model: (root.inspection.skins || []).length ? root.inspection.skins : []
        Text {
            required property var modelData
            Layout.fillWidth: true
            text: qsTr("Armature %1: %2").arg(modelData.name || qsTr("(sin nombre)")).arg(modelData.joints.join(", "))
            color: Theme.textMuted
            wrapMode: Text.WordWrap
        }
    }
    Button {
        Layout.fillWidth: true
        text: qsTr("Recargar vista previa")
        enabled: root.forge.hasProject && root.scene.filePath.length > 0 && !root.scene.busy
        onClicked: root.scene.reload()
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Vuelve a validar y cargar el archivo (por ejemplo, después de editarlo fuera del estudio).")
    }
    Button {
        Layout.fillWidth: true
        text: qsTr("Preparar adaptación · bridge pendiente")
        enabled: false
    }
}
