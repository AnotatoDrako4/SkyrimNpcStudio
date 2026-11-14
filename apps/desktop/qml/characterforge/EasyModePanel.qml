import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import SkyrimNpcStudio

// Character Forge — Modo Fácil. Everything shown here comes from forge.easyMode (engines/creation forge_easy_mode),
// computed from the real state of the project, Blender, the BethesdaWorker, the indexed profile and the last
// diagnosis. Every button calls an operation the advanced tools already have; nothing is exported here that the
// advanced path would refuse. "Ver detalle técnico" opens the full Skyrim wizard.
ColumnLayout {
    id: root
    objectName: "forgeEasyMode"
    required property CharacterForgeViewModel forge
    signal toolRequested(string tool)
    signal workspaceRequested(string target)
    signal analyzeRequested()   // analysis after the preview settles (CharacterForge.ensurePreviewThenSkyrimAction)
    signal oneClickRequested()  // the whole chain: analysis, safe fixes, NIF, mod
    spacing: Theme.spacingMd

    readonly property var easy: forge.easyMode || ({})
    readonly property var actions: easy.actions || []
    readonly property var flow: easy.flow || ({ steps: [], current: "", canExport: false })
    readonly property var diagnosis: easy.diagnosis || ({ findings: [] })
    readonly property var conversion: forge.skyrimConversion || ({})
    readonly property bool hasModel: !!(forge.hasProject && forge.workspace.model && forge.workspace.model.path)

    function stateColor(state) {
        switch (state) {
        case "available": case "done": case "COMPATIBLE": return Theme.ok
        case "partial": case "current": case "running": case "CORRECCION_SEGURA_DISPONIBLE": return Theme.accent
        case "REVISION_MANUAL": return Theme.warn
        case "blocked": case "BLOQUEANTE": return Theme.error
        default: return Theme.textDim
        }
    }
    function stateText(state) {
        switch (state) {
        case "available": return qsTr("Disponible")
        case "partial": return qsTr("Parcial")
        case "blocked": return qsTr("Bloqueado")
        case "done": return qsTr("Hecho")
        case "current": return qsTr("Siguiente")
        case "running": return qsTr("En curso")
        case "pending": return qsTr("Pendiente")
        case "COMPATIBLE": return qsTr("Compatible")
        case "CORRECCION_SEGURA_DISPONIBLE": return qsTr("Corrección segura")
        case "REVISION_MANUAL": return qsTr("Revisión manual")
        case "BLOQUEANTE": return qsTr("Bloqueante")
        case "NO_VERIFICADO": return qsTr("No verificado")
        default: return state
        }
    }
    function actionById(id) {
        for (const a of actions) if (a.id === id) return a
        return null
    }

    // A model file picked here: a project when there is none, the model, and a folder of its own in projectsFolder
    // (never a fixed path). The preview starts by itself (CharacterForge's coordinator converts BLEND/FBX/OBJ to GLB).
    function importModel(url) {
        const file = decodeURIComponent(String(url).split("/").pop())
        if (!forge.hasProject && !forge.newProject(file.replace(/\.[^.]+$/, "")))
            return
        forge.selectModelFile(String(url))
        if (forge.lastError.length > 0)
            return
        if (!forge.workspace.blender || !forge.workspace.blender.executable)
            forge.detectBlender()
        if (forge.projectPath.length === 0)
            forge.saveProjectInProjectsFolder(file)
    }
    function runAction(id) {
        switch (id) {
        case "createCharacter":
            if (!forge.hasProject) forge.newProject(qsTr("Nuevo personaje"))
            root.toolRequested("native")
            break
        case "editNpc": root.workspaceRequested("creator"); break
        case "bodyAppearance": root.workspaceRequested("creator.body"); break
        case "outfits": root.workspaceRequested("outfit"); break
        case "importBlender": importDialog.open(); break
        case "prepareResource": runStep(flow.current === "prepare" ? "prepare" : "diagnose"); break
        case "exportMod": runStep("export"); break
        }
    }
    function runStep(id) {
        switch (id) {
        case "select": importDialog.open(); break
        case "preview": root.toolRequested("blender"); break
        case "diagnose": root.analyzeRequested(); break
        case "prepare": forge.exportSkyrimNif(); break
        case "validate": root.toolRequested("skyrim"); break
        case "export": if (flow.canExport) forge.packageSkyrimMod(); break
        }
    }
    readonly property var stepButton: ({
        "select": qsTr("ELEGIR MODELO"), "preview": qsTr("VER BLENDER Y VISTA PREVIA"), "diagnose": qsTr("DIAGNOSTICAR"),
        "prepare": qsTr("PREPARAR Y EXPORTAR NIF"), "validate": qsTr("VER LA VERIFICACIÓN"), "export": qsTr("CREAR MOD MO2")
    })

    FileDialog {
        id: importDialog
        title: qsTr("Modelo para Skyrim")
        nameFilters: [qsTr("Modelos 3D (*.blend *.fbx *.obj *.glb *.gltf)")]
        onAccepted: root.importModel(selectedFile.toString())
    }
    FolderDialog {
        id: projectsFolderDialog
        title: qsTr("Carpeta para los proyectos .sncf")
        onAccepted: root.forge.setProjectsFolder(selectedFolder.toString())
    }

    Text {
        Layout.fillWidth: true
        text: qsTr("Character Forge — Modo Fácil")
        color: Theme.text
        font.pixelSize: Theme.large
        font.bold: true
    }
    Text {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        color: Theme.textDim
        font.pixelSize: Theme.small
        text: qsTr("Cada opción consulta el estado real del estudio. Si algo falta, se indica qué y cómo seguir; "
                   + "nunca se habilita una exportación que no haya superado sus comprobaciones.")
    }

    // ---- Guided flow of the current resource ----
    Rectangle {
        Layout.fillWidth: true
        visible: root.hasModel
        implicitHeight: flowColumn.implicitHeight + Theme.spacingLg * 2
        radius: Theme.radius
        color: "transparent"
        border.color: Theme.accent
        ColumnLayout {
            id: flowColumn
            anchors.fill: parent
            anchors.margins: Theme.spacingLg
            spacing: Theme.spacingXs
            Text {
                Layout.fillWidth: true
                text: qsTr("Recurso: ") + (root.forge.workspace.model ? root.forge.workspace.model.name : "")
                color: Theme.text
                font.bold: true
                elide: Text.ElideMiddle
            }
            Repeater {
                model: root.flow.steps
                delegate: RowLayout {
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    spacing: Theme.spacingSm
                    Rectangle {
                        Layout.alignment: Qt.AlignTop
                        implicitWidth: Theme.sp(20)
                        implicitHeight: Theme.sp(20)
                        radius: width / 2
                        color: modelData.state === "pending" ? "transparent" : root.stateColor(modelData.state)
                        border.color: root.stateColor(modelData.state)
                        Text {
                            anchors.centerIn: parent
                            text: modelData.state === "done" ? "✓" : modelData.state === "blocked" ? "!" : String(index + 1)
                            color: modelData.state === "pending" ? Theme.textDim : Theme.onAccent
                            font.pixelSize: Theme.small
                            font.bold: true
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text {
                            objectName: "easyStep_" + modelData.id
                            Layout.fillWidth: true
                            text: modelData.title.toUpperCase() + "  ·  " + root.stateText(modelData.state)
                            color: modelData.state === "pending" ? Theme.textDim : Theme.text
                            font.bold: modelData.id === root.flow.current
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: modelData.detail.length > 0
                            text: modelData.detail
                            color: modelData.state === "blocked" ? Theme.error : Theme.textDim
                            font.pixelSize: Theme.small
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Theme.spacingSm
                Button {
                    objectName: "easyRunStep"
                    Layout.fillWidth: true
                    visible: root.flow.current.length > 0
                    text: root.stepButton[root.flow.current] || ""
                    enabled: !root.forge.busy && (root.flow.current !== "export" || root.flow.canExport)
                             && (root.flow.current !== "diagnose" || (root.actionById("prepareResource") || {}).state !== "blocked")
                    onClicked: root.runStep(root.flow.current)
                }
                Button {
                    objectName: "easyOneClick"
                    Layout.fillWidth: true
                    visible: root.flow.current === "diagnose" || root.flow.current === "prepare"
                    text: qsTr("TODO EN UN CLIC")
                    enabled: !root.forge.busy && !root.conversion.easy && (root.actionById("prepareResource") || {}).state === "available"
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Diagnostica, aplica solo correcciones seguras, exporta el NIF y crea el mod; se detiene en el primer paso que no pueda completarse.")
                    onClicked: root.oneClickRequested()
                }
            }
            Text {
                objectName: "easyModResult"
                Layout.fillWidth: true
                visible: root.conversion.modState === "done"
                wrapMode: Text.WrapAnywhere
                color: Theme.ok
                font.pixelSize: Theme.small
                text: qsTr("Mod creado: ") + ((root.conversion.mod || {}).zipPath || "")
                      + qsTr("\nPrueba dentro de Skyrim: PENDIENTE_SKYRIM (instálalo en un perfil de prueba de MO2).")
            }
        }
    }

    // ---- Diagnosis explained ----
    ColumnLayout {
        Layout.fillWidth: true
        visible: root.hasModel && (root.diagnosis.findings || []).length > 0
        spacing: Theme.spacingXs
        RowLayout {
            Layout.fillWidth: true
            Text {
                text: qsTr("Diagnóstico")
                color: Theme.text
                font.bold: true
            }
            Label {
                objectName: "easyDiagnosisState"
                text: root.stateText(root.diagnosis.state || "NO_VERIFICADO")
                color: root.stateColor(root.diagnosis.state)
                font.bold: true
            }
        }
        Text {
            Layout.fillWidth: true
            text: root.diagnosis.summary || ""
            color: Theme.textDim
            wrapMode: Text.WordWrap
        }
        Repeater {
            model: root.diagnosis.findings || []
            delegate: Rectangle {
                required property var modelData
                Layout.fillWidth: true
                implicitHeight: findingColumn.implicitHeight + Theme.spacingMd * 2
                radius: Theme.radiusSmall
                color: Theme.panelAlt
                border.color: root.stateColor(modelData.uiState)
                ColumnLayout {
                    id: findingColumn
                    anchors.fill: parent
                    anchors.margins: Theme.spacingMd
                    spacing: 2
                    Text {
                        Layout.fillWidth: true
                        text: root.stateText(modelData.uiState) + "  ·  " + modelData.resource
                        color: root.stateColor(modelData.uiState)
                        font.bold: true
                        font.pixelSize: Theme.small
                        elide: Text.ElideRight
                    }
                    Text { Layout.fillWidth: true; text: modelData.problem; color: Theme.text; wrapMode: Text.WordWrap; font.pixelSize: Theme.small }
                    Text {
                        Layout.fillWidth: true
                        visible: modelData.whySkyrim.length > 0
                        text: qsTr("Por qué importa en Skyrim: ") + modelData.whySkyrim
                        color: Theme.textDim; wrapMode: Text.WordWrap; font.pixelSize: Theme.small
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: modelData.operation.length > 0
                        text: qsTr("Qué hacer: ") + modelData.operation
                        color: Theme.textDim; wrapMode: Text.WordWrap; font.pixelSize: Theme.small
                    }
                    Text {
                        Layout.fillWidth: true
                        text: [modelData.reversible ? qsTr("Reversible (copia de trabajo)") : qsTr("No reversible"),
                               modelData.requiresUser ? qsTr("requiere tu intervención") : qsTr("automático"),
                               modelData.blocksExport ? qsTr("bloquea la exportación") : qsTr("no bloquea la exportación")].join(" · ")
                        color: modelData.blocksExport ? Theme.error : Theme.textMuted
                        wrapMode: Text.WordWrap
                        font.pixelSize: Theme.micro
                    }
                }
            }
        }
    }

    // ---- NPC of the Character Creator (creation::easy::npc_overview over BodyDirector, OutfitEditor and Outfit Fit Doctor) ----
    Rectangle {
        objectName: "easyNpc"
        Layout.fillWidth: true
        implicitHeight: npcColumn.implicitHeight + Theme.spacingLg * 2
        radius: Theme.radius
        color: "transparent"
        border.color: Theme.borderSubtle
        readonly property var npc: root.easy.npc || ({ state: "none" })
        ColumnLayout {
            id: npcColumn
            anchors.fill: parent
            anchors.margins: Theme.spacingLg
            spacing: Theme.spacingXs
            readonly property var npc: parent.npc
            readonly property bool ready: npc.state === "ready"
            // Bindings below read these, never a missing key of a state that is not ready
            readonly property var body: npc.body || ({})
            readonly property var presets: npc.presets || ({})
            readonly property var who: npc.npc || ({})
            Text {
                Layout.fillWidth: true
                text: npcColumn.ready ? qsTr("NPC: ") + (npcColumn.who.name || npcColumn.who.formKey) : qsTr("NPC del Creador de personajes")
                color: Theme.text
                font.bold: true
                elide: Text.ElideRight
            }
            Text {
                objectName: "easyNpcMessage"
                Layout.fillWidth: true
                visible: !npcColumn.ready
                text: npcColumn.npc.message || ""
                color: npcColumn.npc.state === "error" ? Theme.error : Theme.textDim
                wrapMode: Text.WordWrap
                font.pixelSize: Theme.small
            }
            // Body
            Text {
                objectName: "easyNpcBody"
                Layout.fillWidth: true
                visible: npcColumn.ready
                text: qsTr("Cuerpo: ") + (npcColumn.body.familyLabel || qsTr("sin determinar")) + "  ·  "
                      + root.stateText(npcColumn.body.uiState) + "\n" + (npcColumn.body.provider || "") + "  " + (npcColumn.body.path || "")
                      + (npcColumn.body.tri ? "\nTRI: " + npcColumn.body.tri : qsTr("\nSin TRI de morphs"))
                color: root.stateColor(npcColumn.body.uiState)
                wrapMode: Text.WrapAnywhere
                font.pixelSize: Theme.small
            }
            Text {
                Layout.fillWidth: true
                visible: npcColumn.ready
                text: qsTr("Presets BodySlide: %1 compatibles · %2 experimentales · %3 requieren recursos · %4 bloqueados")
                      .arg(npcColumn.presets.compatible).arg(npcColumn.presets.experimental)
                      .arg(npcColumn.presets.needsResources).arg(npcColumn.presets.blocked)
                color: Theme.text
                wrapMode: Text.WordWrap
                font.pixelSize: Theme.small
            }
            Text {
                Layout.fillWidth: true
                visible: npcColumn.ready && (npcColumn.npc.familyChanges || []).length > 0
                text: qsTr("Cambio de familia (solo evaluación, no se exporta): ")
                      + (npcColumn.npc.familyChanges || []).map(c => c.label + " → " + c.verdictLabel).join(" · ")
                color: Theme.textDim
                wrapMode: Text.WordWrap
                font.pixelSize: Theme.small
            }
            // Garments
            Text {
                objectName: "easyNpcGarments"
                Layout.fillWidth: true
                visible: npcColumn.ready
                readonly property var g: npcColumn.npc.outfits || ({})
                text: (g.verified ? qsTr("Prendas (Outfit Fit Doctor): ") : qsTr("Prendas (sin analizar): "))
                      + qsTr("%1 compatibles · %2 de otra familia · %3 sin verificar").arg((g.compatible || []).length)
                        .arg((g.needsConversion || []).length).arg((g.unverified || []).length)
                      + ((g.compatible || []).length ? qsTr("\nCompatibles: ") + g.compatible.join(", ") : "")
                      + ((g.needsConversion || []).length ? qsTr("\nRequieren conversión: ") + g.needsConversion.map(p => p.name + " (" + p.reason + ")").join("; ") : "")
                      + ((g.unverified || []).length ? qsTr("\nSin verificar: ") + g.unverified.join(", ") : "")
                color: Theme.text
                wrapMode: Text.WordWrap
                font.pixelSize: Theme.small
            }
            Text {
                Layout.fillWidth: true
                visible: npcColumn.ready && (npcColumn.npc.conflicts || []).length > 0
                text: qsTr("Conflictos: ") + (npcColumn.npc.conflicts || []).map(c => (c.kind === "file" ? qsTr("[archivo] ") : qsTr("[slot] ")) + c.text).join("\n")
                color: Theme.warn
                wrapMode: Text.WordWrap
                font.pixelSize: Theme.small
            }
            Text {
                Layout.fillWidth: true
                visible: npcColumn.ready && (npcColumn.npc.missing || []).length > 0
                text: qsTr("Falta: ") + (npcColumn.npc.missing || []).join("\n")
                color: Theme.warn
                wrapMode: Text.WordWrap
                font.pixelSize: Theme.small
            }
            Repeater {
                model: npcColumn.ready ? (npcColumn.npc.findings || []).filter(f => f.uiState !== "COMPATIBLE") : []
                delegate: Text {
                    required property var modelData
                    Layout.fillWidth: true
                    text: root.stateText(modelData.uiState) + " · " + modelData.resource + ": " + modelData.problem
                          + (modelData.operation ? "\n  " + modelData.operation : "")
                    color: root.stateColor(modelData.uiState)
                    wrapMode: Text.WordWrap
                    font.pixelSize: Theme.small
                }
            }
            // Export routes
            Text {
                Layout.fillWidth: true
                Layout.topMargin: Theme.spacingSm
                visible: npcColumn.ready
                text: qsTr("Exportaciones permitidas")
                color: Theme.text
                font.bold: true
            }
            Repeater {
                model: npcColumn.ready ? (npcColumn.npc.exports || []) : []
                delegate: RowLayout {
                    required property var modelData
                    objectName: "easyNpcExport_" + modelData.id
                    Layout.fillWidth: true
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text {
                            Layout.fillWidth: true
                            text: modelData.label + "  ·  " + (modelData.state === "notNeeded" ? qsTr("No hace falta") : root.stateText(modelData.state))
                            color: modelData.state === "notNeeded" ? Theme.textDim : root.stateColor(modelData.state)
                            font.pixelSize: Theme.small
                            font.bold: true
                            wrapMode: Text.WordWrap
                        }
                        Text {
                            Layout.fillWidth: true
                            text: modelData.reason
                            color: Theme.textDim
                            font.pixelSize: Theme.small
                            wrapMode: Text.WordWrap
                        }
                    }
                    Button {
                        text: qsTr("IR")
                        enabled: modelData.state === "available" || modelData.state === "partial"
                        onClicked: root.workspaceRequested(modelData.target)
                    }
                }
            }
        }
    }

    // ---- What do you want to do? ----
    Text {
        Layout.fillWidth: true
        Layout.topMargin: Theme.spacingSm
        text: qsTr("¿Qué quieres hacer?")
        color: Theme.text
        font.bold: true
    }
    Repeater {
        model: root.actions
        delegate: Rectangle {
            required property var modelData
            objectName: "easyAction_" + modelData.id
            Layout.fillWidth: true
            implicitHeight: actionColumn.implicitHeight + Theme.spacingMd * 2
            radius: Theme.radiusSmall
            color: Theme.panelAlt
            border.color: Theme.borderSubtle
            ColumnLayout {
                id: actionColumn
                anchors.fill: parent
                anchors.margins: Theme.spacingMd
                spacing: 2
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: modelData.title
                        color: Theme.text
                        font.bold: true
                        wrapMode: Text.WordWrap
                    }
                    Label {
                        objectName: "easyActionState_" + modelData.id
                        text: root.stateText(modelData.state)
                        color: root.stateColor(modelData.state)
                        font.pixelSize: Theme.small
                        font.bold: true
                    }
                }
                Text { Layout.fillWidth: true; text: modelData.description; color: Theme.textDim; wrapMode: Text.WordWrap; font.pixelSize: Theme.small }
                Text {
                    Layout.fillWidth: true
                    visible: modelData.reason.length > 0
                    text: modelData.reason
                    color: modelData.state === "blocked" ? Theme.error : Theme.text
                    wrapMode: Text.WordWrap
                    font.pixelSize: Theme.small
                }
                Text {
                    Layout.fillWidth: true
                    visible: modelData.missing.length > 0
                    text: qsTr("Falta: ") + modelData.missing.join("; ")
                    color: Theme.warn
                    wrapMode: Text.WordWrap
                    font.pixelSize: Theme.small
                }
                Text {
                    Layout.fillWidth: true
                    visible: modelData.next.length > 0
                    text: qsTr("Siguiente paso: ") + modelData.next
                    color: Theme.textDim
                    wrapMode: Text.WordWrap
                    font.pixelSize: Theme.small
                }
                Button {
                    objectName: "easyActionButton_" + modelData.id
                    Layout.alignment: Qt.AlignRight
                    text: modelData.id === "exportMod" ? qsTr("CREAR MOD") : qsTr("ABRIR")
                    // Export only when every check of its route passed (state "available" and the flow allows it).
                    enabled: !root.forge.busy && modelData.state !== "blocked"
                             && (modelData.id !== "exportMod" || root.flow.canExport)
                    onClicked: root.runAction(modelData.id)
                }
            }
        }
    }

    // ---- Projects folder and technical detail ----
    Text {
        Layout.fillWidth: true
        Layout.topMargin: Theme.spacingSm
        wrapMode: Text.WrapAnywhere
        color: Theme.textDim
        font.pixelSize: Theme.small
        text: qsTr("Proyectos nuevos en: ") + (root.easy.projectsFolder || "")
    }
    RowLayout {
        Layout.fillWidth: true
        Button {
            Layout.fillWidth: true
            text: qsTr("Cambiar carpeta…")
            onClicked: projectsFolderDialog.open()
        }
        Button {
            objectName: "easyTechnicalDetail"
            Layout.fillWidth: true
            text: qsTr("Ver detalle técnico")
            onClicked: root.toolRequested("skyrim")
        }
    }
}
