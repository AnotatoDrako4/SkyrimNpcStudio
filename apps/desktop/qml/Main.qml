import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import SkyrimNpcStudio

ApplicationWindow {
    id: window
    required property StudioController studio

    width: 1600
    height: 960
    minimumWidth: 1100
    minimumHeight: 700
    visible: true
    color: Theme.bg
    title: "Skyrim NPC Studio — Creative Workspace"
           + (studio.selectedProfile.length > 0 ? " [" + studio.instanceName + " / " + studio.selectedProfile + "]" : "")
           + (studio.outfit.dirty ? "  •" : "")

    Settings {
        category: "window"
        property alias x: window.x
        property alias y: window.y
        property alias width: window.width
        property alias height: window.height
        property alias showBottom: bottomToggle.checked
        property alias showLeft: leftToggle.checked
        property alias showRight: rightToggle.checked
        property alias advancedMode: modeSwitch.checked
    }

    readonly property bool ready: studio.phase === StudioController.Ready

    // ---------------------------------------------------------------- DIÁLOGOS
    FileDialog {
        id: instanceDialog
        title: qsTr("Selecciona ModOrganizer.ini")
        nameFilters: ["ModOrganizer.ini (ModOrganizer.ini)"]
        onAccepted: if (window.studio.openInstance(selectedFile)) welcome.open()
    }
    FolderDialog {
        id: openProjectDialog
        title: qsTr("Abrir proyecto (.snps)")
        onAccepted: window.studio.outfit.openProject(selectedFolder)
    }
    FolderDialog {
        id: saveProjectDialog
        title: qsTr("Carpeta donde guardar el proyecto")
        onAccepted: window.studio.outfit.saveProject(selectedFolder, window.studio.outfit.npcLabel.split(" (")[0])
    }
    WelcomeOverlay {
        id: welcome
        studio: window.studio
        onBrowseInstance: instanceDialog.open()
        onBrowseGame: gameDialog.open()
    }
    FolderDialog {
        id: gameDialog
        title: qsTr("Carpeta que contiene SkyrimSE.exe")
        onAccepted: window.studio.setGamePath(selectedFolder)
    }
    ExportDialog {
        id: exportDialog
        studio: window.studio
    }
    ExportWizardDialog {
        id: exportWizardDialog
        studio: window.studio
    }
    FolderDialog {
        id: forgeOpenDialog
        title: qsTr("Abrir proyecto Character Forge (.sncf)")
        onAccepted: window.studio.forge.openProject(selectedFolder.toString())
    }
    FolderDialog {
        id: forgeSaveDialog
        title: qsTr("Carpeta para guardar proyecto Character Forge")
        onAccepted: window.studio.forge.saveProject(selectedFolder.toString())
    }

    Component.onCompleted: if (!ready) welcome.open()

    // ---------------------------------------------------------------- MENÚS
    menuBar: MenuBar {
        Menu {
            title: qsTr("&Proyecto")
            Action {
                text: workspaceTabs.currentIndex === 2 ? qsTr("Abrir Character Forge (.sncf)…") : qsTr("Abrir proyecto (.snps)…")
                enabled: workspaceTabs.currentIndex === 2 || window.ready
                onTriggered: workspaceTabs.currentIndex === 2 ? forgeOpenDialog.open() : openProjectDialog.open()
            }
            Action {
                text: qsTr("Guardar proyecto")
                shortcut: StandardKey.Save
                enabled: workspaceTabs.currentIndex === 2 ? window.studio.forge.hasProject : window.studio.outfit.npcFormKey.length > 0
                onTriggered: {
                    if (workspaceTabs.currentIndex === 2) {
                        window.studio.forge.projectPath.length > 0
                                ? window.studio.forge.saveProject(window.studio.forge.projectPath)
                                : forgeSaveDialog.open()
                    } else {
                        window.studio.outfit.projectPath.length > 0
                                ? window.studio.outfit.saveProject(window.studio.outfit.projectPath, "")
                                : saveProjectDialog.open()
                    }
                }
            }
            Action {
                text: qsTr("Guardar proyecto como…")
                enabled: workspaceTabs.currentIndex === 2 ? window.studio.forge.hasProject : window.studio.outfit.npcFormKey.length > 0
                onTriggered: workspaceTabs.currentIndex === 2 ? forgeSaveDialog.open() : saveProjectDialog.open()
            }
        }
        Menu {
            title: qsTr("&Archivo")
            Action { text: qsTr("Instancia y perfil de MO2…"); onTriggered: welcome.open() }
            Action { text: qsTr("Abrir ModOrganizer.ini…"); onTriggered: instanceDialog.open() }
            MenuSeparator {}
            Action { text: qsTr("Salir"); shortcut: StandardKey.Quit; onTriggered: Qt.quit() }
        }
        Menu {
            title: qsTr("&Editar")
            Action {
                text: qsTr("Deshacer") + (workspaceTabs.currentIndex === 2 ? "" : (window.studio.outfit.undoText ? ": " + window.studio.outfit.undoText : ""))
                shortcut: StandardKey.Undo
                enabled: workspaceTabs.currentIndex === 2 ? window.studio.forge.canUndo : window.studio.outfit.canUndo
                onTriggered: workspaceTabs.currentIndex === 2 ? window.studio.forge.undo() : window.studio.outfit.undo()
            }
            Action {
                text: qsTr("Rehacer") + (workspaceTabs.currentIndex === 2 ? "" : (window.studio.outfit.redoText ? ": " + window.studio.outfit.redoText : ""))
                shortcut: StandardKey.Redo
                enabled: workspaceTabs.currentIndex === 2 ? window.studio.forge.canRedo : window.studio.outfit.canRedo
                onTriggered: workspaceTabs.currentIndex === 2 ? window.studio.forge.redo() : window.studio.outfit.redo()
            }
        }
        Menu {
            title: qsTr("&Vista")
            Action { id: leftToggle; text: qsTr("Panel de exploración"); checkable: true; checked: true }
            Action { id: rightToggle; text: qsTr("Panel de propiedades"); checkable: true; checked: true }
            Action { id: bottomToggle; text: qsTr("Consola y diagnósticos"); checkable: true; checked: false }
            MenuSeparator {}
            Action {
                text: qsTr("Modo avanzado")
                checkable: true
                checked: Theme.advancedMode
                onTriggered: Theme.advancedMode = checked
            }
        }
        Menu {
            title: qsTr("&Herramientas")
            Action {
                text: qsTr("Reindexar perfil")
                shortcut: "F5"
                enabled: window.studio.selectedProfile.length > 0 && window.studio.phase !== StudioController.Scanning
                         && window.studio.phase !== StudioController.Indexing
                onTriggered: window.studio.reindex()
            }
            Action {
                text: qsTr("Cancelar operación en curso")
                enabled: window.studio.phase === StudioController.Scanning || window.studio.phase === StudioController.Indexing
                onTriggered: window.studio.cancel()
            }
            Action {
                text: qsTr("Reiniciar worker")
                onTriggered: { window.studio.worker.stop(); window.studio.worker.start() }
            }
        }
        Menu {
            title: qsTr("E&xportar")
            Action {
                text: qsTr("Exportar mod completo / rostro (Character Creator)…")
                shortcut: "Ctrl+Shift+E"
                enabled: workspaceTabs.currentIndex !== 2 && window.ready && !window.studio.appearance.exporting
                onTriggered: exportWizardDialog.open()
            }
            Action {
                text: qsTr("Exportar parche de atuendo (OTFT)…")
                shortcut: "Ctrl+E"
                enabled: workspaceTabs.currentIndex !== 2 && window.studio.outfit.exportable && !window.studio.outfit.exporting
                onTriggered: exportDialog.open()
            }
        }
    }

    // ---------------------------------------------------------------- TOOLBAR SUPERIOR
    header: ToolBar {
        implicitHeight: 44
        background: Rectangle {
            color: Theme.header
            border.color: Theme.borderSubtle
            border.width: 1
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            spacing: 10

            // Modos de trabajo principales
            TabBar {
                id: workspaceTabs
                objectName: "workspaceTabs"
                implicitHeight: 32
                TabButton { text: qsTr("🎭 PERSONALIZAR PERSONAJE") }
                TabButton { text: qsTr("👗 ESTUDIO DE ATUENDOS") }
                TabButton { text: qsTr("CHARACTER FORGE") }
            }

            Rectangle { width: 1; height: 22; color: Theme.borderSubtle }

            // Acciones de proyecto
            ToolButton {
                text: "📁 " + qsTr("Abrir")
                enabled: workspaceTabs.currentIndex === 2 || window.ready
                implicitHeight: 28
                onClicked: workspaceTabs.currentIndex === 2 ? forgeOpenDialog.open() : openProjectDialog.open()
                ToolTip.visible: hovered; ToolTip.text: workspaceTabs.currentIndex === 2 ? qsTr("Abrir un proyecto .sncf") : qsTr("Abrir proyecto .snps existente")
            }

            ToolButton {
                text: "💾 " + qsTr("Guardar")
                enabled: workspaceTabs.currentIndex === 2 ? window.studio.forge.hasProject : window.studio.outfit.npcFormKey.length > 0
                implicitHeight: 28
                onClicked: {
                    if (workspaceTabs.currentIndex === 2) {
                        window.studio.forge.projectPath.length > 0
                                ? window.studio.forge.saveProject(window.studio.forge.projectPath)
                                : forgeSaveDialog.open()
                    } else {
                        window.studio.outfit.projectPath.length > 0
                                ? window.studio.outfit.saveProject(window.studio.outfit.projectPath, "")
                                : saveProjectDialog.open()
                    }
                }
                ToolTip.visible: hovered; ToolTip.text: workspaceTabs.currentIndex === 2 ? qsTr("Guardar proyecto independiente .sncf") : qsTr("Guardar proyecto .snps actual")
            }

            Item { Layout.fillWidth: true }

            // Conmutador Modo Simple / Avanzado
            Switch {
                id: modeSwitch
                text: Theme.advancedMode ? qsTr("Modo Avanzado") : qsTr("Modo Simple")
                checked: Theme.advancedMode
                onToggled: Theme.advancedMode = checked
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Alterna entre interfaz simplificada e inspector técnico con diagnósticos")
            }

            Rectangle { width: 1; height: 22; color: Theme.borderSubtle }

            // Botón de exportación principal
            Button {
                text: qsTr("🚀 Exportar Mod…")
                highlighted: true
                enabled: window.ready && workspaceTabs.currentIndex !== 2
                implicitHeight: 32
                font.bold: true
                ToolTip.visible: hovered && workspaceTabs.currentIndex === 2
                ToolTip.text: qsTr("La exportación de NPC nuevos llegará en Fase 2")
                onClicked: exportWizardDialog.open()
            }
        }
    }

    // ---------------------------------------------------------------- CONTENIDO PRINCIPAL
    SplitView {
        anchors.fill: parent
        orientation: Qt.Vertical

        StackLayout {
            currentIndex: workspaceTabs.currentIndex
            SplitView.fillHeight: true

            // Vista 1: Character Creator completo
            CharacterCreator {
                studio: window.studio
            }

            // Vista 2: Estudio de Atuendos dedicado (Layout 3 columnas: 24% | 56% | 20%)
            SplitView {
                SplitView.fillHeight: true
                orientation: Qt.Horizontal

                // Panel Izquierdo: Catálogo y Exploración (~24%)
                Rectangle {
                    visible: leftToggle.checked
                    SplitView.preferredWidth: 380
                    SplitView.minimumWidth: 280
                    SplitView.maximumWidth: 500
                    color: Theme.panel

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 0

                        TabBar {
                            id: leftTabs
                            Layout.fillWidth: true
                            TabButton { text: qsTr("Prendas") }
                            TabButton { text: qsTr("NPCs") }
                            TabButton { text: qsTr("Mods") }
                        }

                        StackLayout {
                            currentIndex: leftTabs.currentIndex
                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            ArmorCatalog {
                                studio: window.studio
                                onPreviewRequested: (formKey) => {
                                    window.studio.armorPreview.selectArmor(formKey)
                                }
                            }

                            NpcBrowser {
                                studio: window.studio
                            }

                            ModBrowser {
                                studio: window.studio
                            }
                        }
                    }
                }

                // Panel Central: Visor 3D Dominante (~56%)
                Rectangle {
                    SplitView.fillWidth: true
                    SplitView.minimumWidth: 400
                    color: Theme.surface

                    CharacterViewport {
                        anchors.fill: parent
                        studio: window.studio
                        sceneOverride: window.studio.armorPreview
                        previewMode: true
                    }
                }

                // Panel Derecho: Editor de Atuendos (~20%)
                Rectangle {
                    visible: rightToggle.checked
                    SplitView.preferredWidth: 380
                    SplitView.minimumWidth: 300
                    SplitView.maximumWidth: 550
                    color: Theme.panel

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 0

                        TabBar {
                            id: rightTabs
                            Layout.fillWidth: true
                            TabButton { text: qsTr("Outfit Activo") }
                            TabButton { text: qsTr("Dependencias") }
                        }

                        StackLayout {
                            currentIndex: rightTabs.currentIndex
                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            OutfitPanel {
                                studio: window.studio
                                onExportRequested: exportDialog.open()
                            }

                            DependenciesPanel {
                                studio: window.studio
                            }
                        }
                    }
                }
            }

            CharacterForge {
                forge: window.studio.forge
            }
        }

        // Panel inferior colapsable para Consola y Diagnósticos
        BottomPanel {
            visible: bottomToggle.checked
            studio: window.studio
            SplitView.preferredHeight: 200
            SplitView.minimumHeight: 120
        }
    }

    // ---------------------------------------------------------------- STATUS BAR INFERIOR
    footer: Rectangle {
        implicitHeight: 28
        color: Theme.header
        border.color: Theme.borderSubtle
        border.width: 1

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            spacing: 12

            StatusBadge {
                text: window.studio.worker.state === WorkerClient.Ready ? qsTr("WORKER OK")
                      : window.studio.worker.state === WorkerClient.Starting ? qsTr("WORKER…") : qsTr("WORKER OFF")
                tone: window.studio.worker.state === WorkerClient.Ready ? Theme.ok
                      : window.studio.worker.state === WorkerClient.Starting ? Theme.warn : Theme.error
                ToolTip.visible: workerHover.hovered
                ToolTip.text: window.studio.worker.statusText
                HoverHandler { id: workerHover }
            }

            Text {
                text: window.studio.progressText
                color: Theme.textDim
                font.pixelSize: Theme.small
                elide: Text.ElideRight
                Layout.fillWidth: true
            }

            ProgressBar {
                visible: window.studio.phase === StudioController.Scanning || window.studio.phase === StudioController.Indexing
                indeterminate: window.studio.progress < 0
                value: Math.max(0, window.studio.progress)
                Layout.preferredWidth: 200
            }

            Text {
                visible: window.ready
                text: qsTr("%1 NPC · %2 armaduras · %3 plugins · %4 archivos")
                      .arg(window.studio.summary.npcs ?? 0).arg(window.studio.summary.armors ?? 0)
                      .arg(window.studio.summary.activePlugins ?? 0).arg(window.studio.summary.files ?? 0)
                color: Theme.textDim
                font.pixelSize: Theme.small
            }

            Rectangle { width: 1; height: 16; color: Theme.borderSubtle }

            // Botón de alternancia de Consola y Diagnósticos
            ToolButton {
                text: "📟 " + qsTr("Consola & Diagnósticos") + (window.studio.diagnostics.count > 0 ? " (" + window.studio.diagnostics.count + ")" : "")
                checkable: true
                checked: bottomToggle.checked
                implicitHeight: 22
                font.pixelSize: Theme.micro
                onClicked: bottomToggle.checked = !bottomToggle.checked
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Mostrar/ocultar panel de consola y diagnósticos técnicos")
            }
        }
    }
}
