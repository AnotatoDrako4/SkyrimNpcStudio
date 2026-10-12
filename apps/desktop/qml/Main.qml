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
    title: "Skyrim NPC Studio"
           + (studio.selectedProfile.length > 0 ? " — " + studio.instanceName + " / " + studio.selectedProfile : "")
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
    }

    readonly property bool ready: studio.phase === StudioController.Ready

    // ---------------------------------------------------------------- dialogs
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

    Component.onCompleted: if (!ready) welcome.open()

    // ---------------------------------------------------------------- menus
    menuBar: MenuBar {
        Menu {
            title: qsTr("&Proyecto")
            Action { text: qsTr("Abrir proyecto…"); enabled: window.ready; onTriggered: openProjectDialog.open() }
            Action {
                text: qsTr("Guardar proyecto")
                shortcut: StandardKey.Save
                enabled: window.studio.outfit.npcFormKey.length > 0
                onTriggered: window.studio.outfit.projectPath.length > 0
                             ? window.studio.outfit.saveProject(window.studio.outfit.projectPath, "")
                             : saveProjectDialog.open()
            }
            Action {
                text: qsTr("Guardar proyecto como…")
                enabled: window.studio.outfit.npcFormKey.length > 0
                onTriggered: saveProjectDialog.open()
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
                text: qsTr("Deshacer") + (window.studio.outfit.undoText ? ": " + window.studio.outfit.undoText : "")
                shortcut: StandardKey.Undo
                enabled: window.studio.outfit.canUndo
                onTriggered: window.studio.outfit.undo()
            }
            Action {
                text: qsTr("Rehacer") + (window.studio.outfit.redoText ? ": " + window.studio.outfit.redoText : "")
                shortcut: StandardKey.Redo
                enabled: window.studio.outfit.canRedo
                onTriggered: window.studio.outfit.redo()
            }
        }
        Menu {
            title: qsTr("&Vista")
            Action { id: leftToggle; text: qsTr("Panel de exploración"); checkable: true; checked: true }
            Action { id: rightToggle; text: qsTr("Panel de propiedades"); checkable: true; checked: true }
            Action { id: bottomToggle; text: qsTr("Consola y diagnósticos"); checkable: true; checked: true }
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
                text: qsTr("Exportar mod completo / rostro (Character Creator 2.0)…")
                shortcut: "Ctrl+Shift+E"
                enabled: window.ready && !window.studio.appearance.exporting
                onTriggered: exportWizardDialog.open()
            }
            Action {
                text: qsTr("Exportar parche de atuendo (Modo A)…")
                shortcut: "Ctrl+E"
                enabled: window.studio.outfit.exportable && !window.studio.outfit.exporting
                onTriggered: exportDialog.open()
            }
        }
    }

    header: ToolBar {
        background: Rectangle { color: Theme.header; border.color: Theme.border }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            spacing: 12
            TabBar {
                id: workspaceTabs
                TabButton { text: qsTr("🎭 PERSONALIZAR PERSONAJE (2.0)") }
                TabButton { text: qsTr("👗 ESTUDIO DE ATUENDOS") }
            }
            Item { Layout.fillWidth: true }
            Button {
                text: qsTr("🚀 Exportar Mod…")
                highlighted: true
                enabled: window.ready
                onClicked: exportWizardDialog.open()
            }
        }
    }

    // ---------------------------------------------------------------- layout
    SplitView {
        anchors.fill: parent
        orientation: Qt.Vertical

        StackLayout {
            currentIndex: workspaceTabs.currentIndex
            SplitView.fillHeight: true

            CharacterCreator {
                studio: window.studio
            }

            SplitView {
                SplitView.fillHeight: true
                orientation: Qt.Horizontal

            Rectangle {
                visible: leftToggle.checked
                SplitView.preferredWidth: 380
                SplitView.minimumWidth: 280
                color: Theme.panel
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0
                    TabBar {
                        id: leftTabs
                        Layout.fillWidth: true
                        TabButton { text: qsTr("NPC") }
                        TabButton { text: qsTr("Mods") }
                        TabButton { text: qsTr("Ropa y armaduras") }
                    }
                    StackLayout {
                        currentIndex: leftTabs.currentIndex
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        NpcBrowser { studio: window.studio }
                        ModBrowser { studio: window.studio }
                        ArmorCatalog {
                            studio: window.studio
                            onPreviewRequested: (formKey) => {
                                window.studio.armorPreview.selectArmor(formKey)
                                centerTabs.currentIndex = 2
                            }
                        }
                    }
                }
            }

            ColumnLayout {
                SplitView.fillWidth: true
                SplitView.minimumWidth: 300
                spacing: 0
                TabBar {
                    id: centerTabs
                    Layout.fillWidth: true
                    TabButton { text: qsTr("Vista 3D") }
                    TabButton { text: qsTr("Registro NPC") }
                    TabButton { text: qsTr("Estudio de prendas") }
                }
                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: centerTabs.currentIndex
                    CharacterViewport { studio: window.studio }
                    NpcOverview { studio: window.studio }
                    CharacterViewport {
                        studio: window.studio
                        sceneOverride: window.studio.armorPreview
                        previewMode: true
                    }
                }
            }

            Rectangle {
                visible: rightToggle.checked
                SplitView.preferredWidth: 440
                SplitView.minimumWidth: 320
                color: Theme.panel
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0
                    TabBar {
                        id: rightTabs
                        Layout.fillWidth: true
                        TabButton { text: qsTr("Ropa") }
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
                        DependenciesPanel { studio: window.studio }
                    }
                }
            }
        }
    }

    BottomPanel {
            visible: bottomToggle.checked
            studio: window.studio
            SplitView.preferredHeight: 220
            SplitView.minimumHeight: 120
        }
    }

    // ---------------------------------------------------------------- status bar
    footer: Rectangle {
        implicitHeight: 26
        color: Theme.header
        border.color: Theme.border
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
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
                Layout.preferredWidth: 220
            }
            Text {
                visible: window.ready
                text: qsTr("%1 NPC · %2 armaduras · %3 plugins · %4 archivos")
                      .arg(window.studio.summary.npcs ?? 0).arg(window.studio.summary.armors ?? 0)
                      .arg(window.studio.summary.activePlugins ?? 0).arg(window.studio.summary.files ?? 0)
                color: Theme.textDim
                font.pixelSize: Theme.small
            }
        }
    }
}
