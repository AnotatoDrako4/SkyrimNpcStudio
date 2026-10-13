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
    Text {
        Layout.fillWidth: true
        text: root.modelInfo.metadataInspected ? qsTr("Metadatos glTF 2.0 leídos del archivo. Buffers, texturas y geometría todavía no han sido importados ni validados para Skyrim.") : qsTr("Se comprueba archivo, tamaño y formato admitido. La inspección completa de FBX/OBJ/BLEND necesita el bridge.")
        color: Theme.textDim
        wrapMode: Text.WordWrap
    }
    Button {
        Layout.fillWidth: true
        text: qsTr("Previsualizar · backend pendiente")
        enabled: false
    }
    Button {
        Layout.fillWidth: true
        text: qsTr("Preparar adaptación · bridge pendiente")
        enabled: false
    }
}
