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
    // Controls (Fusion) follow the theme chosen in Configuración: menus, fields, lists and dialogs too.
    // "Grafito oscuro" gives back the original palette of main.cpp exactly.
    palette.window: Theme.pal.qtWindow
    palette.windowText: Theme.pal.qtText
    palette.base: Theme.pal.qtBase
    palette.alternateBase: Theme.pal.qtAlternateBase
    palette.text: Theme.pal.qtText
    palette.button: Theme.pal.qtButton
    palette.buttonText: Theme.pal.qtText
    palette.brightText: "white"
    palette.highlight: Theme.pal.qtHighlight
    palette.highlightedText: Theme.onAccent
    palette.link: Theme.pal.qtHighlight
    palette.toolTipBase: Theme.pal.qtWindow
    palette.toolTipText: Theme.pal.qtText
    palette.placeholderText: Theme.pal.qtPlaceholder
    palette.mid: Theme.pal.qtMid
    palette.dark: Theme.pal.qtDark
    palette.disabled.text: Theme.pal.qtDisabled
    palette.disabled.buttonText: Theme.pal.qtDisabled
    palette.disabled.windowText: Theme.pal.qtDisabled
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

    // Stage of the NPC gallery portraits: a component the renderer creates inside its own off-screen window
    // (never in this one, whose viewer would lose its Quick 3D resources). The renderer gives it its scene.
    Component {
        id: portraitStageComponent
        PortraitStage {}
    }

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

    // The Settings Center reaches the window preferences through WorkbenchState (one source of truth).
    Binding { target: AppSettings; property: "workbench"; value: Workbench }
    Binding { target: window.studio.portraits; property: "instantCache"; value: AppSettings.portraitInstantCache }

    SettingsCenter {
        id: settingsCenter
        studio: window.studio
    }

    Dialog {
        id: saveLayoutDialog
        title: qsTr("Guardar distribución del espacio de trabajo")
        modal: true
        anchors.centerIn: Overlay.overlay
        standardButtons: Dialog.Save | Dialog.Cancel
        onAccepted: {
            if (saveLayoutNameField.text.trim().length > 0) {
                Workbench.saveLayout(saveLayoutNameField.text.trim())
                saveLayoutNameField.text = ""
            }
        }
        ColumnLayout {
            spacing: Theme.spacingMd
            Label { text: qsTr("Nombre para la distribución personalizada:") }
            TextField {
                id: saveLayoutNameField
                Layout.preferredWidth: Theme.px(300)
                placeholderText: qsTr("Ej: Mi Edición Dual, Visor Ancho...")
            }
        }
    }

    // Leaving with unsaved work asks first (Configuración › General). Automation quits without closing the window.
    property bool closeConfirmed: false
    readonly property bool hasUnsavedWork: window.studio.outfit.dirty || window.studio.appearance.dirty || window.studio.forge.dirty
    onClosing: (close) => {
        if (closeConfirmed || !AppSettings.confirmUnsavedClose || !hasUnsavedWork) return
        close.accepted = false
        unsavedDialog.open()
    }
    Dialog {
        id: unsavedDialog
        objectName: "unsavedCloseDialog"
        title: qsTr("Hay cambios sin guardar")
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(Theme.px(480), window.width - 40)
        Label {
            width: parent.width
            wrapMode: Text.WordWrap
            text: [window.studio.outfit.dirty ? qsTr("• Atuendo (.snps) modificado") : "",
                   window.studio.appearance.dirty ? qsTr("• Rostro o altura del NPC editados") : "",
                   window.studio.forge.dirty ? qsTr("• Proyecto Character Forge (.sncf) modificado") : ""]
                  .filter(s => s.length > 0).join("\n") + "\n\n" + qsTr("Si sales ahora se perderán. Nada se ha escrito en Skyrim ni en MO2.")
        }
        footer: DialogButtonBox {
            Button { text: qsTr("Volver"); DialogButtonBox.buttonRole: DialogButtonBox.RejectRole; highlighted: true }
            Button { text: qsTr("Salir sin guardar"); DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole }
            onRejected: unsavedDialog.close()
            onClicked: (button) => {
                if (button.DialogButtonBox.buttonRole === DialogButtonBox.DestructiveRole) {
                    window.closeConfirmed = true
                    unsavedDialog.close()
                    window.close()
                }
            }
        }
    }

    // Theme or scale being previewed from Configuración: kept only on "Mantener", reverted automatically otherwise,
    // so an unreadable choice never sticks. Shown above every dialog.
    Popup {
        objectName: "themePreviewBanner"
        visible: AppSettings.previewActive
        // Bottom center: never over the Settings Center's search or the menus.
        x: ((parent ? parent.width : window.width) - width) / 2
        y: (parent ? parent.height : window.height) - height - Theme.toolbarHeight - Theme.spacingXl * 2
        z: 1000
        modal: false
        closePolicy: Popup.NoAutoClose
        padding: Theme.spacingMd
        background: Rectangle { color: Theme.panelHeader; border.color: Theme.accent; border.width: 2; radius: Theme.radiusLarge }
        contentItem: RowLayout {
            spacing: Theme.spacingLg
            Label {
                text: qsTr("¿Mantener esta apariencia? Se revierte en %1 s").arg(AppSettings.previewSecondsLeft)
                color: Theme.text
                font.pixelSize: Theme.normal
            }
            Button { objectName: "keepPreviewButton"; text: qsTr("Mantener"); highlighted: true; onClicked: AppSettings.keepPreview() }
            Button { objectName: "revertPreviewButton"; text: qsTr("Revertir"); onClicked: AppSettings.revertPreview() }
        }
    }

    Component.onCompleted: {
        Theme.advancedMode = windowSettings.advancedMode
        window.studio.portraits.attachStage(portraitStageComponent)
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
            Action {
                objectName: "settingsAction"
                text: qsTr("Configuración…")
                shortcut: "Ctrl+,"
                onTriggered: settingsCenter.openAt("", "")
            }
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
            Menu {
                title: qsTr("Distribuciones de paneles")
                Repeater {
                    model: Workbench.builtinLayouts
                    MenuItem {
                        text: modelData.name
                        onTriggered: Workbench.applyBuiltinLayout(modelData.key, window.workspaceKeys[workspaceTabs.currentIndex] ?? "creator")
                    }
                }
                MenuSeparator { visible: Workbench.savedLayouts.length > 0 }
                Menu {
                    title: qsTr("Mis distribuciones guardadas")
                    visible: Workbench.savedLayouts.length > 0
                    Repeater {
                        model: Workbench.savedLayouts
                        MenuItem {
                            text: modelData
                            onTriggered: Workbench.applyLayout(modelData)
                        }
                    }
                }
                MenuSeparator {}
                Action {
                    text: qsTr("Guardar distribución actual…")
                    onTriggered: saveLayoutDialog.open()
                }
                Action {
                    text: qsTr("Restablecer a valores de fábrica")
                    onTriggered: window.layout.reset()
                }
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
            Action { text: qsTr("Tema y colores…"); onTriggered: settingsCenter.openAt("appearance", "appearance.theme") }
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
    Shortcut { sequence: "Ctrl+-"; onActivated: Workbench.zoomOut() }
    Shortcut { sequence: "Ctrl+0"; onActivated: Workbench.zoomReset() }

    ScalePreferencesDialog {
        id: scaleDialog
        onMoreRequested: { scaleDialog.close(); settingsCenter.openAt("appearance", "") }
    }

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
            readonly property bool ultraCompact: window.width / Theme.fontFactor < 750
            anchors.fill: parent
            anchors.leftMargin: ultraCompact ? Theme.spacingSm : Theme.spacingLg
            anchors.rightMargin: ultraCompact ? Theme.spacingSm : Theme.spacingLg
            spacing: ultraCompact ? Theme.spacingSm : Theme.spacingLg

            // Modos de trabajo principales
            TabBar {
                id: workspaceTabs
                objectName: "workspaceTabs"
                implicitHeight: Theme.controlHeight + Theme.spacingXs
                // The last workspace comes back on start, or the fixed one chosen in Configuración › General
                // (automation may still pick another one).
                Component.onCompleted: {
                    const fixed = ["creator", "outfit", "forge"].indexOf(AppSettings.startWorkspace)
                    currentIndex = fixed >= 0 ? fixed : Workbench.workspace
                }
                onCurrentIndexChanged: Workbench.workspace = currentIndex
                // Each tab as wide as its label (TabBar would otherwise split the width evenly and elide).
                TabButton {
                    width: implicitWidth
                    text: headerRow.ultraCompact ? "🎭" : (headerRow.compact ? qsTr("🎭 PERSONAJE") : qsTr("🎭 PERSONALIZAR PERSONAJE"))
                    ToolTip.visible: hovered && headerRow.ultraCompact
                    ToolTip.text: qsTr("Personalizar personaje")
                }
                TabButton {
                    width: implicitWidth
                    text: headerRow.ultraCompact ? "👗" : (headerRow.compact ? qsTr("👗 ATUENDOS") : qsTr("👗 ESTUDIO DE ATUENDOS"))
                    ToolTip.visible: hovered && headerRow.ultraCompact
                    ToolTip.text: qsTr("Estudio de atuendos")
                }
                TabButton {
                    width: implicitWidth
                    text: headerRow.ultraCompact ? "⚒" : (headerRow.compact ? "FORGE" : "CHARACTER FORGE")
                    ToolTip.visible: hovered && headerRow.ultraCompact
                    ToolTip.text: qsTr("Character Forge")
                }
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

            ToolButton {
                id: layoutPresetButton
                objectName: "layoutPresetButton"
                text: headerRow.compact ? "🗔" : "🗔 " + qsTr("Distribución")
                implicitHeight: Theme.controlHeight
                onClicked: layoutPresetMenu.open()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Alternar distribuciones de paneles predefinidas y guardadas")
                Menu {
                    id: layoutPresetMenu
                    y: layoutPresetButton.height
                    Repeater {
                        model: Workbench.builtinLayouts
                        MenuItem {
                            text: modelData.name
                            onTriggered: Workbench.applyBuiltinLayout(modelData.key, window.workspaceKeys[workspaceTabs.currentIndex] ?? "creator")
                        }
                    }
                    MenuSeparator { visible: Workbench.savedLayouts.length > 0 }
                    Menu {
                        title: qsTr("Guardadas")
                        visible: Workbench.savedLayouts.length > 0
                        Repeater {
                            model: Workbench.savedLayouts
                            MenuItem {
                                text: modelData
                                onTriggered: Workbench.applyLayout(modelData)
                            }
                        }
                    }
                    MenuSeparator {}
                    MenuItem {
                        text: qsTr("Guardar distribución actual…")
                        onTriggered: saveLayoutDialog.open()
                    }
                    MenuItem {
                        text: qsTr("Restablecer de fábrica")
                        onTriggered: window.layout.reset()
                    }
                }
            }

            Item { Layout.fillWidth: true }

            ToolButton {
                objectName: "settingsButton"
                text: headerRow.compact ? "⚙" : "⚙ " + qsTr("Configuración")
                implicitHeight: Theme.controlHeight
                Accessible.name: qsTr("Configuración")
                onClicked: settingsCenter.openAt("", "")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Tema, escala, distribuciones, visor 3D, retratos y más (Ctrl+,)")
            }

            // Botón de exportación principal
            Button {
                objectName: "headerExportButton"
                text: headerRow.ultraCompact ? qsTr("🚀") : (headerRow.compact ? qsTr("🚀 Exportar") : qsTr("🚀 Exportar Mod…"))
                highlighted: true
                enabled: window.ready && workspaceTabs.currentIndex !== 2
                implicitHeight: Theme.controlHeight + Theme.spacingXs
                font.bold: true
                ToolTip.visible: (hovered && workspaceTabs.currentIndex === 2) || (hovered && headerRow.ultraCompact)
                ToolTip.text: workspaceTabs.currentIndex === 2 ? qsTr("La exportación de NPC nuevos llegará en Fase 2") : qsTr("Exportar mod completo / rostro")
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
                                objectName: "outfitNpcBrowser"
                                studio: window.studio
                                // The editor of the selected NPC is the character workspace.
                                onEditRequested: workspaceTabs.currentIndex = 0
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
