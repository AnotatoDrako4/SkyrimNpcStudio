import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import SkyrimNpcStudio

ColumnLayout {
    id: root
    required property CharacterForgeViewModel forge
    enabled: forge.hasProject && !forge.busy
    readonly property var request: forge.workspace.ai || ({})
    function update(field, value) {
        let ai = Object.assign({}, request);
        ai[field] = value;
        forge.setWorkspaceValue("ai", ai);
    }
    Text {
        text: qsTr("Generación IA")
        color: Theme.text
        font.bold: true
        font.pixelSize: Theme.title
    }
    Text {
        Layout.fillWidth: true
        text: qsTr("Prepara una solicitud local. Los proveedores no están conectados; no hay consumo ni generación de modelos.")
        color: Theme.textDim
        wrapMode: Text.WordWrap
    }
    Text {
        text: qsTr("Descripción del personaje")
        color: Theme.textDim
    }
    TextArea {
        Layout.fillWidth: true
        Layout.preferredHeight: 130
        text: root.request.description || ""
        wrapMode: TextEdit.WordWrap
        enabled: root.forge.hasProject && !root.forge.busy
        onTextChanged: if (activeFocus)
            root.update("description", text)
    }
    ComboBox {
        Layout.fillWidth: true
        enabled: root.forge.hasProject && !root.forge.busy
        model: [qsTr("Sin proveedor"), "Meshy", "Tripo", "Rodin"]
        currentIndex: Math.max(0, model.indexOf(root.request.provider || ""))
        onActivated: root.update("provider", currentIndex === 0 ? "" : currentText)
    }
    Text {
        Layout.fillWidth: true
        text: qsTr("Opciones del proveedor pendientes de documentación y adaptador real. El proveedor puede requerir configuración y tener costos al habilitarlo.")
        color: Theme.textDim
        wrapMode: Text.WordWrap
    }
    Repeater {
        model: [
            {
                role: "front",
                label: qsTr("Imagen frontal")
            },
            {
                role: "side",
                label: qsTr("Imagen lateral")
            },
            {
                role: "back",
                label: qsTr("Imagen posterior")
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
                    images.role = modelData.role;
                    images.open();
                }
            }
            Text {
                Layout.fillWidth: true
                text: root.request[modelData.role] ? qsTr("Referencia %1 × %2").arg(root.request[modelData.role].width).arg(root.request[modelData.role].height) : qsTr("Sin imagen")
                color: Theme.textMuted
            }
            Button {
                visible: !!root.request[modelData.role]
                text: qsTr("Quitar referencia")
                onClicked: {
                    let ai = Object.assign({}, root.request);
                    delete ai[modelData.role];
                    root.forge.setWorkspaceValue("ai", ai);
                }
            }
        }
    }
    FileDialog {
        id: images
        property string role: "front"
        title: qsTr("Selecciona una imagen de referencia")
        nameFilters: ["Imágenes (*.png *.jpg *.jpeg *.webp)"]
        onAccepted: root.forge.selectReferenceImage(role, selectedFile.toString())
    }
    Button {
        Layout.fillWidth: true
        text: qsTr("Generar · adaptador no configurado")
        enabled: false
    }
    Text {
        Layout.fillWidth: true
        text: qsTr("Sin resultado generado. La preparación se guarda en .sncf y no contiene claves API.")
        color: Theme.textMuted
        wrapMode: Text.WordWrap
    }
}
