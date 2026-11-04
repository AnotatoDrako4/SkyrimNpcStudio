import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import SkyrimNpcStudio

ColumnLayout {
    id: root
    required property CharacterForgeViewModel forge
    enabled: forge.hasProject && !forge.busy
    readonly property var material: forge.workspace.materials || ({})
    readonly property var inspection: forge.scene.inspection || ({})
    readonly property var gltfMaterials: inspection.materials || []
    readonly property var gltfImages: inspection.images || []
    readonly property var skyrimTextures: ((forge.skyrimConversion || ({})).prepare || ({})).textures || []

    function update(field, value) {
        let next = Object.assign({}, material);
        next[field] = value;
        forge.setWorkspaceValue("materials", next);
    }

    Text {
        text: qsTr("Materiales")
        color: Theme.text
        font.bold: true
        font.pixelSize: Theme.title
    }

    // Real inspected materials from the loaded GLB/BLEND preview
    Text {
        Layout.fillWidth: true
        text: qsTr("Materiales inspeccionados del modelo (%1)").arg(root.gltfMaterials.length)
        color: Theme.text
        font.bold: true
    }
    Text {
        Layout.fillWidth: true
        visible: root.gltfMaterials.length === 0
        text: root.forge.scene.hasModel
            ? qsTr("El modelo cargado no define materiales glTF.")
            : qsTr("Carga o convierte un modelo para inspeccionar sus materiales reales.")
        color: Theme.textMuted
        font.pixelSize: Theme.small
        wrapMode: Text.WordWrap
    }
    Repeater {
        model: root.gltfMaterials
        delegate: Rectangle {
            required property var modelData
            required property int index
            Layout.fillWidth: true
            implicitHeight: matCardCol.implicitHeight + Theme.spacingSm * 2
            radius: Theme.radius
            color: Theme.panelAlt
            border.color: Theme.borderSubtle
            ColumnLayout {
                id: matCardCol
                anchors.fill: parent
                anchors.margins: Theme.spacingSm
                spacing: 2
                Text {
                    Layout.fillWidth: true
                    color: Theme.text
                    font.bold: true
                    font.pixelSize: Theme.small
                    text: (index + 1) + ". " + (modelData.name || qsTr("Material sin nombre"))
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textDim
                    font.pixelSize: Theme.small
                    text: qsTr("Modo alfa: %1 · Doble cara: %2")
                        .arg(modelData.alphaMode || "OPAQUE")
                        .arg(modelData.doubleSided ? qsTr("sí") : qsTr("no"))
                }
            }
        }
    }

    Text {
        Layout.fillWidth: true
        visible: root.gltfImages.length > 0
        text: qsTr("Texturas / imágenes detectadas (%1)").arg(root.gltfImages.length)
        color: Theme.text
        font.bold: true
        font.pixelSize: Theme.small
    }
    Repeater {
        model: root.gltfImages
        delegate: Text {
            required property var modelData
            Layout.fillWidth: true
            elide: Text.ElideMiddle
            color: Theme.textDim
            font.pixelSize: Theme.small
            text: "• " + (modelData.name || modelData.uri || modelData.mimeType || qsTr("Textura embebida"))
        }
    }

    Text {
        Layout.fillWidth: true
        visible: root.skyrimTextures.length > 0
        text: qsTr("Texturas preparadas para Skyrim SE (%1)").arg(root.skyrimTextures.length)
        color: Theme.ok
        font.bold: true
        font.pixelSize: Theme.small
    }
    Repeater {
        model: root.skyrimTextures
        delegate: Text {
            required property var modelData
            Layout.fillWidth: true
            elide: Text.ElideMiddle
            color: Theme.textDim
            font.pixelSize: Theme.small
            text: "✓ " + (modelData.staged || modelData.source || "") + (modelData.compression ? " (" + modelData.compression + ")" : "")
        }
    }

    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border; Layout.topMargin: 4; Layout.bottomMargin: 4 }

    Text {
        Layout.fillWidth: true
        text: qsTr("Borrador de variante de material (.sncf). La conversión automática de texturas a DDS BC1/BC3 se realiza en «Convertir a Skyrim».")
        color: Theme.textDim
        font.pixelSize: Theme.small
        wrapMode: Text.WordWrap
    }
    TextField {
        Layout.fillWidth: true
        text: root.material.name || ""
        placeholderText: qsTr("Nombre de variante")
        enabled: root.forge.hasProject && !root.forge.busy
        onEditingFinished: root.update("name", text)
    }
    Repeater {
        model: [
            {
                key: "diffuse",
                label: qsTr("Diffuse")
            },
            {
                key: "normal",
                label: qsTr("Normal map")
            },
            {
                key: "alpha",
                label: qsTr("Mapa alfa")
            }
        ]
        ColumnLayout {
            required property var modelData
            Layout.fillWidth: true
            Button {
                Layout.fillWidth: true
                text: modelData.label + "…"
                enabled: root.forge.hasProject && !root.forge.busy
                onClicked: {
                    texture.field = modelData.key;
                    texture.open();
                }
            }
            Text {
                Layout.fillWidth: true
                text: root.material[modelData.key] ? qsTr("Referencia elegida: %1").arg(root.material[modelData.key]) : qsTr("Sin textura")
                color: Theme.textMuted
                elide: Text.ElideMiddle
            }
        }
    }
    FileDialog {
        id: texture
        property string field: "diffuse"
        title: qsTr("Selecciona una referencia de textura")
        nameFilters: ["Texturas (*.dds *.png *.tga)"]
        onAccepted: root.update(field, selectedFile.toString())
    }
    Text {
        text: qsTr("Opacidad del borrador")
        color: Theme.textDim
    }
    Slider {
        Layout.fillWidth: true
        from: 0
        to: 1
        value: root.material.opacity === undefined ? 1 : root.material.opacity
        enabled: root.forge.hasProject && !root.forge.busy
        onPressedChanged: if (!pressed)
            root.update("opacity", value)
    }
    Button {
        Layout.fillWidth: true
        text: qsTr("Aplicar al NIF · motor de edición de shaders en vivo pendiente")
        enabled: false
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Contrato faltante: edición interactiva de BSLightingShaderProperty sobre un NIF ya exportado. Usa «Convertir a Skyrim» para generar el NIF con sus DDS.")
    }
}
