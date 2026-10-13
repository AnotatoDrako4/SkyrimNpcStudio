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
    Text {
        Layout.fillWidth: true
        text: qsTr("Borrador de material. No altera el visor, no genera DDS ni escribe referencias NIF exportables.")
        color: Theme.textDim
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
                text: root.material[modelData.key] ? qsTr("Referencia elegida") : qsTr("Sin textura")
                color: Theme.textMuted
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
        text: qsTr("Aplicar al NIF · motor pendiente")
        enabled: false
    }
}
