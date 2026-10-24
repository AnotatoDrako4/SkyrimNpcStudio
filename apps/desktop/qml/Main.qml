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

    // Which side panel is on screen: wanted by the user, not put away for lack of room, not in focus mode.
    function panelOnScreen(which) {
        if (layout.maximized) return false
        if (which === "left") return layout.leftVisible && !layout.leftAutoHidden
        if (which === "right") return layout.rightVisible && !layout.rightAutoHidden
        return layout.bottomVisible
    }
    // The panel the user asked for but the window has no room for (it is put away on screen only).
    function panelAutoHidden(which) {
        return !layout.maximized && (which === "left" ? layout.leftAutoHidden : which === "right" ? layout.rightAutoHidden : false)
    }

    // Show or hide one panel of the current workspace; showing one leaves the focused viewer. When the window
    // cannot hold both side panels, the one that was asked for wins and the other gives way (that is the
    // user's own action, so the other panel is hidden as a choice and comes back with its shortcut).
    function setPanel(which, shown) {
        if (layout.maximized) {
            layout.maximized = false
            if (!shown)
                return
        }
        if (which === "left") {
            layout.leftVisible = shown
            if (shown && layout.leftAutoHidden) layout.rightVisible = false
        } else if (which === "right") {
            layout.rightVisible = shown
            if (shown && layout.rightAutoHidden) layout.leftVisible = false
        } else {
            layout.bottomVisible = shown
        }
    }
    // Save the project of the workspace on screen (.sncf in Character Forge, .snps elsewhere): header button,
    // Proyecto › Guardar (Ctrl+S) and the command palette all call this one.
    readonly property bool canSave: workspaceTabs.currentIndex === 2 ? studio.forge.hasProject : studio.outfit.npcFormKey.length > 0
    function saveCurrentProject() {
        if (workspaceTabs.currentIndex === 2) {
            studio.forge.projectPath.length > 0 ? studio.forge.saveProject(studio.forge.projectPath) : forgeSaveDialog.open()
        } else {
            studio.outfit.projectPath.length > 0 ? studio.outfit.saveProject(studio.outfit.projectPath, "") : saveProjectDialog.open()
        }
    }
    function openCurrentProject() { workspaceTabs.currentIndex === 2 ? forgeOpenDialog.open() : openProjectDialog.open() }
    // "Buscar NPC" / "Ver prendas" from anywhere: the workspace that has an explorer shows it, search focused.
    function searchNpc() {
        if (workspaceTabs.currentIndex === 1) { setPanel("left", true); outfitLeftTabs.currentIndex = 1; outfitNpcBrowser.focusSearch() }
        else { workspaceTabs.currentIndex = 0; creatorWorkspace.searchNpc() }
    }
    function showCatalog() {
        if (workspaceTabs.currentIndex === 1) { setPanel("left", true); outfitLeftTabs.currentIndex = 0; outfitArmorCatalog.focusSearch() }
        else { workspaceTabs.currentIndex = 0; creatorWorkspace.showCatalog() }
    }

    // Commands of the palette (Ctrl+Shift+P) and of the shortcut list: each one calls the handler its menu entry
    // or button calls, and is enabled exactly when that entry is.
    readonly property var commands: {
        const list = [
            { id: "ws.creator", category: qsTr("Workspace"), label: qsTr("Ir a Personalizar personaje"), shortcut: "", run: () => workspaceTabs.currentIndex = 0 },
            { id: "ws.outfit", category: qsTr("Workspace"), label: qsTr("Ir a Estudio de atuendos"), shortcut: "", run: () => workspaceTabs.currentIndex = 1 },
            { id: "ws.forge", category: qsTr("Workspace"), label: qsTr("Ir a Character Forge"), shortcut: "", run: () => workspaceTabs.currentIndex = 2 },
            { id: "panel.left", category: qsTr("Vista"), label: panelOnScreen("left") ? qsTr("Ocultar el panel izquierdo") : qsTr("Mostrar el panel izquierdo"), shortcut: "Ctrl+B", run: () => setPanel("left", !panelOnScreen("left")) },
            { id: "panel.right", category: qsTr("Vista"), label: panelOnScreen("right") ? qsTr("Ocultar el panel derecho") : qsTr("Mostrar el panel derecho"), shortcut: "Ctrl+Alt+B", run: () => setPanel("right", !panelOnScreen("right")) },
            { id: "panel.bottom", category: qsTr("Vista"), label: panelOnScreen("bottom") ? qsTr("Ocultar la consola y diagnósticos") : qsTr("Mostrar la consola y diagnósticos"), shortcut: "Ctrl+J", run: () => setPanel("bottom", !panelOnScreen("bottom")) },
            { id: "view.focus", category: qsTr("Vista"), label: layout.maximized ? qsTr("Salir del enfoque del visor") : qsTr("Enfocar el visor"), shortcut: "F11", run: () => layout.maximized = !layout.maximized },
            { id: "layout.reset", category: qsTr("Vista"), label: qsTr("Restablecer paneles de este workspace"), shortcut: "", run: () => layout.reset() },
            { id: "layout.save", category: qsTr("Vista"), label: qsTr("Guardar la distribución actual…"), shortcut: "", run: () => saveLayoutDialog.open() },
            { id: "search.npc", category: qsTr("Buscar"), label: qsTr("Buscar NPC"), shortcut: "", enabled: ready, run: () => searchNpc() },
            { id: "search.armor", category: qsTr("Buscar"), label: qsTr("Buscar prendas en el catálogo"), shortcut: "", enabled: ready, run: () => showCatalog() },
            { id: "project.open", category: qsTr("Proyecto"), label: workspaceTabs.currentIndex === 2 ? qsTr("Abrir proyecto Character Forge (.sncf)…") : qsTr("Abrir proyecto (.snps)…"), shortcut: "", enabled: workspaceTabs.currentIndex === 2 || ready, run: () => openCurrentProject() },
            { id: "project.save", category: qsTr("Proyecto"), label: qsTr("Guardar proyecto"), shortcut: "Ctrl+S", enabled: canSave, run: () => saveCurrentProject() },
            { id: "edit.undo", category: qsTr("Editar"), label: qsTr("Deshacer"), shortcut: "Ctrl+Z", enabled: workspaceTabs.currentIndex === 2 ? studio.forge.canUndo : studio.outfit.canUndo, run: () => workspaceTabs.currentIndex === 2 ? studio.forge.undo() : studio.outfit.undo() },
            { id: "edit.redo", category: qsTr("Editar"), label: qsTr("Rehacer"), shortcut: "Ctrl+Y", enabled: workspaceTabs.currentIndex === 2 ? studio.forge.canRedo : studio.outfit.canRedo, run: () => workspaceTabs.currentIndex === 2 ? studio.forge.redo() : studio.outfit.redo() },
            { id: "forge.new", category: qsTr("Forge"), label: qsTr("Nuevo proyecto Character Forge"), shortcut: "", enabled: !studio.forge.busy, run: () => { workspaceTabs.currentIndex = 2; forgeWorkspace.newProject() } },
            { id: "forge.validate", category: qsTr("Forge"), label: qsTr("Validar el NPC del proyecto"), shortcut: "", enabled: studio.forge.hasProject && studio.forge.catalogReady && !studio.forge.busy, run: () => { workspaceTabs.currentIndex = 2; studio.forge.validateNpc() } },
            { id: "scale.in", category: qsTr("Escala"), label: qsTr("Aumentar la escala de interfaz"), shortcut: "Ctrl++", run: () => Workbench.zoomIn() },
            { id: "scale.out", category: qsTr("Escala"), label: qsTr("Reducir la escala de interfaz"), shortcut: "Ctrl+-", run: () => Workbench.zoomOut() },
            { id: "scale.reset", category: qsTr("Escala"), label: qsTr("Escala de interfaz al 100 %"), shortcut: "Ctrl+0", run: () => Workbench.zoomReset() },
            { id: "scale.prefs", category: qsTr("Escala"), label: qsTr("Preferencias de apariencia…"), shortcut: "", run: () => scaleDialog.open() },
            { id: "settings.open", category: qsTr("Configuración"), label: qsTr("Abrir Configuración"), shortcut: "Ctrl+,", run: () => settingsCenter.openAt("", "") },
            { id: "settings.theme", category: qsTr("Configuración"), label: qsTr("Tema y colores…"), shortcut: "", run: () => settingsCenter.openAt("appearance", "appearance.theme") },
            { id: "tools.reindex", category: qsTr("Herramientas"), label: qsTr("Reindexar perfil"), shortcut: "F5", enabled: studio.selectedProfile.length > 0 && studio.phase !== StudioController.Scanning && studio.phase !== StudioController.Indexing, run: () => studio.reindex() },
            { id: "export.mod", category: qsTr("Exportar"), label: qsTr("Exportar mod completo / rostro…"), shortcut: "Ctrl+Shift+E", enabled: workspaceTabs.currentIndex !== 2 && ready && !studio.appearance.exporting, run: () => exportWizardDialog.open() },
            { id: "help.shortcuts", category: qsTr("Ayuda"), label: qsTr("Atajos de teclado"), shortcut: "", run: () => shortcutsDialog.open() }
        ]
        for (const p of Workbench.readabilityPresets)
            list.push({ id: "readability." + p.key, category: qsTr("Legibilidad"), label: qsTr("Presentación «%1»: %2").arg(p.name).arg(p.detail), shortcut: "",
                        run: () => Workbench.applyReadabilityPreset(p.key) })
        for (const b of layoutsFor(workspaceKey))
            list.push({ id: "layout." + b.key, category: qsTr("Distribución"), label: b.name, shortcut: "",
                        run: () => Workbench.applyBuiltinLayout(b.key, workspaceKey) })
        return list
    }

    // Built-in layouts that make sense for a workspace ("" in the table = every workspace).
    function layoutsFor(workspace) {
        return Workbench.builtinLayouts.filter(b => b.workspace === "" || b.workspace === workspace)
    }
    readonly property string workspaceKey: workspaceKeys[workspaceTabs.currentIndex] ?? "creator"
    readonly property var workspaceInfo: [
        { glyph: "person", full: qsTr("Personalizar personaje"), brief: qsTr("Personaje") },
        { glyph: "tag", full: qsTr("Estudio de atuendos"), brief: qsTr("Atuendos") },
        { glyph: "forge", full: qsTr("Character Forge"), brief: qsTr("Forge") }
    ]
    // Where the user is working: the NPC of the two NPC workspaces, the project of Character Forge.
    readonly property string contextText: workspaceTabs.currentIndex === 2
        ? (studio.forge.hasProject ? (studio.forge.projectName || qsTr("Proyecto sin nombre")) : qsTr("Sin proyecto Forge"))
        : (studio.outfit.npcLabel.length > 0 ? studio.outfit.npcLabel : qsTr("Ningún NPC seleccionado"))
    readonly property bool contextDirty: workspaceTabs.currentIndex === 2
        ? studio.forge.dirty : (studio.appearance.dirty || studio.outfit.dirty)

    // The tabs of the workspaces: glyph + name, the name gives way to a short one and then to the glyph alone
    // (tooltip and accessible name always carry the full name).
    component WorkspaceTab: TabButton {
        id: tab
        property string glyph
        property string full
        property string brief
        width: implicitWidth
        implicitHeight: Theme.controlHeight + Theme.spacingXs
        leftPadding: Theme.sp(12)
        rightPadding: Theme.sp(12)
        text: headerRow.ultraCompact ? "" : (headerRow.compact ? brief : full)
        Accessible.name: full
        ToolTip.visible: hovered && (headerRow.compact || visualFocus)
        ToolTip.delay: 450
        ToolTip.text: full
        contentItem: Row {
            spacing: Theme.sp(7)
            Icon { name: tab.glyph; color: tab.checked ? Theme.accent : Theme.textDim; anchors.verticalCenter: parent.verticalCenter }
            Text {
                visible: tab.text.length > 0
                text: tab.text
                color: tab.checked ? Theme.text : Theme.textDim
                font.family: Theme.fontFamily
                font.pixelSize: Theme.normal
                font.bold: tab.checked
                anchors.verticalCenter: parent.verticalCenter
            }
        }
        background: Rectangle {
            radius: Theme.radius
            color: tab.checked ? Theme.panelAlt : tab.down ? Theme.accentDim : tab.hovered ? Theme.accentSubtle : "transparent"
            border.width: tab.visualFocus ? Theme.focusWidth : 0
            border.color: Theme.borderFocus
            Rectangle {
                visible: tab.checked
                anchors.bottom: parent.bottom
                anchors.horizontalCenter: parent.horizontalCenter
                width: parent.width - Theme.sp(16)
                height: 2
                radius: 1
                color: Theme.accent
            }
        }
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
        // The text sits in a layout: a wrapped Label as the dialog's only child loops its implicitHeight when the font
        // scale changes (reproduced with plain Qt 6.11).
        ColumnLayout {
            anchors.fill: parent
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: [window.studio.outfit.dirty ? qsTr("• Atuendo (.snps) modificado") : "",
                       window.studio.appearance.dirty ? qsTr("• Rostro o altura del NPC editados") : "",
                       window.studio.forge.dirty ? qsTr("• Proyecto Character Forge (.sncf) modificado") : ""]
                      .filter(s => s.length > 0).join("\n") + "\n\n" + qsTr("Si sales ahora se perderán. Nada se ha escrito en Skyrim ni en MO2.")
            }
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
                enabled: window.canSave
                onTriggered: window.saveCurrentProject()
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
                checked: window.panelOnScreen("left")
                onTriggered: window.setPanel("left", checked)
            }
            Action {
                text: workspaceTabs.currentIndex === 2 ? qsTr("Panel contextual") : qsTr("Panel de propiedades")
                shortcut: "Ctrl+Alt+B"
                checkable: true
                checked: window.panelOnScreen("right")
                onTriggered: window.setPanel("right", checked)
            }
            Action {
                text: qsTr("Consola y diagnósticos")
                shortcut: "Ctrl+J"
                checkable: true
                checked: window.panelOnScreen("bottom")
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
                    model: window.layoutsFor(window.workspaceKey)
                    MenuItem {
                        text: modelData.name
                        onTriggered: Workbench.applyBuiltinLayout(modelData.key, window.workspaceKey)
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
                    objectName: "resetPanelsAction"
                    text: qsTr("Restablecer paneles de este workspace")
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
            title: qsTr("Ay&uda")
            Action {
                objectName: "commandPaletteAction"
                text: qsTr("Paleta de comandos…  (Ctrl+Shift+P)")
                onTriggered: commandPalette.openPalette(window.activeFocusItem)
            }
            Action {
                text: qsTr("Atajos de teclado…")
                onTriggered: shortcutsDialog.open()
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

    // Command palette: the same commands as the menus and buttons (window.commands).
    CommandPalette {
        id: commandPalette
        commands: window.commands
    }
    Shortcut {
        objectName: "commandPaletteShortcut"
        sequence: "Ctrl+Shift+P"
        context: Qt.ApplicationShortcut
        onActivated: commandPalette.opened ? commandPalette.close() : commandPalette.openPalette(window.activeFocusItem)
    }
    // Shortcuts that are really bound (taken from the command list), never a list written by hand.
    Dialog {
        id: shortcutsDialog
        objectName: "shortcutsDialog"
        title: qsTr("Atajos de teclado")
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(Theme.px(560), window.width - 40)
        height: Math.min(Theme.px(560), window.height - 80)
        standardButtons: Dialog.Close
        ListView {
            anchors.fill: parent
            clip: true
            model: [{ category: qsTr("Ayuda"), label: qsTr("Paleta de comandos"), shortcut: "Ctrl+Shift+P" }]
                   .concat(window.commands.filter(c => (c.shortcut || "").length > 0))
                   .concat([{ category: qsTr("Explorador"), label: qsTr("Lista/galería: Espacio selecciona, Enter abre o equipa, Ctrl+C copia el FormKey"), shortcut: "" },
                            { category: qsTr("Divisores"), label: qsTr("Doble clic en un divisor: tamaño predeterminado de ese panel"), shortcut: "" }])
            ScrollBar.vertical: ScrollBar {}
            delegate: RowLayout {
                required property var modelData
                width: ListView.view.width
                height: Theme.rowHeight - Theme.sp(6)
                Text { text: modelData.category; color: Theme.textMuted; font.pixelSize: Theme.small; Layout.preferredWidth: Theme.px(110) }
                Text { text: modelData.label; color: Theme.text; font.pixelSize: Theme.normal; elide: Text.ElideRight; Layout.fillWidth: true }
                Text { text: modelData.shortcut; color: Theme.textDim; font.family: Theme.mono; font.pixelSize: Theme.small }
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

    // ---------------------------------------------------------------- CABECERA
    // Workspaces, where the user is working, the project actions, the three panels and the two main actions.
    // Labels give way before anything leaves the window: first the secondary ones (glyph + tooltip), then the
    // project actions move into "Más". Every action keeps its glyph, tooltip and accessible name.
    header: ToolBar {
        implicitHeight: Theme.toolbarHeight
        background: Rectangle {
            color: Theme.header
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }
        }

        RowLayout {
            id: headerRow
            objectName: "headerRow"
            // The window width in design pixels (100 % scale; the legible text floor counts as scale).
            readonly property real designWidth: window.width / Math.max(Theme.uiFactor, Theme.normal / 13)
            readonly property bool compact: designWidth < 1500
            readonly property bool ultraCompact: designWidth < 900
            anchors.fill: parent
            anchors.leftMargin: ultraCompact ? Theme.spacingSm : Theme.spacingLg
            anchors.rightMargin: ultraCompact ? Theme.spacingSm : Theme.spacingLg
            spacing: ultraCompact ? Theme.spacingXs : Theme.spacingMd

            // Modos de trabajo principales
            TabBar {
                id: workspaceTabs
                objectName: "workspaceTabs"
                implicitHeight: Theme.controlHeight + Theme.spacingXs
                background: Item {}
                spacing: Theme.spacingXs
                // The last workspace comes back on start, or the fixed one chosen in Configuración › General
                // (automation may still pick another one).
                Component.onCompleted: {
                    const fixed = ["creator", "outfit", "forge"].indexOf(AppSettings.startWorkspace)
                    currentIndex = fixed >= 0 ? fixed : Workbench.workspace
                }
                onCurrentIndexChanged: Workbench.workspace = currentIndex
                WorkspaceTab { glyph: window.workspaceInfo[0].glyph; full: window.workspaceInfo[0].full; brief: window.workspaceInfo[0].brief }
                WorkspaceTab { glyph: window.workspaceInfo[1].glyph; full: window.workspaceInfo[1].full; brief: window.workspaceInfo[1].brief }
                WorkspaceTab { glyph: window.workspaceInfo[2].glyph; full: window.workspaceInfo[2].full; brief: window.workspaceInfo[2].brief }
            }

            Rectangle { width: 1; height: Theme.px(22); color: Theme.border; visible: !headerRow.ultraCompact }

            // Acciones de proyecto (en ventanas muy estrechas pasan al menú «Más»)
            IconToolButton {
                id: openButton
                objectName: "headerOpenButton"
                visible: !headerRow.ultraCompact
                glyph: "folder-open"
                label: qsTr("Abrir")
                showLabel: !headerRow.compact
                enabled: workspaceTabs.currentIndex === 2 || window.ready
                tip: workspaceTabs.currentIndex === 2 ? qsTr("Abrir un proyecto Character Forge (.sncf)") : qsTr("Abrir un proyecto de atuendo (.snps)")
                onClicked: window.openCurrentProject()
            }

            IconToolButton {
                id: saveButton
                objectName: "headerSaveButton"
                visible: !headerRow.ultraCompact
                glyph: "save"
                label: qsTr("Guardar")
                showLabel: !headerRow.compact
                shortcutText: "Ctrl+S"
                enabled: window.canSave
                tip: workspaceTabs.currentIndex === 2 ? qsTr("Guardar el proyecto independiente .sncf") : qsTr("Guardar el proyecto de atuendo .snps")
                onClicked: window.saveCurrentProject()
            }

            Rectangle { width: 1; height: Theme.px(22); color: Theme.border; visible: !headerRow.ultraCompact }

            // Dónde se trabaja: el NPC o el proyecto, y si tiene cambios sin guardar.
            RowLayout {
                id: contextRow
                objectName: "headerContext"
                visible: !headerRow.ultraCompact
                spacing: Theme.spacingSm
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Text {
                    id: contextLabel
                    objectName: "headerContextLabel"
                    text: window.contextText
                    color: Theme.text
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.normal
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Accessible.name: qsTr("Trabajando en: %1").arg(window.contextText)
                    ToolTip.visible: contextHover.hovered && truncated
                    ToolTip.delay: 450
                    ToolTip.text: window.contextText
                    HoverHandler { id: contextHover }
                }
                StatusBadge {
                    visible: window.contextDirty
                    text: workspaceTabs.currentIndex === 2 ? qsTr("SIN GUARDAR") : qsTr("MODIFICADO")
                    tone: Theme.warn
                    Layout.alignment: Qt.AlignVCenter
                }
            }
            Item { visible: headerRow.ultraCompact; Layout.fillWidth: true }

            // Visor a pantalla completa dentro de la ventana; la escena y la selección no cambian.
            IconToolButton {
                objectName: "focusViewerButton"
                glyph: window.layout.maximized ? "exit-fullscreen" : "fullscreen"
                label: window.layout.maximized ? qsTr("Salir del enfoque") : qsTr("Enfocar visor")
                showLabel: !headerRow.compact
                shortcutText: "F11"
                checkable: true
                checked: window.layout.maximized
                tip: qsTr("Oculta los paneles laterales y la consola. Vuelve a la distribución anterior al salir.")
                onClicked: window.layout.maximized = !window.layout.maximized
            }

            // Los tres paneles: explorador, inspector y consola.
            Row {
                spacing: 0
                visible: !headerRow.ultraCompact
                Repeater {
                    model: [
                        { key: "left", glyph: "panel-left", name: workspaceTabs.currentIndex === 2 ? qsTr("Panel de herramientas") : qsTr("Panel de exploración"), shortcut: "Ctrl+B" },
                        { key: "right", glyph: "panel-right", name: workspaceTabs.currentIndex === 2 ? qsTr("Panel contextual") : qsTr("Panel de propiedades"), shortcut: "Ctrl+Alt+B" },
                        { key: "bottom", glyph: "panel-bottom", name: qsTr("Consola y diagnósticos"), shortcut: "Ctrl+J" }
                    ]
                    delegate: IconToolButton {
                        required property var modelData
                        objectName: "toggle_" + modelData.key + "_panel"
                        glyph: modelData.glyph
                        label: modelData.name
                        showLabel: false
                        shortcutText: modelData.shortcut
                        checkable: true
                        checked: window.panelOnScreen(modelData.key)
                        tone: window.panelAutoHidden(modelData.key) ? Theme.warn : Theme.text
                        tip: window.panelAutoHidden(modelData.key)
                             ? qsTr("%1: oculto por falta de espacio en la ventana. Clic para mostrarlo (el otro panel lateral cede).").arg(modelData.name)
                             : (checked ? qsTr("Ocultar: %1").arg(modelData.name) : qsTr("Mostrar: %1").arg(modelData.name))
                        onClicked: window.setPanel(modelData.key, !window.panelOnScreen(modelData.key))
                    }
                }
            }

            IconToolButton {
                id: layoutPresetButton
                objectName: "layoutPresetButton"
                visible: !headerRow.ultraCompact
                glyph: "layout"
                label: qsTr("Distribución")
                showLabel: !headerRow.compact
                tip: qsTr("Distribuciones de paneles para cada tarea, y las que guardaste")
                onClicked: layoutPresetMenu.open()
                Menu {
                    id: layoutPresetMenu
                    y: layoutPresetButton.height
                    Repeater {
                        model: window.layoutsFor(window.workspaceKey)
                        MenuItem {
                            text: modelData.name
                            onTriggered: Workbench.applyBuiltinLayout(modelData.key, window.workspaceKey)
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
                        text: qsTr("Restablecer paneles de este workspace")
                        onTriggered: window.layout.reset()
                    }
                }
            }

            // Menú «Más»: lo que sale de la barra en ventanas muy estrechas.
            IconToolButton {
                id: moreButton
                objectName: "headerMoreButton"
                visible: headerRow.ultraCompact
                glyph: "more"
                label: qsTr("Más acciones")
                showLabel: false
                onClicked: moreMenu.open()
                Menu {
                    id: moreMenu
                    y: moreButton.height
                    MenuItem {
                        text: workspaceTabs.currentIndex === 2 ? qsTr("Abrir Character Forge (.sncf)…") : qsTr("Abrir proyecto (.snps)…")
                        enabled: openButton.enabled
                        onTriggered: openButton.clicked()
                    }
                    MenuItem { text: qsTr("Guardar proyecto"); enabled: saveButton.enabled; onTriggered: saveButton.clicked() }
                    MenuSeparator {}
                    MenuItem { text: qsTr("Panel izquierdo (Ctrl+B)"); checkable: true; checked: window.panelOnScreen("left"); onTriggered: window.setPanel("left", checked) }
                    MenuItem { text: qsTr("Panel derecho (Ctrl+Alt+B)"); checkable: true; checked: window.panelOnScreen("right"); onTriggered: window.setPanel("right", checked) }
                    MenuItem { text: qsTr("Consola (Ctrl+J)"); checkable: true; checked: window.panelOnScreen("bottom"); onTriggered: window.setPanel("bottom", checked) }
                    MenuSeparator {}
                    Menu {
                        title: qsTr("Distribución")
                        Repeater {
                            model: window.layoutsFor(window.workspaceKey)
                            MenuItem {
                                text: modelData.name
                                onTriggered: Workbench.applyBuiltinLayout(modelData.key, window.workspaceKey)
                            }
                        }
                    }
                }
            }

            Rectangle { width: 1; height: Theme.px(22); color: Theme.border; visible: !headerRow.ultraCompact }

            IconToolButton {
                objectName: "settingsButton"
                glyph: "settings"
                label: qsTr("Configuración")
                showLabel: !headerRow.compact
                shortcutText: "Ctrl+,"
                tip: qsTr("Tema, escala, distribuciones, visor 3D, retratos y más")
                onClicked: settingsCenter.openAt("", "")
            }

            // Botón de exportación principal
            IconToolButton {
                objectName: "headerExportButton"
                glyph: "export"
                label: headerRow.compact ? qsTr("Exportar") : qsTr("Exportar mod…")
                showLabel: !headerRow.ultraCompact
                emphasized: true
                font.bold: true
                enabled: window.ready && workspaceTabs.currentIndex !== 2
                implicitHeight: Theme.controlHeight + Theme.spacingXs
                tip: workspaceTabs.currentIndex === 2
                     ? qsTr("No disponible en Character Forge: la exportación de NPC nuevos está bloqueada hasta validar FaceGen nativo.")
                     : (window.ready ? qsTr("Exportar el mod completo o solo el rostro") : qsTr("Carga un perfil de MO2 para poder exportar"))
                shortcutText: workspaceTabs.currentIndex === 2 ? "" : "Ctrl+Shift+E"
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
        handle: SplitHandleBar {
            vertical: true
            onResetRequested: window.layout.bottomHeight = window.layout.defaultBottomHeight
        }

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
                id: creatorWorkspace
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
                            id: outfitLeftTabs
                            Layout.fillWidth: true
                            TabButton { text: qsTr("Prendas") }
                            TabButton { text: qsTr("NPCs") }
                            TabButton { text: qsTr("Mods") }
                        }

                        StackLayout {
                            currentIndex: outfitLeftTabs.currentIndex
                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            ArmorCatalog {
                                id: outfitArmorCatalog
                                studio: window.studio
                                onPreviewRequested: (formKey) => {
                                    window.studio.armorPreview.selectArmor(formKey)
                                }
                            }

                            NpcBrowser {
                                id: outfitNpcBrowser
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
                        onCatalogRequested: window.showCatalog()
                        onNpcSearchRequested: window.searchNpc()
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
                id: forgeWorkspace
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
        Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.border }

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
                font.family: Theme.fontFamily
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
                id: summaryText
                visible: window.ready
                text: qsTr("%1 NPC · %2 armaduras · %3 plugins · %4 archivos")
                      .arg(window.studio.summary.npcs ?? 0).arg(window.studio.summary.armors ?? 0)
                      .arg(window.studio.summary.activePlugins ?? 0).arg(window.studio.summary.files ?? 0)
                color: Theme.textDim
                font.family: Theme.fontFamily
                font.pixelSize: Theme.small
                // Gives way (elided) before the scale and console buttons leave a narrow window.
                elide: Text.ElideRight
                Layout.fillWidth: true
                Layout.maximumWidth: implicitWidth
                ToolTip.visible: summaryHover.hovered && truncated
                ToolTip.text: text
                HoverHandler { id: summaryHover }
            }

            // Text held at its legible minimum: the preference stays as chosen, and this says so.
            StatusBadge {
                objectName: "legibilityBadge"
                visible: Theme.legibilityFloor
                text: qsTr("TEXTO AL MÍNIMO LEGIBLE")
                tone: Theme.warn
                ToolTip.visible: legibilityHover.hovered
                ToolTip.text: qsTr("La escala %1 % con texto %2 % haría el texto demasiado pequeño: se mantiene en %3 px. Tu preferencia no cambia. Clic para ajustarla.")
                              .arg(Workbench.uiScale).arg(Workbench.fontScale).arg(Theme.normal)
                HoverHandler { id: legibilityHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: scaleDialog.open() }
            }

            Rectangle { width: 1; height: Theme.px(16); color: Theme.borderSubtle }

            // Escala actual; un clic abre las preferencias de apariencia.
            IconToolButton {
                objectName: "scaleStatusButton"
                Accessible.name: qsTr("Escala de interfaz: %1").arg(label)
                label: Workbench.uiScale + " %" + (Workbench.fontScale !== 100 ? " · " + qsTr("texto %1 %").arg(Workbench.fontScale) : "")
                glyph: "zoom-in"
                implicitHeight: Theme.statusHeight - 6
                font.pixelSize: Theme.small
                tip: qsTr("Escala de interfaz y tamaño del texto (Ctrl++ / Ctrl+- / Ctrl+0)")
                onClicked: scaleDialog.open()
            }

            Rectangle { width: 1; height: Theme.px(16); color: Theme.borderSubtle }

            // Botón de alternancia de Consola y Diagnósticos
            IconToolButton {
                objectName: "consoleToggleButton"
                glyph: "console"
                label: qsTr("Consola y diagnósticos") + (window.studio.diagnostics.count > 0 ? " (" + window.studio.diagnostics.count + ")" : "")
                checkable: true
                checked: mainSplit.bottomShown
                implicitHeight: Theme.statusHeight - 6
                shortcutText: "Ctrl+J"
                tip: qsTr("Mostrar u ocultar la consola y los diagnósticos técnicos")
                onClicked: window.setPanel("bottom", !mainSplit.bottomShown)
            }
        }
    }
}
