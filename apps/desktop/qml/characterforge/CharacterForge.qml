import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import SkyrimNpcStudio

Item {
    id: root
    required property CharacterForgeViewModel forge
    property string toolWithoutProject: "project"
    readonly property string currentTool: forge.hasProject ? forge.workspace.tool || "project" : toolWithoutProject
    function save() {
        if (forge.projectPath.length)
            forge.saveProject(forge.projectPath);
        else
            saveDialog.open();
    }
    FolderDialog {
        id: openDialog
        title: qsTr("Selecciona un proyecto .sncf")
        onAccepted: root.forge.openProject(selectedFolder.toString())
    }
    FolderDialog {
        id: saveDialog
        title: qsTr("Guardar proyecto .sncf")
        onAccepted: root.forge.saveProject(selectedFolder.toString())
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: 1
        ForgeToolbar {
            Layout.fillWidth: true
            forge: root.forge
            onOpenRequested: openDialog.open()
            onSaveRequested: root.save()
        }
        SplitView {
            id: panels
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal
            Rectangle {
                SplitView.preferredWidth: panels.width * 0.20
                SplitView.minimumWidth: 210
                color: Theme.panel
                ScrollView {
                    anchors.fill: parent
                    anchors.margins: 12
                    contentWidth: availableWidth
                    ForgeNavigation {
                        width: parent.width
                        forge: root.forge
                        currentTool: root.currentTool
                        onToolSelected: tool => {
                            if (!root.forge.hasProject)
                                root.toolWithoutProject = tool;
                        }
                    }
                }
            }
            ForgeViewport {
                SplitView.fillWidth: true
                SplitView.minimumWidth: 380
                forge: root.forge
            }
            Rectangle {
                SplitView.preferredWidth: panels.width * 0.25
                SplitView.minimumWidth: 280
                color: Theme.panel
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8
                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        contentWidth: availableWidth
                        Loader {
                            id: contextPanel
                            width: parent.width
                            sourceComponent: {
                                switch (root.currentTool) {
                                case "native":
                                    return nativePanel;
                                case "import":
                                    return importPanel;
                                case "library":
                                    return libraryPanel;
                                case "blender":
                                    return blenderPanel;
                                case "ai":
                                    return aiPanel;
                                case "outfit":
                                    return outfitPanel;
                                case "skeleton":
                                    return skeletonPanel;
                                case "materials":
                                    return materialsPanel;
                                case "validation":
                                    return validationPanel;
                                case "export":
                                    return exportPanel;
                                default:
                                    return projectPanel;
                                }
                            }
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: root.forge.lastError.length > 0 && root.currentTool !== "export"
                        text: root.forge.lastError
                        color: Theme.warn
                        wrapMode: Text.WordWrap
                    }
                    CheckBox {
                        id: advanced
                        text: qsTr("Inspector avanzado")
                    }
                    ScrollView {
                        visible: advanced.checked
                        Layout.fillWidth: true
                        Layout.preferredHeight: 220
                        contentWidth: availableWidth
                        ForgeAdvancedInspector {
                            width: parent.width
                            forge: root.forge
                        }
                    }
                }
            }
        }
    }
    Component {
        id: projectPanel
        ColumnLayout {
            Text {
                text: qsTr("Proyecto de creación")
                color: Theme.text
                font.pixelSize: Theme.title
                font.bold: true
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("Crea y guarda un personaje original. El proyecto, sus archivos de referencia y su historial son independientes de los otros workspaces.")
                color: Theme.textDim
                wrapMode: Text.WordWrap
            }
            TextField {
                Layout.fillWidth: true
                text: root.forge.projectName
                placeholderText: qsTr("Nombre del proyecto")
                enabled: root.forge.hasProject && !root.forge.busy
                onEditingFinished: root.forge.setProjectName(text)
            }
            TextArea {
                Layout.fillWidth: true
                Layout.preferredHeight: 100
                text: root.forge.description
                placeholderText: qsTr("Descripción interna")
                wrapMode: TextEdit.WordWrap
                enabled: root.forge.hasProject && !root.forge.busy
                onTextChanged: if (activeFocus)
                    root.forge.setDescription(text)
            }
            Button {
                Layout.fillWidth: true
                text: qsTr("Crear NPC")
                enabled: root.forge.hasProject
                onClicked: {
                    root.forge.setWorkspaceValue("tool", "native");
                }
            }
            Button {
                Layout.fillWidth: true
                text: qsTr("Guardar proyecto")
                enabled: root.forge.hasProject && !root.forge.busy
                onClicked: root.save()
            }
            Text {
                Layout.fillWidth: true
                text: root.forge.projectPath || qsTr("Sin guardar")
                color: Theme.textMuted
                wrapMode: Text.WrapAnywhere
            }
            Text {
                Layout.fillWidth: true
                text: root.forge.catalogReady ? qsTr("Perfil indexado disponible") : qsTr("Carga un perfil MO2 para elegir registros reales. Puedes preparar y guardar el resto del proyecto ahora.")
                color: Theme.textDim
                wrapMode: Text.WordWrap
            }
        }
    }
    Component {
        id: nativePanel
        NativeNpcEditor {
            forge: root.forge
            implicitHeight: 680
        }
    }
    Component {
        id: importPanel
        ModelImporter {
            forge: root.forge
        }
    }
    Component {
        id: libraryPanel
        ForgeModelLibrary {
            forge: root.forge
        }
    }
    Component {
        id: blenderPanel
        BlenderConnectionPanel {
            forge: root.forge
        }
    }
    Component {
        id: aiPanel
        AIGenerationPanel {
            forge: root.forge
        }
    }
    Component {
        id: outfitPanel
        ForgeOutfitEditor {
            forge: root.forge
        }
    }
    Component {
        id: skeletonPanel
        SkeletonEditor {
            forge: root.forge
        }
    }
    Component {
        id: materialsPanel
        MaterialsEditor {
            forge: root.forge
        }
    }
    Component {
        id: validationPanel
        ForgeValidationPanel {
            forge: root.forge
        }
    }
    Component {
        id: exportPanel
        ForgeExportWizard {
            forge: root.forge
        }
    }
}
