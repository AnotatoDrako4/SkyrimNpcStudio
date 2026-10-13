import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import SkyrimNpcStudio

ColumnLayout {
    id: root
    required property CharacterForgeViewModel forge
    readonly property int step: (forge.workspace.exportWizard || {}).step || 0
    readonly property string outputFolder: (forge.workspace.exportWizard || {}).outputFolder || ""
    function updateWizard(field, value) {
        let w = Object.assign({}, forge.workspace.exportWizard || {});
        w[field] = value;
        forge.setWorkspaceValue("exportWizard", w);
    }
    property bool technicalAccepted: false
    readonly property var titles: [qsTr("Resumen"), qsTr("Recursos y dependencias"), qsTr("Compatibilidad"), qsTr("Destino"), qsTr("Exportar")]
    Text {
        text: qsTr("Exportación · %1 de 5").arg(root.step + 1)
        color: Theme.text
        font.bold: true
        font.pixelSize: Theme.title
    }
    Text {
        text: root.titles[root.step]
        color: Theme.accent
        font.bold: true
    }
    ColumnLayout {
        visible: root.step === 0
        Text {
            text: root.forge.characterName || qsTr("NPC sin nombre")
            color: Theme.text
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("NPC original con identidad propia. Ningún NPC existente se usará como base de exportación.")
            color: Theme.textDim
            wrapMode: Text.WordWrap
        }
    }
    ColumnLayout {
        visible: root.step === 1
        Text {
            Layout.fillWidth: true
            text: qsTr("Solo referencias externas a los masters. No se copian mallas o texturas de mods. FaceGen nativo pendiente.")
            color: Theme.textDim
            wrapMode: Text.WordWrap
        }
        Repeater {
            model: root.forge.validation.masters || []
            Text {
                required property var modelData
                text: modelData
                color: Theme.textDim
            }
        }
    }
    ColumnLayout {
        visible: root.step === 2
        Button {
            text: qsTr("Validar ahora")
            enabled: root.forge.catalogReady && root.forge.hasProject && !root.forge.busy
            onClicked: root.forge.validateNpc()
        }
        ForgeValidationPanel {
            Layout.fillWidth: true
            forge: root.forge
        }
    }
    ColumnLayout {
        visible: root.step === 3
        TextField {
            Layout.fillWidth: true
            text: root.forge.exportOptions.pluginName || "CharacterForge.esp"
            placeholderText: qsTr("Nombre.esp")
            enabled: root.forge.hasProject && !root.forge.busy
            onEditingFinished: root.forge.setExportOption("pluginName", text)
        }
        CheckBox {
            text: qsTr("Flag ESL")
            checked: !!root.forge.exportOptions.eslFlag
            onClicked: root.forge.setExportOption("eslFlag", checked)
        }
        CheckBox {
            text: qsTr("ZIP técnico")
            checked: root.forge.exportOptions.createZip !== false
            onClicked: root.forge.setExportOption("createZip", checked)
        }
        Button {
            text: qsTr("Seleccionar carpeta…")
            onClicked: destination.open()
        }
        Text {
            Layout.fillWidth: true
            text: root.outputFolder || qsTr("Sin destino seleccionado")
            color: Theme.textDim
            elide: Text.ElideMiddle
        }
    }
    ColumnLayout {
        visible: root.step === 4
        Text {
            Layout.fillWidth: true
            text: qsTr("Exportación final bloqueada: FaceGen no certificado. Puedes producir un paquete técnico para inspeccionar registros; NO instalarlo en Skyrim.")
            color: Theme.warn
            wrapMode: Text.WordWrap
        }
        CheckBox {
            Layout.fillWidth: true
            text: qsTr("Entiendo que el plugin técnico no está listo para jugar")
            checked: root.technicalAccepted
            onClicked: root.technicalAccepted = checked
        }
        Button {
            Layout.fillWidth: true
            text: qsTr("Exportar plugin técnico")
            enabled: root.technicalAccepted && root.outputFolder.length > 0 && root.forge.canExportTechnical
            onClicked: root.forge.exportTechnical(root.outputFolder)
        }
        Button {
            Layout.fillWidth: true
            text: qsTr("Exportación final · FaceGen pendiente")
            enabled: false
        }
        Text {
            Layout.fillWidth: true
            text: root.forge.lastError
            color: Theme.textDim
            wrapMode: Text.WrapAnywhere
        }
        Button {
            text: qsTr("Revalidar datos")
            enabled: root.forge.catalogReady && !root.forge.busy
            onClicked: root.forge.validateNpc()
        }
    }
    RowLayout {
        Button {
            text: qsTr("Anterior")
            enabled: root.step > 0 && !root.forge.busy
            onClicked: root.updateWizard("step", root.step - 1)
        }
        Item {
            Layout.fillWidth: true
        }
        Button {
            text: qsTr("Siguiente")
            enabled: root.step < 4 && !root.forge.busy
            onClicked: root.updateWizard("step", root.step + 1)
        }
    }
    FolderDialog {
        id: destination
        title: qsTr("Destino de pruebas, fuera de Skyrim y MO2")
        onAccepted: root.updateWizard("outputFolder", selectedFolder.toString())
    }
}
