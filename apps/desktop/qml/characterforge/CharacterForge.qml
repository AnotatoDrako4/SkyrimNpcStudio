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
                Text { text: qsTr("CHARACTER FORGE"); color: Theme.text; font.pixelSize: Theme.title; font.bold: true }
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
                    enabled: root.forge.hasProject
                    onClicked: root.forge.projectPath.length > 0
                               ? root.forge.saveProject(root.forge.projectPath)
                               : saveForgeDialog.open()
                }
                RowLayout {
                    Layout.fillWidth: true
                    Button {
                        text: qsTr("Deshacer")
                        Layout.fillWidth: true
                        enabled: root.forge.canUndo
                        onClicked: root.forge.undo()
                    }
                    Button {
                        text: qsTr("Rehacer")
                        Layout.fillWidth: true
                        enabled: root.forge.canRedo
                        onClicked: root.forge.redo()
                    }
                }
                Rectangle { height: 1; Layout.fillWidth: true; color: Theme.borderSubtle }
                Text { text: qsTr("Próximas etapas"); color: Theme.text; font.bold: true }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("NPC nuevo: Fase 2\nBlender y MCP: Fase 3\nModelos externos: Fase 4\nGeneración IA: Fase 6")
                    color: Theme.textDim
                    wrapMode: Text.WordWrap
                }
                Item { Layout.fillHeight: true }
            }
        }

        Rectangle {
            SplitView.fillWidth: true
            SplitView.minimumWidth: 330
            color: Theme.surface
            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(parent.width - 40, 520)
                spacing: 16
                Text {
                    Layout.fillWidth: true
                    text: root.forge.hasProject ? qsTr("Proyecto de creación preparado") : qsTr("Character Forge Studio")
                    color: Theme.text
                    font.pixelSize: Theme.heading
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                }
                Text {
                    Layout.fillWidth: true
                    text: root.forge.hasProject
                          ? qsTr("Puedes editar la identidad, deshacer cambios y guardar el proyecto .sncf. La creación 3D y la exportación de NPC nuevos se incorporarán en fases posteriores.")
                          : qsTr("Crea un proyecto vacío o abre un .sncf. No necesitas seleccionar un NPC existente.")
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                    color: Theme.textDim
                }
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 55
                    radius: Theme.radiusLarge
                    color: Theme.panel
                    border.color: Theme.border
                    Text {
                        anchors.centerIn: parent
                        text: qsTr("Visor 3D de modelos importados — Fase 4")
                        color: Theme.textMuted
                    }
                }
            }
        }

        Rectangle {
            SplitView.preferredWidth: 300
            SplitView.minimumWidth: 250
            color: Theme.panel
            ScrollView {
                anchors.fill: parent
                anchors.margins: 14
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    spacing: 12
                    Text { text: qsTr("Propiedades del proyecto"); color: Theme.text; font.bold: true; font.pixelSize: Theme.large }
                    Text { text: qsTr("Nombre del proyecto"); color: Theme.textDim }
                    TextField {
                        Layout.fillWidth: true
                        enabled: root.forge.hasProject
                        text: root.forge.projectName
                        onEditingFinished: root.forge.setProjectName(text)
                    }
                    Text { text: qsTr("Nombre del personaje"); color: Theme.textDim }
                    TextField {
                        Layout.fillWidth: true
                        enabled: root.forge.hasProject
                        text: root.forge.characterName
                        onEditingFinished: root.forge.setCharacterName(text)
                    }
                    Text { text: qsTr("EditorID (borrador)"); color: Theme.textDim }
                    TextField {
                        Layout.fillWidth: true
                        enabled: root.forge.hasProject
                        text: root.forge.editorId
                        placeholderText: "SNS_MiPersonaje"
                        onEditingFinished: root.forge.setEditorId(text)
                    }
                    Text { text: qsTr("Descripción"); color: Theme.textDim }
                    TextField {
                        Layout.fillWidth: true
                        enabled: root.forge.hasProject
                        text: root.forge.description
                        onEditingFinished: root.forge.setDescription(text)
                    }
                    Text {
                        Layout.fillWidth: true
                        text: root.forge.projectPath.length > 0 ? root.forge.projectPath : qsTr("Sin guardar")
                        color: Theme.textMuted
                        wrapMode: Text.WrapAnywhere
                    }
                    Text {
                        visible: root.forge.lastError.length > 0
                        Layout.fillWidth: true
                        text: root.forge.lastError
                        color: Theme.error
                        wrapMode: Text.WordWrap
                    }
                    Rectangle { height: 1; Layout.fillWidth: true; color: Theme.borderSubtle }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Exportar un NPC nuevo se habilitará cuando el worker pueda escribir y verificar registros NPC_ y FaceGen reales (Fase 2).")
                        color: Theme.textDim
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
    }
}
