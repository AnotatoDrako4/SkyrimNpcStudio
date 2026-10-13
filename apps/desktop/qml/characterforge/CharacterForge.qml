import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import SkyrimNpcStudio

Item {
    id: root
    required property CharacterForgeViewModel forge

    FolderDialog {
        id: openForgeDialog
        title: qsTr("Selecciona una carpeta de proyecto .sncf")
        onAccepted: root.forge.openProject(selectedFolder.toString())
    }
    FolderDialog {
        id: saveForgeDialog
        title: qsTr("Selecciona donde guardar el proyecto .sncf")
        onAccepted: root.forge.saveProject(selectedFolder.toString())
    }

    FolderDialog {
        id: exportFolder
        title: qsTr("Carpeta de pruebas para el plugin técnico")
        onAccepted: root.forge.exportTechnical(selectedFolder.toString())
    }
    Dialog {
        id: technicalWarning
        parent: Overlay.overlay
        anchors.centerIn: parent
        title: qsTr("Exportación técnica — NO instalar en Skyrim")
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        Label {
            width: 420
            text: qsTr("Se verificará el ESP y se creará un paquete de pruebas. Faltan FaceGen NIF y FaceTint DDS; no es un mod listo para jugar.")
            wrapMode: Text.WordWrap
        }
        onAccepted: exportFolder.open()
    }
    SplitView {
        anchors.fill: parent
        orientation: Qt.Horizontal

        Rectangle {
            SplitView.preferredWidth: 290
            SplitView.minimumWidth: 235
            color: Theme.panel
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 12
                Text {
                    text: qsTr("CHARACTER FORGE")
                    color: Theme.text
                    font.pixelSize: Theme.title
                    font.bold: true
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Crea personajes originales. Este espacio es independiente del editor de NPC y de los atuendos.")
                    color: Theme.textDim
                    wrapMode: Text.WordWrap
                }
                Button {
                    text: qsTr("Nuevo proyecto")
                    Layout.fillWidth: true
                    onClicked: root.forge.newProject(qsTr("Nuevo personaje"))
                }
                Button {
                    text: qsTr("Abrir proyecto .sncf")
                    Layout.fillWidth: true
                    onClicked: openForgeDialog.open()
                }
                Button {
                    text: qsTr("Guardar proyecto")
                    Layout.fillWidth: true
                    enabled: root.forge.hasProject && !root.forge.busy
                    onClicked: root.forge.projectPath.length > 0 ? root.forge.saveProject(root.forge.projectPath) : saveForgeDialog.open()
                }
                RowLayout {
                    Layout.fillWidth: true
                    Button {
                        text: qsTr("Deshacer")
                        Layout.fillWidth: true
                        enabled: root.forge.canUndo && !root.forge.busy
                        onClicked: root.forge.undo()
                    }
                    Button {
                        text: qsTr("Rehacer")
                        Layout.fillWidth: true
                        enabled: root.forge.canRedo && !root.forge.busy
                        onClicked: root.forge.redo()
                    }
                }
                Rectangle {
                    height: 1
                    Layout.fillWidth: true
                    color: Theme.borderSubtle
                }
                Button {
                    Layout.fillWidth: true
                    text: qsTr("Validar NPC nuevo")
                    enabled: root.forge.hasProject && !root.forge.busy
                    onClicked: root.forge.validateNpc()
                }
                Button {
                    Layout.fillWidth: true
                    text: qsTr("Exportar plugin técnico…")
                    enabled: root.forge.canExportTechnical
                    onClicked: technicalWarning.open()
                }
                Button {
                    Layout.fillWidth: true
                    text: qsTr("Exportación final · FaceGen pendiente")
                    enabled: false
                }
                Button {
                    Layout.fillWidth: true
                    text: qsTr("Cancelar operación")
                    visible: root.forge.busy
                    onClicked: root.forge.cancelOperation()
                }
                Text {
                    text: qsTr("Nombre del plugin")
                    color: Theme.textDim
                }
                TextField {
                    Layout.fillWidth: true
                    text: root.forge.exportOptions.pluginName || "CharacterForge.esp"
                    enabled: root.forge.hasProject && !root.forge.busy
                    onEditingFinished: root.forge.setExportOption("pluginName", text)
                }
                CheckBox {
                    text: qsTr("Flag ESL")
                    checked: !!root.forge.exportOptions.eslFlag
                    enabled: root.forge.hasProject && !root.forge.busy
                    onClicked: root.forge.setExportOption("eslFlag", checked)
                }
                CheckBox {
                    text: qsTr("Crear ZIP técnico")
                    checked: root.forge.exportOptions.createZip !== false
                    enabled: root.forge.hasProject && !root.forge.busy
                    onClicked: root.forge.setExportOption("createZip", checked)
                }
                Item {
                    Layout.fillHeight: true
                }
            }
        }

        Rectangle {
            SplitView.fillWidth: true
            SplitView.minimumWidth: 330
            color: Theme.surface
            ScrollView {
                anchors.fill: parent
                anchors.margins: 24
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    spacing: 20
                    Text {
                        text: root.forge.hasProject ? root.forge.characterName || qsTr("NPC nuevo en borrador") : qsTr("Character Forge Studio")
                        color: Theme.text
                        font.pixelSize: Theme.heading
                        font.bold: true
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Nuevo personaje → Configurar → Validar → Guardar → Exportar. Selecciona un perfil e indéxalo para elegir registros; no necesitas seleccionar un NPC existente.")
                        color: Theme.textDim
                        wrapMode: Text.WordWrap
                    }
                    ForgeValidationPanel {
                        Layout.fillWidth: true
                        forge: root.forge
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Vista 3D pendiente de recursos faciales nativos compatibles. El registro puede verificarse sin una previsualización.")
                        color: Theme.textMuted
                        wrapMode: Text.WordWrap
                    }
                    TextField {
                        Layout.fillWidth: true
                        enabled: root.forge.hasProject && !root.forge.busy
                        text: root.forge.projectName
                        placeholderText: qsTr("Nombre del proyecto")
                        onEditingFinished: root.forge.setProjectName(text)
                    }
                    TextField {
                        Layout.fillWidth: true
                        enabled: root.forge.hasProject && !root.forge.busy
                        text: root.forge.description
                        placeholderText: qsTr("Descripción interna del proyecto")
                        onEditingFinished: root.forge.setDescription(text)
                    }
                    Text {
                        Layout.fillWidth: true
                        text: root.forge.projectPath || qsTr("Sin guardar")
                        color: Theme.textMuted
                        wrapMode: Text.WrapAnywhere
                    }
                }
            }
        }

        Rectangle {
            SplitView.preferredWidth: 440
            SplitView.minimumWidth: 400
            color: Theme.panel
            NativeNpcEditor {
                anchors.fill: parent
                anchors.margins: 14
                forge: root.forge
            }
        }
    }
}
