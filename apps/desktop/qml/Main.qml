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
    font.family: Theme.fontFamily
    font.pixelSize: Theme.normal
    title: "Skyrim NPC Studio — Creative Workspace"
           + (studio.selectedProfile.length > 0 ? " [" + studio.instanceName + " / " + studio.selectedProfile + "]" : "")
           + (studio.outfit.dirty ? "  •" : "")

    Settings {
        id: windowSettings
        category: "window"
        property alias x: window.x
        property alias y: window.y
        property alias width: window.width
        property alias height: window.height
        property bool advancedMode: false
    }
    Connections {
        target: Theme
        function onAdvancedModeChanged() { windowSettings.advancedMode = Theme.advancedMode }
    }

    readonly property bool ready: studio.phase === StudioController.Ready

    // Panels of the workspace on screen (each workspace keeps its own; see WorkbenchState).
    readonly property var workspaceKeys: ["creator", "outfit", "forge"]
    readonly property WorkspaceLayout layout: Workbench.layout(workspaceKeys[workspaceTabs.currentIndex] ?? "creator")

    // Show or hide one panel of the current workspace; showing one leaves the focused viewer.
    function setPanel(which, shown) {
        if (layout.maximized) {
            layout.maximized = false
            if (!shown)
                return
        }
        if (which === "left") layout.leftVisible = shown
        else if (which === "right") layout.rightVisible = shown
        else layout.bottomVisible = shown
    }

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
        onForgeWithoutProfile: {
            workspaceTabs.currentIndex = 2;
            welcome.close();
        }
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

    Component.onCompleted: {
        Theme.advancedMode = windowSettings.advancedMode
        if (!ready) welcome.open()
    }

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
            Action {
                text: workspaceTabs.currentIndex === 2 ? qsTr("Panel de herramientas") : qsTr("Panel de exploración")
                shortcut: "Ctrl+B"
                checkable: true
                checked: window.layout.leftVisible && !window.layout.maximized
                onTriggered: window.setPanel("left", checked)
            }
            Action {
                text: workspaceTabs.currentIndex === 2 ? qsTr("Panel contextual") : qsTr("Panel de propiedades")
                shortcut: "Ctrl+Alt+B"
                checkable: true
                checked: window.layout.rightVisible && !window.layout.maximized
                onTriggered: window.setPanel("right", checked)
            }
            Action {
                text: qsTr("Consola y diagnósticos")
                shortcut: "Ctrl+J"
                checkable: true
                checked: window.layout.bottomVisible && !window.layout.maximized
                onTriggered: window.setPanel("bottom", checked)
            }
            Action {
                text: qsTr("Enfocar el visor")
                shortcut: "F11"
                checkable: true
                checked: window.layout.maximized
                onTriggered: window.layout.maximized = checked
            }
            Action {
                text: qsTr("Restablecer distribución")
                onTriggered: window.layout.reset()
            }
            MenuSeparator {}
            Menu {
                title: qsTr("Escala de interfaz (%1 %)").arg(Workbench.uiScale)
                Action { text: qsTr("Aumentar"); shortcut: StandardKey.ZoomIn; onTriggered: Workbench.zoomIn() }
                Action { text: qsTr("Reducir"); shortcut: StandardKey.ZoomOut; onTriggered: Workbench.zoomOut() }
                Action { text: qsTr("Restablecer (100 %)"); shortcut: "Ctrl+0"; onTriggered: Workbench.zoomReset() }
            }
            Menu {
                title: qsTr("Tamaño del texto (%1 %)").arg(Workbench.fontScale)
                Action { text: qsTr("Aumentar"); onTriggered: Workbench.fontScale = Workbench.fontScale + 10 }
                Action { text: qsTr("Reducir"); onTriggered: Workbench.fontScale = Workbench.fontScale - 10 }
                Action { text: qsTr("Restablecer (100 %)"); onTriggered: Workbench.fontScale = 100 }
            }
            Menu {
                title: qsTr("Densidad")
                ActionGroup { id: densityGroup }
                Action { text: qsTr("Compacta"); checkable: true; ActionGroup.group: densityGroup
                         checked: Workbench.density === Workbench.Compact; onTriggered: Workbench.density = Workbench.Compact }
                Action { text: qsTr("Normal"); checkable: true; ActionGroup.group: densityGroup
                         checked: Workbench.density === Workbench.Normal; onTriggered: Workbench.density = Workbench.Normal }
                Action { text: qsTr("Cómoda"); checkable: true; ActionGroup.group: densityGroup
                         checked: Workbench.density === Workbench.Comfortable; onTriggered: Workbench.density = Workbench.Comfortable }
            }
            Action { text: qsTr("Preferencias de apariencia…"); onTriggered: scaleDialog.open() }
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

    // Ctrl+= is Ctrl++ on keyboards where "+" needs Shift (StandardKey.ZoomIn covers the rest).
    Shortcut { sequence: "Ctrl+="; onActivated: Workbench.zoomIn() }

    ScalePreferencesDialog { id: scaleDialog }

    // ---------------------------------------------------------------- TOOLBAR SUPERIOR
    header: ToolBar {
        implicitHeight: Theme.toolbarHeight
        background: Rectangle {
            color: Theme.header
            border.color: Theme.borderSubtle
            border.width: 1
        }

        RowLayout {
            id: headerRow
            // Short labels when the window is narrow for the chosen scale, so every action stays on screen.
            readonly property bool compact: window.width / Theme.fontFactor < 1200
            anchors.fill: parent
            anchors.leftMargin: Theme.spacingLg
            anchors.rightMargin: Theme.spacingLg
            spacing: Theme.spacingLg

            // Modos de trabajo principales
            TabBar {
                id: workspaceTabs
                objectName: "workspaceTabs"
                implicitHeight: Theme.controlHeight + Theme.spacingXs
                // The last workspace comes back on start (automation may still pick another one).
                Component.onCompleted: currentIndex = Workbench.workspace
                onCurrentIndexChanged: Workbench.workspace = currentIndex
                // Each tab as wide as its label (TabBar would otherwise split the width evenly and elide).
                TabButton { width: implicitWidth; text: headerRow.compact ? qsTr("🎭 PERSONAJE") : qsTr("🎭 PERSONALIZAR PERSONAJE") }
                TabButton { width: implicitWidth; text: headerRow.compact ? qsTr("👗 ATUENDOS") : qsTr("👗 ESTUDIO DE ATUENDOS") }
                TabButton { width: implicitWidth; text: headerRow.compact ? qsTr("FORGE") : qsTr("CHARACTER FORGE") }
            }

            Rectangle { width: 1; height: Theme.px(22); color: Theme.borderSubtle }

            // Acciones de proyecto
            ToolButton {
                text: headerRow.compact ? "📁" : "📁 " + qsTr("Abrir")
                enabled: workspaceTabs.currentIndex === 2 || window.ready
                implicitHeight: Theme.controlHeight
                onClicked: workspaceTabs.currentIndex === 2 ? forgeOpenDialog.open() : openProjectDialog.open()
                ToolTip.visible: hovered; ToolTip.text: workspaceTabs.currentIndex === 2 ? qsTr("Abrir un proyecto .sncf") : qsTr("Abrir proyecto .snps existente")
            }

            ToolButton {
                text: headerRow.compact ? "💾" : "💾 " + qsTr("Guardar")
                enabled: workspaceTabs.currentIndex === 2 ? window.studio.forge.hasProject : window.studio.outfit.npcFormKey.length > 0
                implicitHeight: Theme.controlHeight
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

            Rectangle { width: 1; height: Theme.px(22); color: Theme.borderSubtle }

            // Visor a pantalla completa dentro de la ventana; la escena y la selección no cambian.
            ToolButton {
                objectName: "focusViewerButton"
                text: (window.layout.maximized ? "⤡" : "⤢") + (headerRow.compact ? ""
                      : " " + (window.layout.maximized ? qsTr("Salir del enfoque") : qsTr("Enfocar visor")))
                checkable: true
                checked: window.layout.maximized
                implicitHeight: Theme.controlHeight
                onClicked: window.layout.maximized = !window.layout.maximized
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Oculta los paneles laterales y la consola (F11). Vuelve a la distribución anterior al salir.")
            }

            Item { Layout.fillWidth: true }

            // Botón de exportación principal
            Button {
                objectName: "headerExportButton"
                text: headerRow.compact ? qsTr("🚀 Exportar") : qsTr("🚀 Exportar Mod…")
                highlighted: true
                enabled: window.ready && workspaceTabs.currentIndex !== 2
                implicitHeight: Theme.controlHeight + Theme.spacingXs
                font.bold: true
                ToolTip.visible: hovered && workspaceTabs.currentIndex === 2
                ToolTip.text: qsTr("La exportación de NPC nuevos llegará en Fase 2")
                onClicked: exportWizardDialog.open()
            }
        }
    }

    // ---------------------------------------------------------------- CONTENIDO PRINCIPAL
    SplitView {
        id: mainSplit
        objectName: "mainVerticalSplit"
        anchors.fill: parent
        orientation: Qt.Vertical
        handle: SplitHandleBar { vertical: true }

        readonly property bool bottomShown: window.layout.bottomVisible && !window.layout.maximized
        property int appliedBottom: 0
        // The console keeps the height the user dragged it to (per workspace, at 100 % scale) but never
        // takes more than 60 % of the window, so the viewer stays usable.
        function applyBottom() {
            if (height <= 0) return
            appliedBottom = Math.max(Theme.px(80), Math.min(Theme.px(window.layout.bottomHeight), Math.round(height * 0.6)))
            bottomPanel.SplitView.preferredHeight = appliedBottom
        }
        onResizingChanged: {
            if (!resizing && bottomShown && Math.abs(bottomPanel.height - appliedBottom) > 1)
                window.layout.bottomHeight = Math.round(bottomPanel.height / Theme.uiFactor)
            applyBottom()
        }
        onHeightChanged: applyBottom()
        onBottomShownChanged: applyBottom()
        Connections {
            target: window
            function onLayoutChanged() { mainSplit.applyBottom() }
        }
        Connections {
            target: window.layout
            function onBottomHeightChanged() { mainSplit.applyBottom() }
        }
        Connections {
            target: Theme
            function onUiFactorChanged() { mainSplit.applyBottom() }
        }

        StackLayout {
            currentIndex: workspaceTabs.currentIndex
            SplitView.fillHeight: true

            // Vista 1: Character Creator completo
            CharacterCreator {
                studio: window.studio
            }

            // Vista 2: Estudio de Atuendos dedicado (Layout 3 columnas: 24% | 56% | 20%)
            WorkbenchSplit {
                objectName: "outfitSplit"
                SplitView.fillHeight: true
                layout: Workbench.layout("outfit")
                leftPane: outfitLeftPanel
                rightPane: outfitRightPanel
                leftMinimum: 240
                rightMinimum: 280
                centerMinimum: 380

                // Panel Izquierdo: Catálogo y Exploración
                Rectangle {
                    id: outfitLeftPanel
                    objectName: "outfitLeftPanel"
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
                    id: outfitRightPanel
                    objectName: "outfitRightPanel"
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
            id: bottomPanel
            objectName: "bottomPanel"
            visible: mainSplit.bottomShown
            studio: window.studio
            SplitView.minimumHeight: Theme.px(80)
        }
    }

    // ---------------------------------------------------------------- STATUS BAR INFERIOR
    footer: Rectangle {
        implicitHeight: Theme.statusHeight
        color: Theme.header
        border.color: Theme.borderSubtle
        border.width: 1

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.spacingLg
            anchors.rightMargin: Theme.spacingLg
            spacing: Theme.spacingLg

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
                Layout.preferredWidth: Theme.px(200)
            }

            Text {
                visible: window.ready
                text: qsTr("%1 NPC · %2 armaduras · %3 plugins · %4 archivos")
                      .arg(window.studio.summary.npcs ?? 0).arg(window.studio.summary.armors ?? 0)
                      .arg(window.studio.summary.activePlugins ?? 0).arg(window.studio.summary.files ?? 0)
                color: Theme.textDim
                font.pixelSize: Theme.small
                // Gives way (elided) before the scale and console buttons leave a narrow window.
                elide: Text.ElideRight
                Layout.fillWidth: true
                Layout.maximumWidth: implicitWidth
            }

            Rectangle { width: 1; height: Theme.px(16); color: Theme.borderSubtle }

            // Escala actual; un clic abre las preferencias de apariencia.
            ToolButton {
                objectName: "scaleStatusButton"
                text: Workbench.uiScale + " %" + (Workbench.fontScale !== 100 ? " · " + qsTr("texto %1 %").arg(Workbench.fontScale) : "")
                implicitHeight: Theme.statusHeight - 6
                font.pixelSize: Theme.small
                onClicked: scaleDialog.open()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Escala de interfaz: Ctrl++ / Ctrl+- / Ctrl+0")
            }

            Rectangle { width: 1; height: Theme.px(16); color: Theme.borderSubtle }

            // Botón de alternancia de Consola y Diagnósticos
            ToolButton {
                text: "📟 " + qsTr("Consola & Diagnósticos") + (window.studio.diagnostics.count > 0 ? " (" + window.studio.diagnostics.count + ")" : "")
                checkable: true
                checked: mainSplit.bottomShown
                implicitHeight: Theme.statusHeight - 6
                font.pixelSize: Theme.small
                onClicked: window.setPanel("bottom", !mainSplit.bottomShown)
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Mostrar/ocultar panel de consola y diagnósticos técnicos")
            }
        }
    }
}
