import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

// Face Source Studio: «Elegir NPC -> Examinar rostros -> Comparar -> Seleccionar -> Validar -> Crear parche».
// Left: the Face Source Index of the whole profile (studio.sources.faceIndex: every NPC with FaceGen, its loose and BSA
// copies, who wins). Right: every source of the selected NPC drawn by the studio's portrait stage with that source's
// files (cache first), its verdict, and what giving the NPC that face takes (studio.sources.transferPlan).
// Nothing here writes to MO2, the game or a mod: the only file it saves is the user's draft; «Crear parche» opens the
// existing assisted resolution (Easy Mod Creator, a separate mod with its own D1 review), and only when the plan allows it.
Popup {
    id: root
    objectName: "faceSourceStudio"
    required property StudioController studio
    readonly property AppearanceSources sources: studio.sources
    readonly property var index: sources.faceIndex
    readonly property var matrix: sources.matrix
    readonly property var candidates: matrix.state === "ready" ? (matrix.candidates || []) : []
    readonly property var draft: sources.draft
    property string selectedId: ""
    property var rows: []
    property int total: 0
    property string notice: ""
    property bool compared: false
    // Bumped on every matrix or draft change: the plan is recomputed (transferPlan is a call, not a property).
    property int planRevision: 0
    readonly property var plan: planRevision >= 0 && selectedId !== "" ? sources.transferPlan(selectedId) : ({})
    signal patchRequested()

    parent: Overlay.overlay
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    padding: 0
    width: parent ? Math.min(parent.width - 2 * Theme.spacingXl, Theme.px(1180)) : Theme.px(1000)
    height: parent ? Math.min(parent.height - 2 * Theme.spacingXl, Theme.px(780)) : Theme.px(700)
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    background: Rectangle { color: Theme.bg; radius: Theme.radiusLarge; border.color: Theme.border }

    onOpened: { sources.galleryOpen = true; resolveAllNames(); refresh(); syncSelection() }
    onClosed: sources.galleryOpen = false

    function flagLabel(flag) {
        return flag === "MultipleSources" ? qsTr("Varias fuentes")
             : flag === "TintFromOtherSource" ? qsTr("Tinte de otra fuente")
             : flag === "NoTint" ? qsTr("Sin FaceTint")
             : flag === "TintOnly" ? qsTr("Solo FaceTint")
             : flag === "ArchivedOnly" ? qsTr("Solo en BSA") : flag
    }
    function verdictLabel(v) {
        return v === "IN_USE" ? qsTr("En uso: no hace falta parche")
             : v === "PATCH_READY" ? qsTr("Parche posible")
             : v === "NEEDS_REVIEW" ? qsTr("Parche posible con revisión D1")
             : v === "NEEDS_LOAD_ORDER" ? qsTr("Requiere cambiar MO2")
             : v === "BLOCKED" ? qsTr("Bloqueado")
             : v === "WAITING" ? qsTr("Esperando la matriz…") : ""
    }
    function verdictTone(v) {
        return v === "IN_USE" || v === "PATCH_READY" ? Theme.ok : v === "BLOCKED" ? Theme.error : v === "" ? Theme.textDim : Theme.warn
    }
    function nameKnown(formKey) { return studio.npcNames[studio.npcKey(formKey)] !== undefined }
    function nameOf(formKey) { return studio.npcNames[studio.npcKey(formKey)] ?? "" }
    function resolveAllNames() {
        if (index.state !== "ready") return
        const all = sources.faceIndexFormKeys()
        if (all.length > 0) studio.resolveNpcNames(all)
    }
    function refresh() {
        if (index.state !== "ready") { rows = []; total = 0; return }
        const source = sourceFilter.currentIndex > 0 ? sourceFilter.currentText : ""
        const flags = ["", "MultipleSources", "TintFromOtherSource", "ArchivedOnly", "NoTint"]
        const result = sources.queryFaceIndex(search.text, source, onlyAlternatives.checked, flags[flagFilter.currentIndex], 300, studio.npcNames)
        rows = result.rows
        total = result.total
        // The rows on view get their names first (the catalog answers 200 FormKeys per call).
        if (rows.length > 0) studio.resolveNpcNames(rows.map(r => r.formKey))
    }
    // The card on view: the draft's choice, else what was selected, else the effective source.
    function syncSelection() {
        const ids = candidates.map(c => c.id)
        if (selectedId !== "" && ids.indexOf(selectedId) >= 0) return
        const chosen = draft.chosen ?? ""
        selectedId = chosen !== "" && ids.indexOf(chosen) >= 0 ? chosen : (ids.length > 0 ? ids[0] : "")
    }
    function selectNpc(formKey) {
        if (formKey === (matrix.formKey ?? "")) return
        selectedId = ""
        compared = false
        notice = ""
        studio.selectNpc(formKey)
    }
    function candidateById(id) {
        for (const c of candidates) if (c.id === id) return c
        return null
    }
    function originLabel(img) {
        const o = img.origin ?? ""
        return o === "cache" ? qsTr("Render del estudio · desde caché")
             : o === "studio-render" ? qsTr("Render del estudio")
             : o === "provisional" ? qsTr("Render anterior (provisional)") : ""
    }

    Connections {
        target: root.sources
        function onFaceIndexChanged() { root.resolveAllNames(); root.refresh() }
        function onMatrixChanged() { root.planRevision++; root.syncSelection() }
        function onDraftChanged() { root.planRevision++; root.syncSelection() }
    }
    Connections {
        target: root.studio
        // Each answer covers at most 200 FormKeys: ask for the next ones until every NPC of the index has its name.
        // Not restart(): batches keep arriving and would postpone the refresh until the last one.
        function onNpcNamesChanged() { if (root.opened) { root.resolveAllNames(); if (!refreshDelay.running) refreshDelay.start() } }
    }
    Timer { id: refreshDelay; interval: 200; onTriggered: root.refresh() }

    // Steps of the guided flow and whether each is done.
    readonly property var steps: [
        { title: qsTr("1 Elegir NPC"), done: (matrix.formKey ?? "") !== "" },
        { title: qsTr("2 Examinar"), done: matrix.state === "ready" },
        { title: qsTr("3 Comparar"), done: compared },
        { title: qsTr("4 Seleccionar"), done: draft.state === "fresh" && (draft.chosen ?? "") !== "" },
        { title: qsTr("5 Validar"), done: (plan.verdict ?? "") !== "" && (plan.verdict ?? "") !== "WAITING" && (plan.chosen ?? false) },
        { title: qsTr("6 Crear parche"), done: false }
    ]

    contentItem: ColumnLayout {
        spacing: Theme.spacingSm
        Accessible.role: Accessible.Dialog
        Accessible.name: qsTr("Estudio de rostros")
        Keys.onEscapePressed: root.close()

        // ---- header and steps
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.spacingMd
            Layout.bottomMargin: 0
            Text { text: qsTr("Estudio de rostros"); color: Theme.text; font.bold: true; font.pixelSize: Theme.large }
            Item { Layout.fillWidth: true }
            Button { objectName: "faceStudioClose"; text: qsTr("Cerrar"); onClicked: root.close() }
        }
        Flow {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingMd
            Layout.rightMargin: Theme.spacingMd
            spacing: Theme.spacingXs
            Repeater {
                model: root.steps
                delegate: Rectangle {
                    required property var modelData
                    required property int index
                    objectName: "faceStep" + (index + 1)
                    implicitWidth: stepText.implicitWidth + 16
                    implicitHeight: stepText.implicitHeight + 8
                    radius: Theme.radiusSmall
                    color: modelData.done ? Theme.accentSubtle : Theme.panel
                    border.color: modelData.done ? Theme.accentBorder : Theme.border
                    Text {
                        id: stepText
                        anchors.centerIn: parent
                        text: (parent.modelData.done ? "✓ " : "") + parent.modelData.title
                        color: parent.modelData.done ? Theme.text : Theme.textDim
                        font.pixelSize: Theme.small
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: Theme.spacingMd
            spacing: Theme.spacingMd

            // ---- left: the profile's faces
            ColumnLayout {
                Layout.preferredWidth: Theme.px(330)
                Layout.maximumWidth: Theme.px(330)
                Layout.fillHeight: true
                spacing: Theme.spacingXs
                Text {
                    objectName: "faceIndexState"
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textDim
                    font.pixelSize: Theme.micro
                    text: root.index.state === "scanning" ? qsTr("Buscando FaceGen en los mods y BSA del perfil…")
                        : root.index.state === "error" ? qsTr("No se pudo indexar: %1").arg(root.index.error)
                        : root.index.state !== "ready" ? qsTr("Carga un perfil de MO2.")
                        : qsTr("%1 NPC con FaceGen, %2 con rostros alternativos (%3 archivos sueltos, %4 en %5 BSA, %6 ms).")
                              .arg(root.index.npcs).arg(root.index.withAlternatives).arg(root.index.looseFiles)
                              .arg(root.index.archivedFiles).arg(root.index.archives).arg(root.index.scanMs)
                }
                TextField {
                    id: search
                    objectName: "faceIndexSearch"
                    Layout.fillWidth: true
                    placeholderText: qsTr("Buscar NPC, FormKey, plugin o mod")
                    onTextChanged: refreshDelay.restart()
                }
                RowLayout {
                    Layout.fillWidth: true
                    ComboBox {
                        id: sourceFilter
                        objectName: "faceIndexSource"
                        Layout.fillWidth: true
                        Layout.minimumWidth: 80
                        model: [qsTr("Todas las fuentes")].concat(root.index.sources || [])
                        onActivated: root.refresh()
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Solo NPC con una copia de este mod o BSA")
                    }
                    ComboBox {
                        id: flagFilter
                        objectName: "faceIndexFlag"
                        Layout.preferredWidth: Theme.px(130)
                        model: [qsTr("Cualquier estado"), qsTr("Varias fuentes"), qsTr("Tinte de otra fuente"), qsTr("Solo en BSA"), qsTr("Sin FaceTint")]
                        onActivated: root.refresh()
                    }
                }
                CheckBox {
                    id: onlyAlternatives
                    objectName: "faceIndexOnlyAlternatives"
                    text: qsTr("Solo con rostros alternativos")
                    checked: true
                    onToggled: root.refresh()
                }
                Text {
                    Layout.fillWidth: true
                    color: Theme.textMuted
                    font.pixelSize: Theme.micro
                    text: root.total > root.rows.length ? qsTr("Mostrando %1 de %2: afina la búsqueda.").arg(root.rows.length).arg(root.total)
                                                        : qsTr("%1 NPC").arg(root.total)
                }
                ListView {
                    id: npcList
                    objectName: "faceIndexList"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: root.rows
                    spacing: 2
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Rectangle {
                        id: npcRow
                        required property var modelData
                        readonly property bool known: root.nameKnown(modelData.formKey)
                        readonly property string name: root.nameOf(modelData.formKey)
                        // The catalog answered and has no NPC with this FormKey: a FaceGen without a record.
                        readonly property bool orphan: known && name === ""
                        readonly property bool current: modelData.formKey === (root.matrix.formKey ?? "")
                        objectName: "faceIndexRow_" + modelData.formKey
                        width: ListView.view.width
                        implicitHeight: rowColumn.implicitHeight + 8
                        radius: Theme.radiusSmall
                        color: current ? Theme.selection : rowMouse.containsMouse ? Theme.panelAlt : Theme.panel
                        border.color: current ? Theme.accentBorder : Theme.borderSubtle
                        opacity: orphan ? 0.6 : 1
                        ColumnLayout {
                            id: rowColumn
                            anchors.fill: parent
                            anchors.margins: 4
                            spacing: 1
                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    Layout.fillWidth: true
                                    text: npcRow.name !== "" ? npcRow.name : npcRow.modelData.formKey
                                    color: Theme.text
                                    font.bold: true
                                    font.pixelSize: Theme.small
                                    elide: Text.ElideRight
                                }
                                Text {
                                    visible: npcRow.modelData.alternatives > 0
                                    text: qsTr("+%1").arg(npcRow.modelData.alternatives)
                                    color: Theme.accent
                                    font.pixelSize: Theme.small
                                    font.bold: true
                                    ToolTip.visible: altMouse.containsMouse
                                    ToolTip.text: qsTr("%1 rostros alternativos").arg(npcRow.modelData.alternatives)
                                    MouseArea { id: altMouse; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                text: qsTr("%1 · gana «%2»%3").arg(npcRow.modelData.formKey).arg(npcRow.modelData.geometryWinner)
                                          .arg(npcRow.modelData.geometryArchived ? qsTr(" (BSA)") : "")
                                color: Theme.textDim
                                font.pixelSize: Theme.micro
                                elide: Text.ElideRight
                            }
                            Text {
                                Layout.fillWidth: true
                                visible: npcRow.orphan || npcRow.modelData.flags.length > 0
                                text: npcRow.orphan ? qsTr("Sin registro NPC en el perfil: FaceGen huérfano")
                                                    : npcRow.modelData.flags.map(f => root.flagLabel(f)).join(" · ")
                                color: npcRow.orphan || npcRow.modelData.flags.indexOf("TintFromOtherSource") >= 0 ? Theme.warn : Theme.textMuted
                                font.pixelSize: Theme.micro
                                wrapMode: Text.WordWrap
                            }
                        }
                        MouseArea {
                            id: rowMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            enabled: !npcRow.orphan
                            cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                            onClicked: root.selectNpc(npcRow.modelData.formKey)
                        }
                        Accessible.role: Accessible.ListItem
                        Accessible.name: (name || modelData.formKey) + ", " + qsTr("%1 alternativas").arg(modelData.alternatives)
                    }
                }
            }

            Rectangle { Layout.fillHeight: true; implicitWidth: 1; color: Theme.border }

            // ---- right: the sources of the selected NPC
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: Theme.spacingXs
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        objectName: "faceStudioNpc"
                        Layout.fillWidth: true
                        text: (root.matrix.formKey ?? "") === "" ? qsTr("Elige un NPC de la lista.")
                            : qsTr("%1 · %2").arg(root.matrix.name || root.nameOf(root.matrix.formKey) || root.matrix.formKey).arg(root.matrix.formKey)
                        color: Theme.text
                        font.bold: true
                        font.pixelSize: Theme.normal
                        elide: Text.ElideRight
                    }
                    Text {
                        objectName: "faceStudioMatrixState"
                        text: root.matrix.state === "gathering" ? qsTr("Reuniendo fuentes…")
                            : root.matrix.state === "ready" ? qsTr("%1 fuentes, %2 se dibujan").arg(root.candidates.length).arg(root.matrix.renderable) : ""
                        color: Theme.textDim
                        font.pixelSize: Theme.micro
                    }
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textMuted
                    font.pixelSize: Theme.micro
                    text: qsTr("Las imágenes las dibuja el estudio con los archivos FaceGen de cada fuente (misma cámara y luz). No son capturas de Skyrim: el resultado en el juego se comprueba aparte.")
                }

                GridView {
                    id: grid
                    objectName: "faceCardGrid"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: root.candidates
                    readonly property int columns: Math.max(1, Math.floor(width / Theme.px(210)))
                    cellWidth: Math.floor(width / columns)
                    cellHeight: Theme.px(300)
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Item {
                        id: cell
                        required property var modelData
                        readonly property var img: root.sources.imageOf(modelData.id, root.sources.imageRevision + root.studio.portraits.revision)
                        readonly property bool showImage: (img.url ?? "") !== "" && (img.state === "ready" || (img.origin ?? "") === "provisional")
                        readonly property bool selected: root.selectedId === modelData.id
                        readonly property bool isA: root.sources.candidateA === modelData.id
                        readonly property bool isB: root.sources.candidateB === modelData.id
                        width: grid.cellWidth
                        height: grid.cellHeight
                        Rectangle {
                            id: card
                            objectName: "faceCard_" + cell.modelData.id
                            anchors.fill: parent
                            anchors.margins: 4
                            radius: Theme.radius
                            color: Theme.panel
                            border.color: cell.selected ? Theme.accent : Theme.border
                            border.width: cell.selected ? 2 : 1
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 6
                                spacing: 3
                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    color: Theme.surface
                                    radius: Theme.radiusSmall
                                    Image {
                                        anchors.fill: parent
                                        anchors.margins: 2
                                        visible: cell.showImage
                                        source: cell.showImage ? cell.img.url : ""
                                        fillMode: Image.PreserveAspectFit
                                        asynchronous: true
                                        cache: false
                                        sourceSize.width: 400
                                    }
                                    Text {
                                        anchors.fill: parent
                                        anchors.margins: 8
                                        visible: !cell.showImage
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                        wrapMode: Text.WordWrap
                                        color: cell.img.state === "error" || cell.img.state === "unavailable" ? Theme.warn : Theme.textDim
                                        font.pixelSize: Theme.micro
                                        text: cell.img.state === "rendering" ? qsTr("Dibujando…")
                                            : cell.img.state === "preparing" ? qsTr("Leyendo archivos…")
                                            : cell.img.state === "error" ? qsTr("Error: %1").arg(cell.img.reason)
                                            : cell.img.state === "unavailable" ? cell.img.reason
                                            : !cell.modelData.renderable ? cell.modelData.renderNote : qsTr("En cola")
                                    }
                                    Text {
                                        anchors.left: parent.left
                                        anchors.bottom: parent.bottom
                                        anchors.margins: 3
                                        visible: cell.showImage
                                        text: root.originLabel(cell.img)
                                        color: Theme.textDim
                                        font.pixelSize: Theme.micro
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: cell.modelData.label
                                    color: Theme.text
                                    font.pixelSize: Theme.small
                                    font.bold: true
                                    elide: Text.ElideRight
                                    ToolTip.visible: labelMouse.containsMouse
                                    ToolTip.text: cell.modelData.label
                                    MouseArea { id: labelMouse; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: qsTr("Registro %1").arg(cell.modelData.recordPlugin || "?")
                                    color: Theme.textDim
                                    font.pixelSize: Theme.micro
                                    elide: Text.ElideRight
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 3
                                    SourceStatusBadge { status: cell.modelData.status }
                                    Item { Layout.fillWidth: true }
                                    Text {
                                        visible: cell.isA || cell.isB
                                        text: cell.isA ? "A" : "B"
                                        color: Theme.accent
                                        font.bold: true
                                        font.pixelSize: Theme.small
                                    }
                                }
                            }
                            MouseArea {
                                anchors.fill: parent
                                acceptedButtons: Qt.LeftButton
                                onClicked: root.selectedId = cell.modelData.id
                                onDoubleClicked: { root.selectedId = cell.modelData.id; root.sources.setCompare(root.sources.candidateA, cell.modelData.id) }
                            }
                            Accessible.role: Accessible.Button
                            Accessible.name: cell.modelData.label + ", " + cell.modelData.status
                        }
                    }
                }

                // ---- selection, comparison, validation and the patch
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: planColumn.implicitHeight + 12
                    visible: root.selectedId !== ""
                    color: Theme.panelAlt
                    radius: Theme.radius
                    border.color: Theme.border
                    ColumnLayout {
                        id: planColumn
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 4
                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                Layout.fillWidth: true
                                text: qsTr("Seleccionada: %1").arg(root.candidateById(root.selectedId)?.label ?? root.selectedId)
                                color: Theme.text
                                font.pixelSize: Theme.small
                                font.bold: true
                                elide: Text.ElideRight
                            }
                            Rectangle {
                                objectName: "faceTransferVerdict"
                                property string verdict: root.plan.verdict ?? ""
                                visible: verdict !== ""
                                implicitWidth: verdictText.implicitWidth + 12
                                implicitHeight: verdictText.implicitHeight + 6
                                radius: Theme.radiusSmall
                                color: "transparent"
                                border.color: root.verdictTone(verdict)
                                Text { id: verdictText; anchors.centerIn: parent; text: root.verdictLabel(parent.verdict); color: root.verdictTone(parent.verdict); font.pixelSize: Theme.micro; font.bold: true }
                            }
                        }
                        Text {
                            objectName: "faceTransferReasons"
                            Layout.fillWidth: true
                            visible: (root.plan.reasons || []).length > 0
                            text: (root.plan.reasons || []).map(r => "• " + r).join("\n")
                            color: Theme.textDim
                            font.pixelSize: Theme.micro
                            wrapMode: Text.WordWrap
                        }
                        Text {
                            objectName: "faceTransferActions"
                            Layout.fillWidth: true
                            visible: (root.plan.actions || []).length > 0
                            text: qsTr("Qué hacer:") + "\n" + (root.plan.actions || []).map((a, i) => (i + 1) + ". " + a).join("\n")
                            color: Theme.text
                            font.pixelSize: Theme.micro
                            wrapMode: Text.WordWrap
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: (root.plan.requirement ?? "") !== ""
                            text: root.plan.requirement ?? ""
                            color: Theme.error
                            font.pixelSize: Theme.micro
                            font.bold: true
                        }
                        Text {
                            objectName: "faceStudioNotice"
                            Layout.fillWidth: true
                            visible: root.notice !== ""
                            text: root.notice
                            color: Theme.warn
                            font.pixelSize: Theme.micro
                            wrapMode: Text.WordWrap
                        }
                        Flow {
                            Layout.fillWidth: true
                            spacing: Theme.spacingXs
                            Button {
                                objectName: "faceCompareButton"
                                text: qsTr("Comparar A/B…")
                                enabled: root.candidates.length >= 2
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("A = lo que carga el juego, B = la seleccionada")
                                onClicked: {
                                    const a = root.selectedId === "effective" ? root.sources.candidateA : "effective"
                                    const b = root.selectedId === "effective" ? root.sources.candidateB : root.selectedId
                                    root.sources.setCompare(a, b)
                                    root.compared = true
                                    compareLoader.active = true
                                    compareLoader.item.open()
                                }
                            }
                            Button {
                                objectName: "faceChooseButton"
                                text: qsTr("Elegir este rostro")
                                enabled: root.matrix.state === "ready" && !((root.draft.chosen ?? "") === root.selectedId && root.draft.state === "fresh")
                                onClicked: {
                                    if (root.selectedId !== "effective" && root.sources.candidateB !== root.selectedId)
                                        root.sources.setCompare("effective", root.selectedId)
                                    const why = root.sources.saveDraft(root.selectedId, "")
                                    root.notice = why === "" ? qsTr("Elección guardada (borrador del estudio; no cambia nada del perfil).") : why
                                }
                            }
                            Button {
                                objectName: "faceCreatePatchButton"
                                text: qsTr("Crear parche…")
                                enabled: root.plan.canCreatePatch === true
                                ToolTip.visible: hovered
                                ToolTip.text: root.plan.canCreatePatch === true ? qsTr("Abre el asistente: un mod aparte, con revisión cuando haga falta")
                                                                                : qsTr("Solo cuando la validación lo permite")
                                onClicked: {
                                    if (root.studio.prepareFaceResolution()) {
                                        root.close()
                                        root.patchRequested()
                                    } else {
                                        root.notice = qsTr("El asistente no aceptó el caso: vuelve a abrir el NPC y revisa la pestaña Origen.")
                                    }
                                }
                            }
                            Button {
                                objectName: "faceClearChoice"
                                text: qsTr("Quitar elección")
                                visible: (root.draft.chosen ?? "") !== ""
                                onClicked: { root.sources.clearDraft(); root.notice = "" }
                            }
                        }
                    }
                }
            }
        }
    }

    // Created on first use: a closed dialog still rebuilds its panes on every new matrix and every picture.
    Loader {
        id: compareLoader
        active: false
        sourceComponent: AppearanceSourceCompareDialog { studio: root.studio }
    }
}
