import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import SkyrimNpcStudio

// BodySlide presets for the selected NPC through Body Change NG (studio.body = BodyDirector).
// Operation A (assign a preset of the NPC's own body family) is kept apart from operation B (changing the body family),
// which is only evaluated here. Nothing is written until "Exportar distribución", which builds a NEW mod folder.
ColumnLayout {
    id: root
    objectName: "bodyPresetSection"
    required property StudioController studio
    readonly property var director: root.studio.body
    readonly property var env: root.director.environment
    readonly property var npc: root.director.npc
    readonly property var draft: root.director.draft
    readonly property var result: root.director.exportResult
    // What the 3D scene really did with BodySlide data (SceneViewModel.bodyMorphSummary / bodyMorphs).
    readonly property var morph: root.studio.scene.bodyMorphSummary
    readonly property var previewState: root.director.preview
    property string selectedUid: ""
    function familyText(v) {
        if (v === "compatible") return qsTr("Familia compatible")
        if (v === "partial") return qsTr("Compatibilidad parcial")
        if (v === "incompatible") return qsTr("Familia incompatible")
        return qsTr("Sin datos de familia")
    }
    function familyTone(v) {
        if (v === "compatible") return Theme.ok
        if (v === "partial") return Theme.warn
        if (v === "incompatible") return Theme.error
        return Theme.textDim
    }
    function sameMesh(a, b) { return !!a && !!b && a.toLowerCase().replace(/\//g, "\\") === b.toLowerCase().replace(/\//g, "\\") }
    readonly property var selected: {
        const list = root.director.presets
        for (let i = 0; i < list.length; ++i) if (list[i].uid === root.selectedUid) return list[i]
        return null
    }
    property string assignMessage: ""
    spacing: Theme.spacingMd
    Layout.fillWidth: true

    function verdictTone(v) {
        if (v === "compatible") return Theme.ok
        if (v === "experimental") return Theme.warn
        if (v === "needs-resources") return Theme.warn
        if (v === "blocked") return Theme.error
        return Theme.textDim
    }
    function applyFilter() {
        root.director.setFilter(searchField.text, familyBox.currentValue, assignableOnly.checked)
    }

    FolderDialog {
        id: folderDialog
        title: qsTr("Carpeta donde crear el mod (fuera de MO2 y de Skyrim)")
        onAccepted: outputField.text = selectedFolder.toString().replace(/^file:\/{3}/, "")
    }

    Text {
        text: qsTr("Presets de BodySlide (Body Change NG)")
        color: Theme.accent
        font.bold: true
        font.pixelSize: Theme.fs(13)
        Accessible.role: Accessible.Heading
    }
    Text {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        color: Theme.textDim
        font.pixelSize: Theme.small
        text: qsTr("Asigna al NPC un preset de su misma familia corporal. Cambiar de familia (3BA ↔ UBE) es otra operación y aquí solo se evalúa.")
    }

    // ---------------------------------------------------------------- profile
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: envCol.implicitHeight + 16
        color: Theme.panelAlt
        radius: Theme.radius
        border.color: Theme.border

        ColumnLayout {
            id: envCol
            anchors.fill: parent
            anchors.margins: 8
            spacing: 4

            Text {
                visible: root.env.state !== "ready"
                text: root.env.state === "loading" ? qsTr("Leyendo presets y Body Change NG del perfil…")
                      : root.env.state === "error" ? qsTr("Error: %1").arg(root.env.error)
                      : qsTr("Carga un perfil de MO2.")
                color: root.env.state === "error" ? Theme.error : Theme.textDim
                font.pixelSize: Theme.small
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Flow {
                visible: root.env.state === "ready"
                Layout.fillWidth: true
                spacing: 6
                StatusBadge {
                    text: root.env.bcng ? qsTr("Body Change NG ✓") : qsTr("Sin Body Change NG")
                    tone: root.env.bcng ? Theme.ok : Theme.error
                    ToolTip.visible: bcngHover.hovered
                    ToolTip.text: root.env.bcng ? qsTr("SKSE\\Plugins\\BodyChangeNG.dll de «%1»").arg(root.env.bcng) : qsTr("SKSE\\Plugins\\BodyChangeNG.dll no está en el perfil")
                    HoverHandler { id: bcngHover }
                }
                StatusBadge {
                    visible: !!root.env.obody
                    text: qsTr("OBody NG activo (choque)")
                    tone: Theme.error
                }
                StatusBadge {
                    text: root.env.racemenu ? qsTr("RaceMenu BodyMorph") : qsTr("Sin RaceMenu")
                    tone: root.env.racemenu ? Theme.ok : Theme.warn
                }
                StatusBadge {
                    text: qsTr("Familia global: %1").arg(root.env.globalFamily ?? "")
                    tone: Theme.textDim
                }
            }
            Text {
                visible: root.env.state === "ready"
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.text
                font.pixelSize: Theme.small
                text: qsTr("%1 presets en %2 archivos (%3 nombres repetidos, %4 archivos ilegibles). Frameworks activos: %5.")
                      .arg(root.env.presets).arg(root.env.files).arg(root.env.duplicates).arg(root.env.unreadableFiles)
                      .arg(root.env.installedFamilies)
            }
            Text {
                visible: root.env.state === "ready"
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: root.env.distributionError ? Theme.error : Theme.textDim
                font.pixelSize: Theme.small
                text: root.env.distributionError ? qsTr("Distribución actual ilegible (%1): %2").arg(root.env.distributionProvider).arg(root.env.distributionError)
                      : root.env.distributionProvider ? qsTr("Distribución actual: %1 regla(s) de «%2».").arg(root.env.distributionRules).arg(root.env.distributionProvider)
                      : qsTr("El perfil no tiene BodyChangeNGdistribution.json.")
            }
        }
    }

    // ---------------------------------------------------------------- NPC body
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: npcCol.implicitHeight + 16
        color: Theme.panelAlt
        radius: Theme.radius
        border.color: Theme.border

        ColumnLayout {
            id: npcCol
            anchors.fill: parent
            anchors.margins: 8
            spacing: 4

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Text {
                    text: root.npc.state === "ready" ? qsTr("Cuerpo de %1").arg(root.npc.name || root.npc.editorId)
                          : root.npc.state === "inspecting" ? qsTr("Analizando el cuerpo del NPC…")
                          : root.npc.state === "error" ? qsTr("No se pudo analizar el cuerpo")
                          : qsTr("Elige un NPC para ver su cuerpo")
                    color: Theme.text
                    font.bold: true
                    font.pixelSize: Theme.normal
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                StatusBadge {
                    visible: root.npc.state === "ready"
                    text: root.npc.familyLabel ?? ""
                    tone: root.npc.family === "unknown" ? Theme.warn : Theme.accent
                }
            }
            Text {
                visible: root.npc.state === "error"
                text: root.npc.error ?? ""
                color: Theme.error
                font.pixelSize: Theme.small
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Text {
                visible: root.npc.state === "ready"
                Layout.fillWidth: true
                wrapMode: Text.WrapAnywhere
                color: Theme.textDim
                font.pixelSize: Theme.small
                text: root.npc.state !== "ready" ? "" : qsTr("Malla: %1 (%2)").arg(root.npc.body.path || qsTr("ninguna"))
                      .arg(root.npc.body.provider || qsTr("no está en el perfil"))
                      + "\n" + (root.npc.body.tri ? qsTr("TRI de morphs: %1 (%2)").arg(root.npc.body.tri).arg(root.npc.body.triProvider || qsTr("falta"))
                                                  : qsTr("Sin TRI de morphs (BODYTRI)"))
            }
            Repeater {
                model: root.npc.state === "ready" ? root.npc.evidence : []
                delegate: Text {
                    required property var modelData
                    Layout.fillWidth: true
                    text: "• " + modelData
                    color: Theme.textDim
                    font.pixelSize: Theme.small
                    wrapMode: Text.WordWrap
                }
            }
            IssueList {
                visible: root.npc.state === "ready"
                issues: root.npc.issues ?? []
                emptyText: qsTr("El cuerpo se identificó sin incidencias.")
            }
        }
    }

    // ---------------------------------------------------------------- 3D viewer: real geometry only
    Rectangle {
        objectName: "bodyMorphPanel"
        Layout.fillWidth: true
        implicitHeight: morphCol.implicitHeight + 16
        color: Theme.panelAlt
        radius: Theme.radius
        border.color: root.morph.verdict === "applied" ? Theme.ok : root.morph.verdict === "partial" ? Theme.warn
                      : root.morph.verdict === "not-applied" ? Theme.error : Theme.border

        ColumnLayout {
            id: morphCol
            anchors.fill: parent
            anchors.margins: 8
            spacing: 4

            Text {
                text: qsTr("Visor corporal (geometría real)")
                color: Theme.accent
                font.bold: true
                font.pixelSize: Theme.normal
                Accessible.role: Accessible.Heading
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.text
                font.pixelSize: Theme.small
                text: root.studio.scene.loading ? qsTr("Construyendo la escena…") : (root.morph.label ?? qsTr("Sin escena cargada."))
            }

            // -- effective body
            Text { text: qsTr("Cuerpo efectivo"); color: Theme.text; font.bold: true; font.pixelSize: Theme.small }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WrapAnywhere
                color: Theme.textDim
                font.pixelSize: Theme.small
                text: {
                    const b = root.morph.body
                    let s = qsTr("Familia: %1").arg(root.npc.state === "ready" ? root.npc.familyLabel : qsTr("—"))
                    s += "\n" + qsTr("Malla (visor): %1 · %2").arg(b ? b.meshPath : qsTr("ninguna")).arg(b ? b.provider : "")
                    if (b && !b.shown) s += qsTr(" (oculta bajo la ropa)")
                    s += "\n" + qsTr("Esqueleto: %1 · %2").arg(root.morph.skeleton || qsTr("—")).arg(root.morph.skeletonProvider || "")
                    s += "\n" + qsTr("Preset: %1").arg(root.morph.preset
                          ? root.morph.preset + (root.morph.presetMode === "preview" ? qsTr(" (previsualización)") : qsTr(" (regla asignada)"))
                          : qsTr("ninguno"))
                    s += "\n" + qsTr("Peso en el visor: %1 (registro: %2)").arg(Math.round(root.morph.weight ?? 0)).arg(Math.round(root.morph.recordWeight ?? 0))
                    return s
                }
            }
            StatusBadge {
                // The inspector (worker body.inspectNpcs + ARMA) and the viewer (worker getAppearance) must resolve the same mesh.
                readonly property bool same: root.sameMesh(root.npc.body ? root.npc.body.path : "", root.morph.body ? root.morph.body.meshPath : "")
                visible: root.npc.state === "ready" && !!root.morph.body
                text: same ? qsTr("Inspector y visor: misma malla") : qsTr("Inspector y visor: mallas distintas")
                tone: same ? Theme.ok : Theme.error
                ToolTip.visible: meshHover.hovered
                ToolTip.text: qsTr("Inspector: %1\nVisor: %2").arg(root.npc.body ? root.npc.body.path : "").arg(root.morph.body ? root.morph.body.meshPath : "")
                HoverHandler { id: meshHover }
            }

            // -- real morphs of the body
            Text { text: qsTr("Morphs reales del cuerpo"); color: Theme.text; font.bold: true; font.pixelSize: Theme.small }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WrapAnywhere
                color: Theme.textDim
                font.pixelSize: Theme.small
                text: {
                    const b = root.morph.body
                    if (!b) return qsTr("Sin malla corporal en la escena.")
                    let s = qsTr("%1 sliders disponibles · %2 compatibles con el preset · %3 aplicados").arg(b.morphs).arg(b.compatible).arg(b.applied)
                    s += "\n" + (b.triPath ? qsTr("TRI: %1 · %2").arg(b.triPath).arg(b.triProvider || qsTr("falta")) : qsTr("Sin BODYTRI"))
                    if (b.missing && b.missing.length) s += "\n" + qsTr("Sin morph: %1").arg(b.missing.slice(0, 12).join(", ")) + (b.missing.length > 12 ? "…" : "")
                    return s
                }
            }
            StatusBadge {
                visible: !!root.morph.body
                text: root.morph.body ? root.morph.body.statusLabel : ""
                tone: !root.morph.body ? Theme.textDim
                      : (root.morph.body.status === "applied" || root.morph.body.status === "available") ? Theme.ok
                      : (root.morph.body.status === "partial" || root.morph.body.status === "no-change") ? Theme.warn : Theme.error
            }

            // -- outfit
            Text {
                text: qsTr("Vestuario")
                color: Theme.text
                font.bold: true
                font.pixelSize: Theme.small
                visible: (root.morph.outfit ?? []).length > 0
            }
            Repeater {
                model: root.morph.outfit ?? []
                delegate: ColumnLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 2
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Text {
                            Layout.fillWidth: true
                            text: modelData.piece
                            color: Theme.text
                            font.pixelSize: Theme.small
                            elide: Text.ElideRight
                        }
                        StatusBadge { text: root.familyText(modelData.family); tone: root.familyTone(modelData.family) }
                        StatusBadge { visible: modelData.needsRefit; text: qsTr("Necesita refit"); tone: Theme.error }
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.textDim
                        font.pixelSize: Theme.micro
                        text: qsTr("%1 de %2 formas con morphs · %3 deformadas · %4").arg(modelData.withMorphs).arg(modelData.shapes)
                              .arg(modelData.changed).arg(modelData.statuses.join(" / "))
                              + (modelData.familyReason ? "\n" + modelData.familyReason : "")
                              + (modelData.details.length ? "\n" + modelData.details.join("\n") : "")
                    }
                }
            }

            // -- BodySlide builds: is each mesh the base RaceMenu morphs expect, or does it carry a baked preset?
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Button {
                    objectName: "bodyVerifyBuilds"
                    text: qsTr("Verificar builds")
                    enabled: root.studio.scene.bodyMorphs.length > 0 && root.director.buildCheck.state !== "checking"
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Reconstruye en memoria la base de cada malla desde su proyecto BodySlide (OSP/OSD) y la compara vértice a vértice")
                    onClicked: {
                        const meshes = []
                        for (const r of root.studio.scene.bodyMorphs) if (r.meshPath && meshes.indexOf(r.meshPath) < 0) meshes.push(r.meshPath)
                        root.director.verifyBuilds(meshes)
                    }
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textDim
                    font.pixelSize: Theme.micro
                    text: root.director.buildCheck.state === "checking" ? qsTr("Comprobando %1 malla(s)…").arg(root.director.buildCheck.meshes)
                          : root.director.buildCheck.state === "error" ? root.director.buildCheck.error
                          : root.director.buildCheck.state === "ready" ? qsTr("%1 proyectos BodySlide en el perfil").arg(root.director.buildCheck.projects)
                          : ""
                }
            }
            Repeater {
                model: root.director.buildCheck.state === "ready" ? root.director.buildCheck.rows : []
                delegate: ColumnLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 1
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Text {
                            Layout.fillWidth: true
                            text: modelData.mesh.split("\\").pop()
                            color: Theme.text
                            font.pixelSize: Theme.micro
                            elide: Text.ElideMiddle
                        }
                        StatusBadge {
                            text: modelData.verdict === "base" ? qsTr("Base sin preset")
                                  : modelData.verdict === "preset-baked" ? qsTr("Preset horneado")
                                  : modelData.verdict === "no-project" ? qsTr("Sin proyecto")
                                  : modelData.verdict === "not-comparable" ? qsTr("No comparable") : qsTr("Error")
                            tone: modelData.verdict === "base" ? Theme.ok : modelData.verdict === "preset-baked" ? Theme.error : Theme.textDim
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.textDim
                        font.pixelSize: Theme.micro
                        text: modelData.provider + " · " + modelData.detail
                    }
                }
            }

            // -- preview controls
            Text { text: qsTr("Previsualización"); color: Theme.text; font.bold: true; font.pixelSize: Theme.small }
            Flow {
                Layout.fillWidth: true
                spacing: 6
                Button {
                    objectName: "bodyViewOriginal"
                    text: qsTr("Original")
                    checkable: true
                    checked: root.studio.scene.original
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("El perfil tal como está: sin preset, sin borrador")
                    onClicked: root.studio.scene.original = true
                }
                Button {
                    objectName: "bodyViewPreset"
                    text: qsTr("Con preset")
                    checkable: true
                    checked: !root.studio.scene.original && !!root.morph.preset
                    enabled: !!root.previewState.presetUid || !!root.selectedUid
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Antes/después: alterna con «Original» para comparar en la misma cámara")
                    onClicked: {
                        root.studio.scene.original = false
                        if (!root.previewState.presetUid && root.selectedUid) root.director.previewPreset(root.selectedUid)
                    }
                }
                Button {
                    text: qsTr("Quitar")
                    enabled: !!root.previewState.presetUid
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Quita la previsualización (la regla asignada, si la hay, se mantiene)")
                    onClicked: root.director.previewPreset("")
                }
                CheckBox {
                    objectName: "bodyShowOutfit"
                    text: qsTr("Vestuario")
                    checked: root.previewState.showOutfit ?? true
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Desmarca para ver el cuerpo sin ropa")
                    onToggled: root.director.setShowOutfit(checked)
                }
            }
            Text {
                // Equipped pieces with no ARMA for the NPC's race: they hold their slots, so the skin there is hidden and nothing replaces it.
                readonly property var pieces: (root.studio.scene.outfitDiagnostics || []).filter(d => d.noAddonForRace).map(d => d.name)
                objectName: "bodyPiecesWithoutAddon"
                Layout.fillWidth: true
                visible: pieces.length > 0
                text: qsTr("El cuerpo no se ve porque estas prendas no tienen ARMA para la raza del NPC y no dibujan nada: %1. Desmarca «Vestuario» para ver el cuerpo; para vestirla hace falta un atuendo convertido o un parche para esa raza.").arg(pieces.join(", "))
                color: Theme.warn
                font.pixelSize: Theme.small
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                CheckBox {
                    id: weightOverride
                    objectName: "bodyWeightOverride"
                    text: qsTr("Peso")
                    checked: (root.previewState.weight ?? -1) >= 0
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Usa este peso en lugar del del registro del NPC (mallas _0/_1 y valores small/big del preset)")
                    onToggled: root.director.setPreviewWeight(checked ? weightSlider.value : -1)
                }
                Slider {
                    id: weightSlider
                    objectName: "bodyWeightSlider"
                    Layout.fillWidth: true
                    from: 0; to: 100; stepSize: 5
                    value: (root.previewState.weight ?? -1) >= 0 ? root.previewState.weight : (root.morph.recordWeight ?? 50)
                    enabled: weightOverride.checked
                    Accessible.name: qsTr("Peso del NPC en el visor")
                    onMoved: root.director.setPreviewWeight(value)
                }
                Text { text: Math.round(weightSlider.value); color: Theme.text; font.pixelSize: Theme.small }
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.textDim
                font.pixelSize: Theme.micro
                text: qsTr("El visor usa los TRI que el juego usa (RaceMenu BodyMorph). Nada de esto prueba el resultado dentro de Skyrim: PENDIENTE_SKYRIM.")
            }
        }
    }

    // ---------------------------------------------------------------- operation A: presets
    Text {
        text: qsTr("Asignar un preset")
        color: Theme.text
        font.bold: true
        font.pixelSize: Theme.normal
        Accessible.role: Accessible.Heading
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: 6
        TextField {
            id: searchField
            objectName: "bodyPresetSearch"
            Layout.fillWidth: true
            Layout.minimumWidth: 80
            placeholderText: qsTr("Buscar preset, archivo o mod…")
            Accessible.name: qsTr("Buscar presets de BodySlide")
            onTextChanged: searchTimer.restart()
            Timer { id: searchTimer; interval: 200; onTriggered: root.applyFilter() }
        }
        ComboBox {
            id: familyBox
            Layout.preferredWidth: Math.min(implicitWidth, root.width * 0.45)
            Layout.minimumWidth: 90
            textRole: "text"
            valueRole: "value"
            Accessible.name: qsTr("Filtrar por familia corporal")
            model: [
                { text: qsTr("Todas las familias"), value: "" },
                { text: "CBBE 3BA", value: "3ba" },
                { text: "CBBE", value: "cbbe" },
                { text: "UBE", value: "ube" },
                { text: qsTr("Otras"), value: "other" },
                { text: qsTr("Desconocida"), value: "unknown" }
            ]
            onActivated: root.applyFilter()
        }
    }
    CheckBox {
        id: assignableOnly
        text: qsTr("Solo asignables")
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Oculta los presets bloqueados y los que requieren recursos que el perfil no tiene")
        onToggled: root.applyFilter()
    }
    Text {
        text: qsTr("%1 presets").arg(root.director.presetTotal) + (root.director.presetTotal > root.director.presets.length
              ? qsTr(" (se muestran %1; afina la búsqueda)").arg(root.director.presets.length) : "")
        color: Theme.textDim
        font.pixelSize: Theme.small
    }

    ListView {
        id: presetList
        objectName: "bodyPresetList"
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(contentHeight, Theme.rowHeight * 8)
        clip: true
        model: root.director.presets
        boundsBehavior: Flickable.StopAtBounds
        keyNavigationEnabled: true
        Accessible.name: qsTr("Presets de BodySlide del perfil")
        ScrollBar.vertical: ScrollBar { Accessible.name: qsTr("Desplazar presets") }
        delegate: ItemDelegate {
            required property var modelData
            required property int index
            width: presetList.width
            highlighted: modelData.uid === root.selectedUid
            Accessible.name: modelData.name + ", " + modelData.familyLabel + ", " + modelData.verdictLabel
            onClicked: { root.selectedUid = modelData.uid; root.assignMessage = ""; presetList.currentIndex = index }
            contentItem: RowLayout {
                spacing: 6
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Text {
                        Layout.fillWidth: true
                        text: (modelData.assigned ? "✔ " : "") + modelData.name
                        color: Theme.text
                        font.pixelSize: Theme.small
                        font.bold: modelData.assigned
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: modelData.provider + " · " + modelData.source
                        color: Theme.textDim
                        font.pixelSize: Theme.micro
                        elide: Text.ElideMiddle
                    }
                }
                StatusBadge { text: modelData.familyLabel; tone: Theme.textDim }
                StatusBadge {
                    visible: modelData.assigned
                    text: qsTr("Regla")
                    tone: Theme.accent
                    ToolTip.visible: ruleHover.hovered
                    ToolTip.text: qsTr("Asignado en el borrador de Body Change NG. Lo que el visor deformó de verdad se ve en «Visor corporal».")
                    HoverHandler { id: ruleHover }
                }
                StatusBadge {
                    visible: modelData.verdict !== ""
                    text: modelData.verdictLabel
                    tone: root.verdictTone(modelData.verdict)
                }
            }
        }
    }

    Rectangle {
        visible: root.selected !== null
        Layout.fillWidth: true
        implicitHeight: selCol.implicitHeight + 16
        color: Theme.panelAlt
        radius: Theme.radius
        border.color: root.selected ? root.verdictTone(root.selected.verdict) : Theme.border

        ColumnLayout {
            id: selCol
            anchors.fill: parent
            anchors.margins: 8
            spacing: 4
            Text {
                Layout.fillWidth: true
                text: root.selected ? root.selected.name : ""
                color: Theme.text
                font.bold: true
                font.pixelSize: Theme.normal
                wrapMode: Text.WordWrap
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WrapAnywhere
                color: Theme.textDim
                font.pixelSize: Theme.small
                text: root.selected ? qsTr("Archivo: %1\nMod: %2\nFamilia: %3 — %4\n%5 sliders%6")
                      .arg(root.selected.file).arg(root.selected.provider).arg(root.selected.familyLabel).arg(root.selected.reason)
                      .arg(root.selected.sliders).arg(root.selected.duplicate ? qsTr(" · nombre repetido en otro archivo") : "") : ""
            }
            IssueList {
                issues: root.selected ? root.selected.issues : []
                emptyText: qsTr("Sin incidencias para este NPC.")
            }
            CheckBox {
                id: acceptExperimental
                visible: root.selected !== null && root.selected.verdict === "experimental"
                text: qsTr("Acepto que es experimental")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("El resultado solo se sabrá dentro del juego; lo probaré en un perfil de prueba")
            }
            RowLayout {
                spacing: 8
                Button {
                    objectName: "bodyPreviewButton"
                    text: qsTr("Previsualizar")
                    enabled: root.selected !== null && root.npc.state === "ready"
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Deforma en el visor el cuerpo y cada prenda con sus propios morphs reales (TRI), sin crear ninguna regla")
                    onClicked: {
                        root.studio.scene.original = false
                        root.director.previewPreset(root.selectedUid)
                    }
                }
                Button {
                    objectName: "bodyAssignButton"
                    text: (root.selected && root.selected.assigned) ? qsTr("Reasignar") : qsTr("Asignar")
                    enabled: root.selected !== null && root.npc.state === "ready"
                             && (root.selected.verdict === "compatible" || (root.selected.verdict === "experimental" && acceptExperimental.checked))
                    Accessible.name: root.npc.state === "ready" ? qsTr("Asignar el preset a %1").arg(root.npc.name || root.npc.editorId) : text
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Crea la regla de Body Change NG en el borrador. El visor solo deforma con morphs reales; el resultado está en «Visor corporal»")
                    onClicked: root.assignMessage = root.director.assign(root.selectedUid, acceptExperimental.checked)
                }
                Button {
                    visible: root.selected !== null && root.selected.assigned
                    text: qsTr("Desasignar")
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Quita la regla y restaura el cuerpo base en el visor 3D")
                    onClicked: {
                        root.director.unassign()
                        root.assignMessage = ""
                    }
                }
                StatusBadge {
                    // The scene's own verdict, not the click: real morphs on a visible shape, or why not.
                    visible: root.selected !== null && root.morph.preset === root.selected.name
                    text: root.morph.verdict === "applied" ? qsTr("Morphs reales aplicados")
                          : root.morph.verdict === "partial" ? qsTr("Aplicado; hay prendas sin ajustar")
                          : qsTr("No aplicado en 3D")
                    tone: root.morph.verdict === "applied" ? Theme.ok : root.morph.verdict === "partial" ? Theme.warn : Theme.error
                }
            }
            Text {
                Layout.fillWidth: true
                visible: root.assignMessage !== ""
                text: root.assignMessage
                color: Theme.error
                font.pixelSize: Theme.small
                wrapMode: Text.WordWrap
            }
        }
    }

    // ---------------------------------------------------------------- operation B: family change (evaluation only)
    Text {
        text: qsTr("Cambiar de familia corporal (solo evaluación)")
        color: Theme.text
        font.bold: true
        font.pixelSize: Theme.normal
        Accessible.role: Accessible.Heading
    }
    Repeater {
        model: root.npc.state === "ready" ? root.npc.changes : []
        delegate: Rectangle {
            required property var modelData
            Layout.fillWidth: true
            implicitHeight: changeCol.implicitHeight + 12
            color: Theme.panel
            radius: Theme.radius
            border.color: Theme.border
            ColumnLayout {
                id: changeCol
                anchors.fill: parent
                anchors.margins: 6
                spacing: 3
                RowLayout {
                    Text {
                        text: qsTr("A %1").arg(modelData.label)
                        color: Theme.text
                        font.pixelSize: Theme.small
                        font.bold: true
                    }
                    Item { Layout.fillWidth: true }
                    StatusBadge { text: modelData.verdictLabel; tone: root.verdictTone(modelData.verdict) }
                }
                IssueList { issues: modelData.issues }
            }
        }
    }

    // ---------------------------------------------------------------- draft and export
    Text {
        text: qsTr("Distribución a exportar")
        color: Theme.text
        font.bold: true
        font.pixelSize: Theme.normal
        Accessible.role: Accessible.Heading
    }
    Text {
        visible: (root.draft.rules ?? []).length === 0
        text: qsTr("Todavía no hay reglas. Asigna un preset a un NPC para crear una.")
        color: Theme.textDim
        font.pixelSize: Theme.small
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }
    Repeater {
        model: root.draft.rules ?? []
        delegate: RowLayout {
            required property var modelData
            Layout.fillWidth: true
            spacing: 6
            Text {
                Layout.fillWidth: true
                text: qsTr("%1 → %2%3").arg(modelData.npcName).arg(modelData.preset).arg(modelData.file ? " (" + modelData.file + ")" : "")
                color: Theme.text
                font.pixelSize: Theme.small
                wrapMode: Text.WordWrap
            }
            StatusBadge { text: modelData.verdictLabel; tone: root.verdictTone(modelData.verdict) }
            Button {
                text: qsTr("Quitar")
                Accessible.name: qsTr("Quitar la regla de %1").arg(modelData.npcName)
                onClicked: root.director.removeRule(modelData.id)
            }
        }
    }
    Text {
        visible: (root.draft.rules ?? []).length > 0
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        color: Theme.textDim
        font.pixelSize: Theme.small
        text: root.draft.baseProvider ? qsTr("Se combinará con las %1 reglas de «%2» (las del estudio van primero): %3 reglas en total.")
                                         .arg(root.draft.baseRules).arg(root.draft.baseProvider).arg(root.draft.mergedRules)
                                       : qsTr("%1 reglas en total.").arg(root.draft.mergedRules)
    }
    Repeater {
        model: root.draft.errors ?? []
        delegate: Text {
            required property var modelData
            Layout.fillWidth: true
            text: "✖ " + modelData
            color: Theme.error
            font.pixelSize: Theme.small
            wrapMode: Text.WordWrap
        }
    }
    GridLayout {
        visible: (root.draft.rules ?? []).length > 0
        Layout.fillWidth: true
        columns: 2
        columnSpacing: 6
        rowSpacing: 4
        Text { text: qsTr("Nombre del mod"); color: Theme.textDim; font.pixelSize: Theme.small }
        TextField {
            id: modNameField
            Layout.fillWidth: true
            Layout.minimumWidth: 60
            text: "SNS Body Distribution"
            Accessible.name: qsTr("Nombre del mod a crear")
        }
        Text { text: qsTr("Carpeta"); color: Theme.textDim; font.pixelSize: Theme.small }
        RowLayout {
            Layout.fillWidth: true
            TextField {
                id: outputField
                Layout.fillWidth: true
                Layout.minimumWidth: 60
                text: root.director.defaultExportDirectory
                Accessible.name: qsTr("Carpeta donde se crea el mod")
            }
            Button {
                text: qsTr("Elegir…")
                Accessible.name: qsTr("Elegir carpeta de exportación")
                onClicked: folderDialog.open()
            }
        }
    }
    CheckBox {
        id: zipBox
        visible: (root.draft.rules ?? []).length > 0
        checked: true
        text: qsTr("Crear también un ZIP")
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Un ZIP que se instala con «Instalar mod desde archivo» de MO2")
    }
    RowLayout {
        visible: (root.draft.rules ?? []).length > 0
        Layout.fillWidth: true
        Item { Layout.fillWidth: true }
        Button {
            text: qsTr("Vaciar")
            Accessible.name: qsTr("Vaciar el borrador de la distribución")
            onClicked: root.director.clearDraft()
        }
        Button {
            objectName: "bodyExportButton"
            text: root.result.state === "exporting" ? qsTr("Exportando…") : qsTr("Exportar")
            enabled: (root.draft.errors ?? []).length === 0 && root.result.state !== "exporting"
            highlighted: true
            Accessible.name: qsTr("Exportar la distribución de Body Change NG como un mod nuevo")
            onClicked: root.director.exportDistribution(modNameField.text, outputField.text, zipBox.checked)
        }
    }
    Rectangle {
        visible: root.result.state === "done" || root.result.state === "error"
        Layout.fillWidth: true
        implicitHeight: resCol.implicitHeight + 16
        color: Theme.panelAlt
        radius: Theme.radius
        border.color: root.result.state === "done" ? Theme.ok : Theme.error
        ColumnLayout {
            id: resCol
            anchors.fill: parent
            anchors.margins: 8
            spacing: 3
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WrapAnywhere
                color: root.result.state === "done" ? Theme.ok : Theme.error
                font.pixelSize: Theme.small
                font.bold: true
                text: root.result.state === "done" ? qsTr("Mod creado y verificado por lectura: %1").arg(root.result.modDirectory)
                                                   : qsTr("No se exportó: %1").arg(root.result.error ?? "")
            }
            Text {
                visible: root.result.state === "done" && !!root.result.zipPath
                Layout.fillWidth: true
                wrapMode: Text.WrapAnywhere
                color: Theme.textDim
                font.pixelSize: Theme.small
                text: qsTr("ZIP: %1").arg(root.result.zipPath ?? "")
            }
            Repeater {
                model: root.result.state === "done" ? root.result.files : []
                delegate: Text {
                    required property var modelData
                    Layout.fillWidth: true
                    wrapMode: Text.WrapAnywhere
                    color: Theme.textDim
                    font.pixelSize: Theme.micro
                    font.family: Theme.mono
                    text: modelData.path + " · " + modelData.bytes + " B · SHA-256 " + modelData.sha256
                }
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.warn
                font.pixelSize: Theme.small
                text: qsTr("Estado en el juego: PENDIENTE_SKYRIM. Instálalo en un perfil de prueba y compruébalo dentro de Skyrim.")
            }
        }
    }
}
