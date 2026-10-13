import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

ColumnLayout {
    id: root
    required property CharacterForgeViewModel forge
    property string currentTool: forge.workspace.tool || "project"
    signal toolSelected(string tool)
    Text {
        text: "CHARACTER FORGE"
        color: Theme.text
        font.bold: true
        font.pixelSize: Theme.title
    }
    Text {
        Layout.fillWidth: true
        text: root.forge.projectName || qsTr("Sin proyecto")
        color: Theme.textDim
        wrapMode: Text.WordWrap
    }
    Repeater {
        model: [
            {
                key: "project",
                label: qsTr("Proyecto")
            },
            {
                key: "native",
                label: qsTr("Crear NPC")
            },
            {
                key: "import",
                label: qsTr("Importar personaje")
            },
            {
                key: "library",
                label: qsTr("Biblioteca de modelos")
            },
            {
                key: "ai",
                label: qsTr("Generar con IA")
            },
            {
                key: "blender",
                label: qsTr("Blender y MCP")
            },
            {
                key: "outfit",
                label: qsTr("Atuendos e inventario")
            },
            {
                key: "skeleton",
                label: qsTr("Esqueleto")
            },
            {
                key: "materials",
                label: qsTr("Materiales")
            },
            {
                key: "validation",
                label: qsTr("Validación")
            },
            {
                key: "export",
                label: qsTr("Exportar")
            }
        ]
        ItemDelegate {
            required property var modelData
            Layout.fillWidth: true
            text: modelData.label
            highlighted: root.currentTool === modelData.key
            onClicked: {
                root.toolSelected(modelData.key);
                if (root.forge.hasProject)
                    root.forge.setWorkspaceValue("tool", modelData.key);
            }
        }
    }
    Item {
        Layout.fillHeight: true
    }
    Text {
        Layout.fillWidth: true
        text: root.forge.dirty ? qsTr("Cambios sin guardar") : qsTr("Borrador independiente .sncf")
        color: Theme.textMuted
        wrapMode: Text.WordWrap
    }
}
