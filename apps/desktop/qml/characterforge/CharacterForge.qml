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
    readonly property WorkspaceLayout panelLayout: Workbench.layout("forge")
    // Title and one line of purpose for the inspector of each tool.
    readonly property var toolInfo: ({
        "project": [qsTr("Proyecto"), qsTr("Nombre, descripción y estado del borrador .sncf.")],
        "native": [qsTr("Crear NPC"), qsTr("NPC nativo: identidad, estadísticas, arquetipos, voz, hechizos y perks.")],
        "outfit": [qsTr("Atuendos e inventario"), qsTr("Atuendo e inventario del personaje.")],
        "import": [qsTr("Importar modelo"), qsTr("Copia un modelo GLB o glTF validado al proyecto.")],
        "library": [qsTr("Biblioteca de modelos"), qsTr("Modelos ya importados en tus proyectos.")],
        "ai": [qsTr("Generar con IA"), qsTr("Imágenes de referencia y generación asistida.")],
        "blender": [qsTr("Blender y MCP"), qsTr("Conversión de modelos con tu instalación de Blender.")],
        "skeleton": [qsTr("Esqueleto"), qsTr("Huesos del modelo frente al esqueleto de Skyrim.")],
        "materials": [qsTr("Materiales"), qsTr("Texturas y materiales del modelo.")],
        "validation": [qsTr("Validación"), qsTr("Validación estructural y de comportamiento contra el perfil indexado.")],
        "export": [qsTr("Exportar"), qsTr("Exportación técnica. La exportación jugable sigue bloqueada sin FaceGen nativo.")]
    })
    function showTool(tool) {
        if (forge.hasProject)
            forge.setWorkspaceValue("tool", tool)
        else
            toolWithoutProject = tool
    }
    function newProject() { forge.newProject(qsTr("Nuevo personaje")) }
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
        WorkbenchSplit {
            id: panels
            objectName: "forgeSplit"
            Layout.fillWidth: true
            Layout.fillHeight: true
            layout: Workbench.layout("forge")
            leftPane: forgeLeftPanel
            rightPane: forgeRightPanel
            leftMinimum: 200
            rightMinimum: 260
            centerMinimum: 380
            Rectangle {
                id: forgeLeftPanel
                objectName: "forgeLeftPanel"
                color: Theme.panel
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0
                    PanelHeader {
                        Layout.fillWidth: true
                        title: qsTr("Herramientas")
                        glyph: "forge"
                        side: "left"
                        onCollapseRequested: root.panelLayout.leftVisible = false
                    }
                    ScrollView {
                        id: navigationScroll
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.margins: Theme.spacingMd
                        contentWidth: availableWidth
                        // Qt creates the scroll view's bars unnamed; screen readers announce them by these names.
                        Component.onCompleted: {
                            navigationScroll.ScrollBar.vertical.Accessible.name = qsTr("Desplazar la navegación de Forge")
                            navigationScroll.ScrollBar.horizontal.Accessible.name = qsTr("Desplazar la navegación de Forge en horizontal")
                        }
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
            }
            ForgeViewport {
                SplitView.fillWidth: true
                forge: root.forge
                onNewProjectRequested: root.newProject()
                onOpenProjectRequested: openDialog.open()
                onToolRequested: tool => root.showTool(tool)
            }
            Rectangle {
                id: forgeRightPanel
                objectName: "forgeRightPanel"
                color: Theme.panel
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0
                    PanelHeader {
                        objectName: "forgeInspectorHeader"
                        Layout.fillWidth: true
                        title: (root.toolInfo[root.currentTool] ?? root.toolInfo["project"])[0]
                        side: "right"
                        onCollapseRequested: root.panelLayout.rightVisible = false
                    }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.margins: Theme.spacingLg
                    spacing: Theme.spacingMd
                    Text {
                        Layout.fillWidth: true
                        text: (root.toolInfo[root.currentTool] ?? root.toolInfo["project"])[1]
                        color: Theme.textDim
                        font.pixelSize: Theme.small
                        wrapMode: Text.WordWrap
                    }
                    ScrollView {
                        id: contextScroll
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        contentWidth: availableWidth
                        // Qt creates the scroll view's bars unnamed; screen readers announce them by these names.
                        Component.onCompleted: {
                            contextScroll.ScrollBar.vertical.Accessible.name = qsTr("Desplazar el panel de la herramienta")
                            contextScroll.ScrollBar.horizontal.Accessible.name = qsTr("Desplazar el panel de la herramienta en horizontal")
                        }
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
                        id: advancedScroll
                        visible: advanced.checked
                        // Qt creates the scroll view's bars unnamed; screen readers announce them by these names.
                        Component.onCompleted: {
                            advancedScroll.ScrollBar.vertical.Accessible.name = qsTr("Desplazar el inspector avanzado")
                            advancedScroll.ScrollBar.horizontal.Accessible.name = qsTr("Desplazar el inspector avanzado en horizontal")
                        }
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
    }
    Component {
        id: projectPanel
        ColumnLayout {
            spacing: Theme.spacingMd
            Text {
                Layout.fillWidth: true
                text: qsTr("Crea y guarda un personaje original. El proyecto, sus archivos de referencia y su historial son independientes de los otros workspaces.")
                color: Theme.textDim
                wrapMode: Text.WordWrap
            }
            // Without a project nothing below can be edited: say how to get one (the same actions as the viewer).
            RowLayout {
                visible: !root.forge.hasProject
                Layout.fillWidth: true
                spacing: Theme.spacingSm
                Button {
                    objectName: "forgeProjectNewButton"
                    text: qsTr("Nuevo proyecto")
                    highlighted: true
                    enabled: !root.forge.busy
                    Layout.fillWidth: true
                    onClicked: root.newProject()
                }
                Button {
                    text: qsTr("Abrir .sncf…")
                    enabled: !root.forge.busy
                    Layout.fillWidth: true
                    onClicked: openDialog.open()
                }
            }
            Label {
                text: qsTr("Nombre del proyecto")
                font.pixelSize: Theme.small
                color: Theme.textDim
            }
            TextField {
                Layout.fillWidth: true
                Accessible.name: qsTr("Nombre del proyecto")
                text: root.forge.projectName
                placeholderText: qsTr("Nombre del proyecto")
                enabled: root.forge.hasProject && !root.forge.busy
                onEditingFinished: root.forge.setProjectName(text)
            }
            Label {
                text: qsTr("Descripción interna")
                font.pixelSize: Theme.small
                color: Theme.textDim
            }
            TextArea {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.px(100)
                Accessible.name: qsTr("Descripción interna")
                text: root.forge.description
                placeholderText: qsTr("Descripción interna")
                wrapMode: TextEdit.WordWrap
                enabled: root.forge.hasProject && !root.forge.busy
                onTextChanged: if (activeFocus)
                    root.forge.setDescription(text)
            }
            Button {
                Layout.fillWidth: true
                text: qsTr("Editar el NPC nativo")
                enabled: root.forge.hasProject
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Identidad, estadísticas, arquetipos, voz, hechizos y perks")
                onClicked: root.forge.setWorkspaceValue("tool", "native")
            }
            // Where the draft stands, in plain words: saved or not, what still needs checking, and what a
            // Windows validation does not prove.
            GridLayout {
                Layout.fillWidth: true
                Layout.topMargin: Theme.spacingSm
                columns: 2
                columnSpacing: Theme.spacingMd
                rowSpacing: Theme.spacingSm
                Text { text: qsTr("Guardado"); color: Theme.textDim; font.pixelSize: Theme.small }
                Text {
                    Layout.fillWidth: true
                    text: !root.forge.hasProject ? qsTr("Sin proyecto")
                        : root.forge.dirty ? qsTr("Cambios sin guardar (Ctrl+S)")
                        : root.forge.projectPath.length > 0 ? root.forge.projectPath : qsTr("Aún no guardado")
                    color: root.forge.dirty ? Theme.warn : Theme.text
                    font.pixelSize: Theme.small
                    wrapMode: Text.WrapAnywhere
                }
                Text { text: qsTr("Perfil"); color: Theme.textDim; font.pixelSize: Theme.small }
                Text {
                    Layout.fillWidth: true
                    text: root.forge.catalogReady ? qsTr("Indexado: los registros reales están disponibles")
                        : qsTr("Sin perfil MO2: puedes preparar y guardar el proyecto; los registros reales llegan al cargar uno")
                    color: root.forge.catalogReady ? Theme.ok : Theme.textDim
                    font.pixelSize: Theme.small
                    wrapMode: Text.WordWrap
                }
                Text { text: qsTr("Validación"); color: Theme.textDim; font.pixelSize: Theme.small }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Requerida antes de exportar (herramienta Validación)")
                    color: Theme.textDim
                    font.pixelSize: Theme.small
                    wrapMode: Text.WordWrap
                }
                Text { text: qsTr("FaceGen"); color: Theme.textDim; font.pixelSize: Theme.small }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Pendiente: la exportación jugable de NPC nuevos está bloqueada")
                    color: Theme.warn
                    font.pixelSize: Theme.small
                    wrapMode: Text.WordWrap
                }
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("Validado en Windows no significa jugable en Skyrim: eso solo lo confirma una prueba dentro del juego.")
                color: Theme.textMuted
                font.pixelSize: Theme.small
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
