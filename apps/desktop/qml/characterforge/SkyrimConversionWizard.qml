import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import SkyrimNpcStudio

// Skyrim Conversion Wizard: IMPORTAR -> DIAGNOSTICAR -> PREPARAR -> ADAPTAR -> EXPORTAR -> VERIFICAR.
// Every action calls CharacterForgeViewModel (Blender job + engines/creation skyrim_conversion); nothing here is a mock.
ColumnLayout {
    id: root
    required property CharacterForgeViewModel forge
    spacing: Theme.spacingMd

    signal saveRequested()
    signal convertPreviewRequested(bool force)
    signal focusIssueRequested(string objectName)
    signal analyzeWithPreviewRequested()
    signal easyModWithPreviewRequested()

    readonly property var conversion: forge.skyrimConversion || ({ state: "none" })
    readonly property var classification: conversion.classification || ({})
    readonly property var counts: classification.counts || ({})
    readonly property var prepareInfo: conversion.prepare || ({})
    readonly property var modelInfo: forge.workspace.model || ({})
    readonly property bool hasModel: !!modelInfo.path
    readonly property bool hasBlender: !!forge.workspace.blender && !!forge.workspace.blender.executable
    readonly property bool saved: forge.hasProject && forge.projectPath.length > 0
    readonly property bool running: conversion.state === "running"
    readonly property bool analyzed: ["analyzed", "exported", "verificationFailed"].indexOf(conversion.state) >= 0
    readonly property string missing: !saved ? qsTr("Guarda el proyecto .sncf.")
                                    : !hasModel ? qsTr("Selecciona un modelo en «Importar modelo».")
                                    : !hasBlender ? qsTr("Detecta o selecciona Blender en «Blender y MCP».") : ""

    // Persisted UI preferences in workspace.skyrimUi (read safely; written only on explicit user interaction)
    readonly property var uiPrefs: forge.workspace.skyrimUi || ({})
    property string severityFilter: uiPrefs.severityFilter || "all"
    property int focusedStep: -1

    function saveUiPref(key, value) {
        if (!forge.hasProject || forge.busy)
            return;
        let next = Object.assign({}, forge.workspace.skyrimUi || ({}));
        next[key] = value;
        forge.setWorkspaceValue("skyrimUi", next);
    }

    readonly property var filteredIssues: {
        const all = classification.issues || [];
        if (root.severityFilter === "all")
            return all;
        const out = [];
        for (let i = 0; i < all.length; ++i) {
            if (all[i] && all[i].severity === root.severityFilter)
                out.push(all[i]);
        }
        return out;
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

    function severityColor(s) {
        return s === "blocking" ? Theme.error : s === "warning" ? Theme.warn : s === "compatible" ? Theme.ok
             : s === "autoFixable" ? Theme.accent : Theme.textDim
    }
    function severityLabel(s) {
        return ({ blocking: qsTr("Bloqueante"), autoFixable: qsTr("Corrección automática"), warning: qsTr("Advertencia"),
                  manualReview: qsTr("Revisión manual"), compatible: qsTr("Compatible") })[s] || s
    }
    function stepState(index) {
        const st = conversion.state;
        const blocked = analyzed && !classification.exportable;
        switch (index) {
        case 0: return hasModel ? "done" : "current"
        case 1: return analyzed ? (blocked ? "blocked" : "done") : (hasModel ? "current" : "todo")
        case 2: case 3: return st === "exported" || st === "verificationFailed" ? "done" : (blocked ? "blocked" : (analyzed ? "current" : "todo"))
        case 4: return st === "exported" ? "done" : (st === "verificationFailed" ? "failed" : (analyzed && classification.exportable ? "current" : "todo"))
        default: return st === "exported" ? "done" : (st === "verificationFailed" ? "failed" : "todo")
        }
    }
    function stepBadge(st) {
        return st === "done" ? "✓ " : st === "current" ? "● " : st === "blocked" ? "! " : st === "failed" ? "✗ " : ""
    }

    RowLayout {
        Layout.fillWidth: true
        Text {
            Layout.fillWidth: true
            text: qsTr("Convertir a Skyrim")
            color: Theme.text
            font.bold: true
            font.pixelSize: Theme.title
        }
        // Reversible workspace actions
        ToolButton {
            text: qsTr("Deshacer")
            enabled: root.forge.canUndo && !root.forge.busy
            font.pixelSize: Theme.small
            onClicked: root.forge.undo()
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Revertir el último cambio en el borrador del proyecto")
        }
        ToolButton {
            text: qsTr("Rehacer")
            enabled: root.forge.canRedo && !root.forge.busy
            font.pixelSize: Theme.small
            onClicked: root.forge.redo()
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Reaplicar el cambio revertido")
        }
    }
    Text {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        color: Theme.textDim
        text: root.forge.skyrimProfile === "skinned"
            ? qsTr("Perfil: Malla con esqueleto (SkinnedMesh) para Skyrim SE. Valida pesos (1..4 influencias), particiones y mapeo frente al skeleton.nif de referencia. La carga en el juego sigue PENDIENTE_SKYRIM.")
            : qsTr("Perfil: recurso estático o accesorio (Tipo A) para Skyrim SE. La vista previa GLB no es un recurso de Skyrim: "
                   + "solo el NIF exportado y releído cuenta, y su carga en el juego sigue PENDIENTE_SKYRIM.")
    }

    // Selector de perfil de conversión (static vs skinned) del contrato de CharacterForgeViewModel
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingSm
        Text {
            text: qsTr("Perfil:")
            color: Theme.textDim
            font.pixelSize: Theme.small
        }
        Button {
            Layout.fillWidth: true
            text: qsTr("Estático (Tipo A)")
            highlighted: root.forge.skyrimProfile !== "skinned"
            enabled: !root.forge.busy
            font.pixelSize: Theme.small
            onClicked: root.forge.setSkyrimProfile("static")
        }
        Button {
            Layout.fillWidth: true
            text: qsTr("Con esqueleto (SkinnedMesh)")
            highlighted: root.forge.skyrimProfile === "skinned"
            enabled: !root.forge.busy
            font.pixelSize: Theme.small
            onClicked: root.forge.setSkyrimProfile("skinned")
        }
    }

    // Modo fácil: one click from a model file to a verified MO2 mod (same steps and checks as the buttons below)
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: easyColumn.implicitHeight + Theme.spacingLg * 2
        radius: 6
        color: Theme.surface
        border.color: Theme.accent
        ColumnLayout {
            id: easyColumn
            anchors.fill: parent
            anchors.margins: Theme.spacingLg
            spacing: Theme.spacingXs
            Text {
                text: qsTr("Modo fácil: crear mod para Skyrim")
                color: Theme.text
                font.bold: true
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.pixelSize: Theme.small
                color: Theme.textDim
                text: qsTr("Elige un modelo (.blend, .fbx, .obj, .glb). Se analiza, se corrige lo seguro sobre una copia (reducir "
                           + "polígonos, metros → unidades, texturas), se exporta el NIF verificado y se crea el mod MO2 con un STAT. "
                           + "Se detiene si algo no se puede corregir solo. El paso del mod usa el perfil MO2 cargado (solo lectura). "
                           + "Tu archivo original nunca se modifica.")
            }
            RowLayout {
                Layout.fillWidth: true
                visible: !root.saved || !root.hasBlender
                spacing: Theme.spacingSm
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font.pixelSize: Theme.small
                    color: Theme.warn
                    text: !root.saved ? qsTr("Antes: guarda el proyecto .sncf.") : qsTr("Antes: detecta Blender en «Blender y MCP».")
                }
                Button {
                    visible: !root.saved && root.forge.hasProject
                    text: qsTr("Guardar .sncf…")
                    font.pixelSize: Theme.small
                    enabled: !root.forge.busy
                    onClicked: root.saveRequested()
                }
                Button {
                    visible: root.saved && !root.hasBlender
                    text: qsTr("Detectar Blender")
                    font.pixelSize: Theme.small
                    enabled: !root.forge.busy
                    onClicked: root.forge.detectBlender()
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Button {
                    Layout.fillWidth: true
                    text: qsTr("ELEGIR MODELO Y CREAR MOD")
                    enabled: root.saved && root.hasBlender && !root.forge.busy && !root.conversion.easy
                    onClicked: easyDialog.open()
                }
                Button {
                    Layout.fillWidth: true
                    text: qsTr("CREAR MOD CON EL MODELO ACTUAL")
                    enabled: root.missing.length === 0 && !root.forge.busy && !root.conversion.easy
                    onClicked: root.forge.createSkyrimModEasy()
                }
            }
            Text {
                Layout.fillWidth: true
                visible: !!root.conversion.easy || root.conversion.modState === "done"
                         || (root.conversion.state === "analyzed" && !root.classification.exportable)
                wrapMode: Text.WordWrap
                font.bold: true
                color: root.conversion.modState === "done" ? Theme.ok
                     : (root.conversion.state === "analyzed" && !root.classification.exportable) || root.conversion.modState === "error"
                       || root.conversion.state === "error" || root.conversion.state === "verificationFailed" ? Theme.error : Theme.accent
                text: root.conversion.modState === "done" ? qsTr("Mod creado: ") + ((root.conversion.mod || {}).zipPath || "")
                    : root.conversion.modState === "running" ? qsTr("3/3 · Creando el mod con el BethesdaWorker…")
                    : root.conversion.state === "running" && root.conversion.operation === "exportNif" ? qsTr("2/3 · Exportando y verificando el NIF…")
                    : root.conversion.state === "running" ? qsTr("1/3 · Analizando el modelo en Blender…")
                    : (root.conversion.state === "analyzed" && !root.classification.exportable)
                      ? qsTr("Detenido: hay problemas bloqueantes que no se corrigen solos (ver la lista).")
                    : root.conversion.modState === "error" ? qsTr("Detenido al crear el mod: ") + (root.conversion.modMessage || "")
                    : root.conversion.state === "verificationFailed" ? qsTr("Detenido: la verificación del NIF falló.")
                    : qsTr("En curso…")
            }
        }
        FileDialog {
            id: easyDialog
            title: qsTr("Modelo para el mod de Skyrim")
            nameFilters: [qsTr("Modelos 3D (*.blend *.fbx *.obj *.glb *.gltf)")]
            onAccepted: {
                root.forge.selectModelFile(selectedFile.toString())
                if (root.forge.lastError.length === 0) root.forge.createSkyrimModEasy()
            }
        }
    }

    // Interactive Stepper: Importar -> Diagnosticar -> Preparar -> Adaptar -> Exportar -> Verificar
    Flow {
        Layout.fillWidth: true
        spacing: Theme.spacingXs
        Repeater {
            model: [qsTr("Importar"), qsTr("Diagnosticar"), qsTr("Preparar"), qsTr("Adaptar"), qsTr("Exportar"), qsTr("Verificar")]
            Rectangle {
                id: stepPill
                required property var modelData
                required property int index
                readonly property string st: root.stepState(index)
                readonly property bool isFocused: root.focusedStep === index
                width: label.implicitWidth + Theme.spacingLg * 2
                height: Theme.controlHeight - 6
                radius: Theme.radius
                color: st === "done" ? Theme.okDim
                     : st === "current" ? Theme.accentSubtle
                     : (st === "failed" || st === "blocked") ? Theme.warnDim
                     : Theme.panelAlt
                border.color: isFocused ? Theme.accent : (st === "current" ? Theme.accentBorder : (st === "blocked" || st === "failed") ? Theme.warn : Theme.borderSubtle)
                border.width: isFocused ? 2 : 1
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("Paso %1: %2 (%3)").arg(index + 1).arg(modelData).arg(st)
                Text {
                    id: label
                    anchors.centerIn: parent
                    text: root.stepBadge(stepPill.st) + (stepPill.index + 1) + ". " + stepPill.modelData
                    color: Theme.text
                    font.pixelSize: Theme.small
                    font.bold: stepPill.st === "current" || stepPill.isFocused
                }
                TapHandler {
                    onTapped: root.focusedStep = (root.focusedStep === stepPill.index ? -1 : stepPill.index)
                }
            }
        }
    }

    // Step 1: Importar / Modelo activo y vínculo con vista previa 3D
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingSm
        Text {
            Layout.fillWidth: true
            elide: Text.ElideMiddle
            color: Theme.text
            text: root.hasModel ? qsTr("Modelo: %1 (%2)").arg(root.modelInfo.name || "").arg((root.modelInfo.format || "").toUpperCase())
                                : qsTr("Sin modelo seleccionado")
        }
        Button {
            visible: root.hasModel && root.forge.scene.state === "unsupported" && !root.forge.busy
            text: qsTr("Previsualizar en 3D (GLB)")
            font.pixelSize: Theme.small
            onClicked: root.convertPreviewRequested(true)
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Convierte el modelo actual a GLB en segundo plano para inspeccionarlo en el visor central")
        }
        Button {
            visible: !!root.modelInfo.sourcePath && root.findLibraryIndexByPath(root.modelInfo.sourcePath, "external") >= 0 && !root.forge.busy
            text: qsTr("Analizar .blend original")
            font.pixelSize: Theme.small
            onClicked: {
                const idx = root.findLibraryIndexByPath(root.modelInfo.sourcePath, "external");
                if (idx >= 0)
                    root.forge.selectLibraryModel(idx);
            }
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Cambia al archivo .blend original en la biblioteca manteniendo la vista previa GLB en el visor")
        }
    }
    Text {
        Layout.fillWidth: true
        visible: root.missing.length > 0
        wrapMode: Text.WordWrap
        color: Theme.warn
        text: root.missing
    }

    // Step 2: Diagnosticar
    RowLayout {
        Layout.fillWidth: true
        Button {
            Layout.fillWidth: true
            text: qsTr("ANALIZAR COMPATIBILIDAD CON SKYRIM")
            enabled: root.missing.length === 0 && !root.forge.busy
            onClicked: root.analyzeWithPreviewRequested()
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Abre una copia del modelo en Blender, genera su vista previa 3D si falta, lo inspecciona y clasifica los problemas. No modifica el original.")
        }
        Button {
            text: qsTr("Cancelar")
            visible: root.running
            enabled: root.forge.blenderBusy
            onClicked: root.forge.cancelOperation()
        }
    }
    ColumnLayout {
        Layout.fillWidth: true
        visible: root.running
        ProgressBar {
            Layout.fillWidth: true
            from: 0
            to: 1
            value: root.conversion.fraction || 0
        }
        Text {
            color: Theme.textDim
            font.pixelSize: Theme.small
            text: qsTr("Blender · %1").arg(({ start: qsTr("iniciando"), load: qsTr("abriendo la copia"), inspect: qsTr("inspeccionando"),
                                               prepare: qsTr("preparando la copia"), export: qsTr("exportando con PyNifly"),
                                               readback: qsTr("releyendo el NIF"), done: qsTr("terminado") })[root.conversion.step] || "")
        }
    }
    Text {
        Layout.fillWidth: true
        visible: root.conversion.state === "error"
        wrapMode: Text.WordWrap
        color: Theme.error
        text: root.conversion.message || ""
    }

    ColumnLayout {
        Layout.fillWidth: true
        visible: root.analyzed
        spacing: Theme.spacingSm
        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.textDim
            font.pixelSize: Theme.small
            readonly property var t: root.conversion.totals || ({})
            readonly property var d: root.conversion.dimensions || [0, 0, 0]
            readonly property var p: root.conversion.pynifly || ({})
            text: qsTr("%1 malla(s) · %2 vértices · %3 triángulos · %4 material(es) · %5 textura(s) · %6 shape keys\n"
                       + "Tamaño %7 × %8 × %9 unidades · Blender %10 · PyNifly %11")
                    .arg(t.meshes || 0).arg(t.vertices || 0).arg(t.triangles || 0).arg(t.materials || 0).arg(t.images || 0)
                    .arg(t.shapeKeys || 0).arg((d[0] || 0).toFixed(1)).arg((d[1] || 0).toFixed(1)).arg((d[2] || 0).toFixed(1))
                    .arg(root.conversion.blenderVersion || "?")
                    .arg(p.available ? (p.version || []).join(".") + (p.tested ? "" : qsTr(" (no probada)")) : qsTr("no disponible"))
        }

        // SkinnedMesh pipeline states & skeletonMapping summary when profile === "skinned"
        Rectangle {
            Layout.fillWidth: true
            visible: root.conversion.profile === "skinned" && (!!root.conversion.states || !!root.conversion.skeletonMapping)
            implicitHeight: skinnedCol.implicitHeight + Theme.spacingSm * 2
            radius: Theme.radius
            color: Theme.panelAlt
            border.color: Theme.accentBorder
            ColumnLayout {
                id: skinnedCol
                anchors.fill: parent
                anchors.margins: Theme.spacingSm
                spacing: 4
                readonly property var skMap: root.conversion.skeletonMapping || ({})
                readonly property var skCounts: skMap.counts || ({})
                readonly property var skWeighted: skMap.weighted || ({})
                readonly property var skStates: (root.conversion.states || ({})).states || []
                Text {
                    Layout.fillWidth: true
                    color: Theme.text
                    font.bold: true
                    font.pixelSize: Theme.small
                    text: qsTr("Estado SkinnedMesh: %1").arg((root.conversion.states || ({})).current || "PREVIEW_ONLY")
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    Repeater {
                        model: skinnedCol.skStates
                        Text {
                            required property var modelData
                            font.pixelSize: Theme.small
                            font.bold: !!modelData.reached
                            color: modelData.reached ? Theme.ok : Theme.textMuted
                            text: (modelData.reached ? "✓ " : "○ ") + modelData.state
                        }
                    }
                }
                Text {
                    visible: !!skinnedCol.skMap.armature
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textDim
                    font.pixelSize: Theme.small
                    text: qsTr("Mapeo de esqueleto («%1» vs %2): %3 verificados · %4 sugeridos · %5 no reconocidos · %6 incompatibles (con peso verificados: %7)")
                        .arg(skinnedCol.skMap.armature || "")
                        .arg((skinnedCol.skMap.reference || ({})).label || "skeleton.nif")
                        .arg(skinnedCol.skCounts.verified || 0)
                        .arg(skinnedCol.skCounts.suggested || 0)
                        .arg(skinnedCol.skCounts.unrecognized || 0)
                        .arg(skinnedCol.skCounts.incompatible || 0)
                        .arg(skinnedCol.skMap.allWeightedVerified ? qsTr("sí") : qsTr("no"))
                }
            }
        }

        // Severity counts (keep exact Text format for automation checks, and allow clicking to filter)
        Flow {
            Layout.fillWidth: true
            spacing: Theme.spacingSm
            Text {
                color: root.severityFilter === "all" ? Theme.text : Theme.textMuted
                font.pixelSize: Theme.small
                font.bold: root.severityFilter === "all"
                font.underline: root.severityFilter === "all"
                text: qsTr("Todos (%1)").arg((root.classification.issues || []).length)
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        root.severityFilter = "all";
                        root.saveUiPref("severityFilter", "all");
                    }
                }
            }
            Repeater {
                model: ["blocking", "autoFixable", "warning", "manualReview", "compatible"]
                Text {
                    required property var modelData
                    color: root.severityColor(modelData)
                    font.pixelSize: Theme.small
                    font.bold: true
                    font.underline: root.severityFilter === modelData
                    text: root.severityLabel(modelData) + ": " + (root.counts[modelData] || 0)
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            const next = (root.severityFilter === modelData) ? "all" : modelData;
                            root.severityFilter = next;
                            root.saveUiPref("severityFilter", next);
                        }
                    }
                }
            }
        }

        Repeater {
            model: root.filteredIssues
            Rectangle {
                required property var modelData
                Layout.fillWidth: true
                implicitHeight: issueText.implicitHeight + Theme.spacingMd * 2
                radius: Theme.radius
                color: Theme.panelAlt
                border.color: root.severityColor(modelData.severity)
                border.width: 1
                Column {
                    id: issueText
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.spacingMd
                    spacing: 3
                    RowLayout {
                        width: parent.width
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            color: root.severityColor(modelData.severity)
                            font.bold: true
                            font.pixelSize: Theme.small
                            text: root.severityLabel(modelData.severity) + (modelData.object ? " · " + modelData.object : "")
                        }
                        Button {
                            visible: !!modelData.object && root.forge.scene.hasModel
                            text: qsTr("Ver en visor 3D")
                            font.pixelSize: Theme.small
                            onClicked: root.focusIssueRequested(modelData.object)
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Selecciona, encuadra y resalta esta malla en el visor 3D")
                        }
                    }
                    Text { width: parent.width; wrapMode: Text.WordWrap; color: Theme.text; text: modelData.problem }
                    Text {
                        width: parent.width; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: Theme.small
                        visible: !!modelData.consequence; text: qsTr("Consecuencia: ") + modelData.consequence
                    }
                    Text {
                        width: parent.width; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: Theme.small
                        visible: !!modelData.solution; text: qsTr("Solución: ") + modelData.solution
                    }
                    Text {
                        width: parent.width; wrapMode: Text.WordWrap; font.pixelSize: Theme.small
                        color: modelData.fixAvailable ? Theme.ok : Theme.textMuted
                        visible: modelData.severity !== "compatible"
                        text: modelData.fixAvailable ? qsTr("Se corrige solo en la copia de trabajo al exportar (no destructivo).")
                                                     : qsTr("Sin corrección automática: requiere intervención en Blender.")
                    }
                }
            }
        }
    }

    // Steps 3 & 4: Preparar y Adaptar (summary of automatic non-destructive preparation)
    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
    Text {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        color: Theme.textDim
        font.pixelSize: Theme.small
        text: qsTr("Preparación automática (solo sobre la copia): aplicar rotación/escala en mallas sin shape keys ni armature, "
                   + "copiar texturas a textures\\CharacterForge\\<modelo>\\ y omitir las ausentes. Perfiles «Armadura/cuerpo» "
                   + "(Tipo B) y «NPC completo» (Tipo C): no disponibles todavía; requieren esqueleto, particiones, slots y "
                   + "registros validados.")
    }
    Rectangle {
        Layout.fillWidth: true
        visible: ((root.prepareInfo.appliedTransforms || []).length > 0
                  || (root.prepareInfo.scaledMeshes || []).length > 0
                  || (root.prepareInfo.decimated || []).length > 0
                  || (root.prepareInfo.textures || []).length > 0)
        implicitHeight: prepDetailCol.implicitHeight + Theme.spacingSm * 2
        radius: Theme.radius
        color: Theme.panelAlt
        border.color: Theme.borderSubtle
        ColumnLayout {
            id: prepDetailCol
            anchors.fill: parent
            anchors.margins: Theme.spacingSm
            spacing: 2
            Text {
                text: qsTr("Adaptaciones aplicadas sobre la copia de trabajo:")
                color: Theme.text
                font.bold: true
                font.pixelSize: Theme.small
            }
            Text {
                visible: (root.prepareInfo.appliedTransforms || []).length > 0
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.textDim
                font.pixelSize: Theme.small
                text: qsTr("• Rotación/escala aplicada en: %1").arg((root.prepareInfo.appliedTransforms || []).join(", "))
            }
            Text {
                visible: (root.prepareInfo.scaledMeshes || []).length > 0
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.textDim
                font.pixelSize: Theme.small
                text: qsTr("• Escala metros → unidades Skyrim (×100) en %1 malla(s)").arg((root.prepareInfo.scaledMeshes || []).length)
            }
            Repeater {
                model: root.prepareInfo.decimated || []
                Text {
                    required property var modelData
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textDim
                    font.pixelSize: Theme.small
                    text: qsTr("• Decimate en «%1»: %2 → %3 caras (ratio %4)")
                        .arg(modelData.object || "?")
                        .arg(modelData.beforeFaces || 0)
                        .arg(modelData.afterFaces || 0)
                        .arg(Number(modelData.ratio || 1).toFixed(3))
                }
            }
            Text {
                visible: (root.prepareInfo.textures || []).length > 0
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.textDim
                font.pixelSize: Theme.small
                text: qsTr("• %1 textura(s) convertidas/copiadas a DDS en textures\\CharacterForge\\").arg((root.prepareInfo.textures || []).length)
            }
        }
    }

    // Steps 5 & 6: Exportar y Verificar
    Button {
        Layout.fillWidth: true
        text: qsTr("EXPORTAR NIF VALIDADO")
        enabled: root.analyzed && !!root.classification.exportable && !root.forge.busy && root.missing.length === 0
        onClicked: root.forge.exportSkyrimNif()
        ToolTip.visible: hovered
        ToolTip.text: !root.analyzed ? qsTr("Analiza el modelo primero.")
                    : !root.classification.exportable ? qsTr("Hay errores bloqueantes.")
                    : qsTr("Exporta con PyNifly (SKYRIMSE), relee el NIF con nifly y crea el paquete dentro del proyecto.")
    }
    ColumnLayout {
        Layout.fillWidth: true
        visible: root.conversion.state === "exported" || root.conversion.state === "verificationFailed"
        spacing: Theme.spacingXs
        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.bold: true
            color: root.conversion.state === "exported" ? Theme.ok : Theme.error
            text: root.conversion.state === "exported"
                  ? qsTr("NIF verificado y empaquetado · Skyrim: PENDIENTE_SKYRIM")
                  : qsTr("La verificación del NIF falló: no se escribió ningún archivo.")
        }
        Repeater {
            model: (root.conversion.verification || {}).checks || []
            Text {
                required property var modelData
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.pixelSize: Theme.small
                color: modelData.passed ? Theme.textDim : Theme.error
                text: (modelData.passed ? "✓ " : "✗ ") + modelData.code + " — " + modelData.detail
            }
        }
        TextField {
            Layout.fillWidth: true
            visible: !!root.conversion.folder
            readOnly: true
            selectByMouse: true
            text: root.conversion.folder || ""
            Accessible.name: qsTr("Carpeta del recurso exportado")
        }
        Repeater {
            model: (root.conversion.manifest || {}).files || []
            Text {
                required property var modelData
                Layout.fillWidth: true
                elide: Text.ElideMiddle
                font.pixelSize: Theme.small
                color: Theme.textMuted
                text: modelData.path + " · " + modelData.bytes + " B · " + String(modelData.sha256).substring(0, 16) + "…"
            }
        }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.small
            color: Theme.warn
            visible: root.conversion.state === "exported" && root.conversion.modState !== "done"
            text: qsTr("Esta carpeta no contiene plugin. «Empaquetar mod MO2» pide al BethesdaWorker un ESP (flag ESL) con un STAT que "
                       + "usa este NIF, lo relee y genera el ZIP. No copies la carpeta a Skyrim\\Data ni a MO2 a mano.")
        }
    }

    // G: MO2 mod (STAT plugin + ZIP) built by the BethesdaWorker
    Button {
        Layout.fillWidth: true
        visible: root.conversion.state === "exported"
        text: qsTr("EMPAQUETAR MOD MO2")
        enabled: !root.forge.busy && root.conversion.modState !== "running" && root.conversion.modState !== "done"
        onClicked: root.forge.packageSkyrimMod()
        ToolTip.visible: hovered
        ToolTip.text: qsTr("El BethesdaWorker crea un ESP (flag ESL) con un STAT que apunta al NIF, copia el NIF y las DDS comprobando "
                           + "sus SHA-256, relee el plugin y el ZIP. Se guarda en el proyecto: nada se instala en MO2.")
    }
    ColumnLayout {
        id: modResult
        Layout.fillWidth: true
        visible: root.conversion.state === "exported" && !!root.conversion.modState
        spacing: Theme.spacingXs
        readonly property var mod: root.conversion.mod || ({})
        readonly property int checked: (mod.verification || []).length
        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.bold: true
            color: root.conversion.modState === "done" ? Theme.ok : root.conversion.modState === "error" ? Theme.error : Theme.textDim
            text: root.conversion.modState === "running" ? qsTr("Generando el plugin con el BethesdaWorker…")
                : root.conversion.modState === "error" ? qsTr("No se generó el mod: ") + (root.conversion.modMessage || "")
                : qsTr("Mod MO2 verificado (PAQUETE_MO2_VERIFICADO) · Skyrim: PENDIENTE_SKYRIM")
        }
        Repeater {
            model: modResult.mod.records || []
            Text {
                required property var modelData
                Layout.fillWidth: true
                font.pixelSize: Theme.small
                color: Theme.text
                text: modelData.recordType + " " + modelData.editorId + " · " + modelData.formKey
                      + (modResult.mod.eslFlagged ? qsTr(" · ESP con flag ESL, sin masters") : "")
            }
        }
        TextField {
            Layout.fillWidth: true
            visible: !!modResult.mod.zipPath
            readOnly: true
            selectByMouse: true
            text: modResult.mod.zipPath || ""
            Accessible.name: qsTr("ZIP del mod para MO2")
        }
        Repeater {
            model: modResult.mod.files || []
            Text {
                required property var modelData
                Layout.fillWidth: true
                elide: Text.ElideMiddle
                font.pixelSize: Theme.small
                color: Theme.textMuted
                text: modelData.path + " · " + modelData.bytes + " B · " + String(modelData.sha256).substring(0, 16) + "…"
            }
        }
        Repeater {
            model: (modResult.mod.verification || []).concat(modResult.mod.pendingVerification || [])
            Text {
                required property var modelData
                required property int index
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.pixelSize: Theme.small
                color: index < modResult.checked ? Theme.textDim : Theme.warn
                text: (index < modResult.checked ? "✓ " : qsTr("Pendiente: ")) + modelData
            }
        }
        Repeater {
            model: modResult.mod.issues || []
            Text {
                required property var modelData
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.pixelSize: Theme.small
                color: modelData.severity === "warning" ? Theme.warn : Theme.textDim
                text: modelData.message
            }
        }
        Text {
            Layout.fillWidth: true
            visible: root.conversion.modState === "done"
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.small
            color: Theme.warn
            text: qsTr("Instálalo solo en un perfil de MO2 de prueba aislado (Instalar mod desde archivo → este ZIP). La aplicación no "
                       + "toca MO2 ni Skyrim\\Data. La carga en el juego requiere tu observación.")
        }
    }
}
