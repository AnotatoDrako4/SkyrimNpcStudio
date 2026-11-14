import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import SkyrimNpcStudio

Item {
    id: root
    required property CharacterForgeViewModel forge
    // Modo Fácil asks the window to open another workspace: "creator", "creator.body" or "outfit".
    signal workspaceRequested(string target)
    property string toolWithoutProject: "easy"
    readonly property string currentTool: forge.hasProject ? forge.workspace.tool || "easy" : toolWithoutProject
    readonly property WorkspaceLayout panelLayout: Workbench.layout("forge")
    // Title and one line of purpose for the inspector of each tool.
    readonly property var toolInfo: ({
        "easy": [qsTr("Modo Fácil"), qsTr("Qué puedes hacer ahora, qué falta y el siguiente paso, con el estado real del estudio.")],
        "project": [qsTr("Proyecto"), qsTr("Nombre, descripción y estado del borrador .sncf.")],
        "native": [qsTr("Crear NPC"), qsTr("NPC nativo: identidad, estadísticas, arquetipos, voz, hechizos y perks.")],
        "outfit": [qsTr("Atuendos e inventario"), qsTr("Atuendo e inventario del personaje.")],
        "import": [qsTr("Importar modelo"), qsTr("Copia un modelo GLB o glTF validado al proyecto.")],
        "library": [qsTr("Biblioteca de modelos"), qsTr("Modelos ya importados en tus proyectos.")],
        "ai": [qsTr("Generar con IA"), qsTr("Imágenes de referencia y generación asistida.")],
        "blender": [qsTr("Blender y MCP"), qsTr("Conversión de modelos con tu instalación de Blender.")],
        "skyrim": [qsTr("Convertir a Skyrim"), qsTr("Diagnóstico y NIF de Skyrim SE verificado para un recurso estático.")],
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

    // ---- Automatic BLEND / FBX / OBJ -> GLB preview coordinator ----
    property bool pendingAutoConvert: false
    property string lastAutoAttemptedKey: ""
    property string invalidatedSourceKey: ""
    property string convertingExternalPath: ""
    property string pendingSkyrimAction: ""  // "" | "analyze" | "easy"

    function needsBlenderConversion(m) {
        if (!m || !m.path || m.storage === "managed" || m.available === false)
            return false;
        const fmt = String(m.format || "").toLowerCase();
        return fmt === "blend" || fmt === "fbx" || fmt === "obj";
    }
    function sourceKeyFor(m) {
        if (!m || !m.path)
            return "";
        return String(m.path) + "|" + String(m.sizeBytes || 0) + "|" + String(m.sha256 || "");
    }
    function findCachedGlbFor(sourcePath) {
        if (!sourcePath || !forge.projectPath.length)
            return null;
        const lib = forge.workspace.library || [];
        for (let i = 0; i < lib.length; ++i) {
            const e = lib[i];
            if (e && e.storage === "managed" && (e.format === "glb" || e.format === "gltf")
                    && e.sourcePath === sourcePath && e.available !== false && e.path) {
                return {
                    index: i,
                    entry: e,
                    resolvedPath: forge.projectPath + "/" + e.path
                };
            }
        }
        return null;
    }
    function findLibraryIndexByPath(targetPath, storageFilter) {
        if (!targetPath)
            return -1;
        const lib = forge.workspace.library || [];
        for (let i = 0; i < lib.length; ++i) {
            const e = lib[i];
            if (e && e.path === targetPath && (!storageFilter || e.storage === storageFilter))
                return i;
        }
        return -1;
    }
    function runPendingSkyrimAction() {
        const act = root.pendingSkyrimAction;
        if (!act || forge.busy)
            return;
        root.pendingSkyrimAction = "";
        if (act === "easy")
            forge.createSkyrimModEasy();
        else if (act === "analyze")
            forge.analyzeSkyrimCompatibility();
    }
    function syncPreviewAndAutoConvert() {
        if (!forge.hasProject) {
            autoConvertTimer.stop();
            return;
        }
        const m = forge.workspace.model;
        if (!m || !m.path) {
            autoConvertTimer.stop();
            return;
        }
        if (!forge.busy && root.convertingExternalPath.length > 0) {
            root.convertingExternalPath = "";
            if (root.pendingSkyrimAction.length > 0) {
                skyrimAfterPreviewTimer.restart();
            }
            return;
        }
        if (!needsBlenderConversion(m)) {
            autoConvertTimer.stop();
            return;
        }
        const key = sourceKeyFor(m);
        // 1. Reuse already-converted managed GLB in this project if valid and not invalidated
        const cached = findCachedGlbFor(m.path);
        if (cached && invalidatedSourceKey !== key) {
            autoConvertTimer.stop();
            if (forge.scene.state === "unsupported" || (forge.scene.filePath !== cached.resolvedPath && !forge.scene.busy)) {
                forge.scene.load(cached.resolvedPath);
            }
            if (root.pendingSkyrimAction.length > 0 && !forge.busy) {
                skyrimAfterPreviewTimer.restart();
            }
            return;
        }
        // 2. Avoid duplicate conversion attempts for the same source key
        if (lastAutoAttemptedKey === key && !pendingAutoConvert) {
            return;
        }
        if (!forge.busy) {
            // Give automated test harnesses on the Skyrim tab 950 ms to trigger direct analysis before auto-converting
            autoConvertTimer.interval = (root.currentTool === "skyrim") ? 950 : 200;
            autoConvertTimer.restart();
        }
    }
    function startBlenderPreviewConversion(force) {
        autoConvertTimer.stop();
        if (!forge.hasProject || forge.busy)
            return;
        if (!force && root.currentTool === "skyrim"
                && forge.skyrimConversion && forge.skyrimConversion.state
                && forge.skyrimConversion.state !== "none") {
            return;
        }
        let m = forge.workspace.model;
        if (!m || !m.path)
            return;
        // If currently on a managed GLB that has a sourcePath (.blend) and user asked to reconvert:
        if (force && m.storage === "managed" && m.sourcePath) {
            const extIdx = findLibraryIndexByPath(m.sourcePath, "external");
            if (extIdx >= 0)
                forge.selectLibraryModel(extIdx);
            else
                forge.selectModelFile(m.sourcePath);
            m = forge.workspace.model;
        }
        if (!needsBlenderConversion(m))
            return;
        if (!forge.projectPath.length) {
            // An unsaved project gets a folder of its own in the configurable projects folder (forge.projectsFolder)
            if (!forge.saveProjectInProjectsFolder(String(m.name || ""))) {
                pendingAutoConvert = true;
                saveDialog.open();
                return;
            }
        }
        if (!forge.workspace.blender || !forge.workspace.blender.executable) {
            forge.detectBlender();
        }
        if (!forge.workspace.blender || !forge.workspace.blender.executable) {
            return;
        }
        const key = sourceKeyFor(m);
        if (!force) {
            const cached = findCachedGlbFor(m.path);
            if (cached && invalidatedSourceKey !== key) {
                forge.scene.load(cached.resolvedPath);
                return;
            }
        }
        invalidatedSourceKey = "";
        lastAutoAttemptedKey = key;
        pendingAutoConvert = false;
        convertingExternalPath = String(m.path);
        forge.convertSelectedModelWithBlender();
    }
    function ensurePreviewThenSkyrimAction(action) {
        if (!forge.hasProject || forge.busy)
            return;
        const m = forge.workspace.model;
        if (m && needsBlenderConversion(m) && forge.projectPath.length > 0) {
            const key = sourceKeyFor(m);
            const cached = findCachedGlbFor(m.path);
            if (cached && invalidatedSourceKey !== key && forge.scene.filePath !== cached.resolvedPath && !forge.scene.busy) {
                forge.scene.load(cached.resolvedPath);
            }
        }
        if (action === "easy")
            forge.createSkyrimModEasy();
        else
            forge.analyzeSkyrimCompatibility();
    }
    function requestSaveAndConvert() {
        if (!forge.hasProject)
            return;
        if (forge.projectPath.length === 0) {
            pendingAutoConvert = true;
            saveDialog.open();
        } else {
            startBlenderPreviewConversion(true);
        }
    }
    function focusMeshIssue(objectName) {
        if (!objectName)
            return;
        forgeViewport.focusPartByName(objectName);
    }

    Timer {
        id: autoConvertTimer
        interval: 250
        repeat: false
        onTriggered: root.startBlenderPreviewConversion(false)
    }
    Timer {
        id: skyrimAfterPreviewTimer
        interval: 250
        repeat: true
        property int waitedMs: 0
        onRunningChanged: if (running) waitedMs = 0
        onTriggered: {
            waitedMs += interval;
            const settled = root.forge.scene.hasModel || root.forge.scene.state === "error"
                            || root.forge.scene.state === "cancelled" || waitedMs >= 8000;
            if (settled && !root.forge.busy) {
                stop();
                root.runPendingSkyrimAction();
            }
        }
    }
    Connections {
        target: root.forge
        function onChanged() {
            if (root.pendingAutoConvert && root.forge.projectPath.length > 0 && !root.forge.busy) {
                Qt.callLater(() => root.startBlenderPreviewConversion(true));
                return;
            }
            Qt.callLater(root.syncPreviewAndAutoConvert);
        }
    }
    onCurrentToolChanged: Qt.callLater(root.syncPreviewAndAutoConvert)

    FolderDialog {
        id: openDialog
        title: qsTr("Selecciona un proyecto .sncf")
        onAccepted: root.forge.openProject(selectedFolder.toString())
    }
    FolderDialog {
        id: saveDialog
        title: qsTr("Guardar proyecto .sncf")
        onAccepted: {
            const ok = root.forge.saveProject(selectedFolder.toString());
            if (ok && root.pendingAutoConvert) {
                Qt.callLater(() => root.startBlenderPreviewConversion(true));
            }
        }
        onRejected: root.pendingAutoConvert = false
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
                id: forgeViewport
                SplitView.fillWidth: true
                forge: root.forge
                onNewProjectRequested: root.newProject()
                onOpenProjectRequested: openDialog.open()
                onToolRequested: tool => root.showTool(tool)
                onSaveAndConvertRequested: root.requestSaveAndConvert()
                onConvertPreviewRequested: force => root.startBlenderPreviewConversion(force)
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
                                case "easy":
                                    return easyPanel;
                                case "native":
                                    return nativePanel;
                                case "import":
                                    return importPanel;
                                case "library":
                                    return libraryPanel;
                                case "blender":
                                    return blenderPanel;
                                case "skyrim":
                                    return skyrimPanel;
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
                        // Only while shown: its text is a JSON dump of the whole preview (every part, the full glTF
                        // inspection) that was rebuilt on each change of the Forge, even hidden.
                        Loader {
                            width: parent.width
                            active: advanced.checked
                            sourceComponent: ForgeAdvancedInspector {
                                forge: root.forge
                            }
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
            onSaveAndConvertRequested: root.requestSaveAndConvert()
            onConvertPreviewRequested: force => root.startBlenderPreviewConversion(force)
            onToolRequested: tool => root.showTool(tool)
        }
    }
    Component {
        id: libraryPanel
        ForgeModelLibrary {
            forge: root.forge
            onConvertPreviewRequested: force => root.startBlenderPreviewConversion(force)
        }
    }
    Component {
        id: easyPanel
        EasyModePanel {
            forge: root.forge
            onToolRequested: tool => root.showTool(tool)
            onWorkspaceRequested: target => root.workspaceRequested(target)
            onAnalyzeRequested: root.ensurePreviewThenSkyrimAction("analyze")
            onOneClickRequested: root.ensurePreviewThenSkyrimAction("easy")
        }
    }
    Component {
        id: blenderPanel
        BlenderConnectionPanel {
            forge: root.forge
        }
    }
    Component {
        id: skyrimPanel
        SkyrimConversionWizard {
            forge: root.forge
            onSaveRequested: root.save()
            onConvertPreviewRequested: force => root.startBlenderPreviewConversion(force)
            onFocusIssueRequested: objectName => root.focusMeshIssue(objectName)
            onAnalyzeWithPreviewRequested: root.ensurePreviewThenSkyrimAction("analyze")
            onEasyModWithPreviewRequested: root.ensurePreviewThenSkyrimAction("easy")
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
