import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

// A/B comparison of the appearance sources of the selected NPC (studio.sources): the same identity (one FormKey) drawn
// with the FaceGen of the mod the game loads and of every other mod that ships one. Same stage, camera and lights for
// both sides; zoom and pan are shared. Read-only: choosing a source here changes no load order, MO2 profile, plugin,
// project or FaceGen and enables no export; the only thing saved is a draft of what the user looked at / prefers, in
// the studio's own folder. Applying a face is still the assisted resolution («Usar el rostro de X») with its D1 review.
Popup {
    id: dialog
    objectName: "sourceCompareDialog"
    required property StudioController studio
    readonly property AppearanceSources sources: studio.sources
    readonly property var candidates: sources.matrix.candidates || []
    // Shared view state of both sides.
    property real zoom: 1.0
    property real panX: 0
    property real panY: 0
    // "Alternar": one side at a time, so the eye sees what changes between A and B at the same spot.
    property bool blink: false
    property bool showingB: false
    property string notice: ""
    // "Varias a la vez": 3 or 4 sources side by side (a grid), same camera, lights, zoom and pan. The A/B view and its draft stay as they are.
    property bool multi: false
    property var multiIds: []
    readonly property int kMaxMulti: 4

    parent: Overlay.overlay
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    padding: 0
    width: parent ? Math.min(parent.width - 2 * Theme.spacingXl, Theme.px(1040)) : Theme.px(900)
    height: parent ? Math.min(parent.height - 2 * Theme.spacingXl, Theme.px(720)) : Theme.px(640)
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    background: Rectangle {
        color: Theme.bg
        radius: Theme.radiusLarge
        border.color: Theme.border
    }
    function resetView() { zoom = 1.0; panX = 0; panY = 0 }
    function applyZoom(factor) {
        if (factor === 0) { resetView(); return }
        zoom = Math.max(1.0, Math.min(6.0, zoom * factor))
        if (zoom === 1.0) { panX = 0; panY = 0 }
    }
    function applyPan(dx, dy) { if (zoom > 1.0) { panX += dx; panY += dy } }
    // Next source for the B side, in the order of the matrix (skips A so that B always differs from it).
    function cycleB(step) {
        const list = candidates
        if (list.length < 2) return
        let index = 0
        for (let i = 0; i < list.length; ++i) if (list[i].id === sources.candidateB) index = i
        for (let n = 0; n < list.length; ++n) {
            index = (index + step + list.length) % list.length
            if (list[index].id !== sources.candidateA) break
        }
        sources.setCompare(sources.candidateA, list[index].id)
    }
    function renderableIds() { return candidates.filter(c => c.renderable).map(c => c.id) }
    // The grid starts with A and B, then the other drawable sources in the order of the matrix, up to four.
    function setMulti(on) {
        multi = on
        if (!on) return
        blink = false
        const drawable = renderableIds()
        const ids = []
        for (const id of [sources.candidateA, sources.candidateB]) if (id && ids.indexOf(id) < 0 && drawable.indexOf(id) >= 0) ids.push(id)
        for (const id of drawable) if (ids.length < kMaxMulti && ids.indexOf(id) < 0) ids.push(id)
        multiIds = ids
        for (const id of ids) sources.requestImage(id)
    }
    function toggleMulti(id) {
        const ids = multiIds.slice()
        const at = ids.indexOf(id)
        if (at >= 0) ids.splice(at, 1)
        else if (ids.length < kMaxMulti) ids.push(id)
        else { notice = qsTr("Como máximo %1 fuentes a la vez.").arg(kMaxMulti); return }
        multiIds = ids
        if (at < 0) sources.requestImage(id)
    }
    onOpened: { resetView(); showingB = false; notice = ""; multi = false; multiIds = []; body.forceActiveFocus(); sources.compareOpen = true }
    onClosed: sources.compareOpen = false

    contentItem: ColumnLayout {
        id: body
        spacing: 0
        focus: true
        Accessible.role: Accessible.Pane
        Accessible.name: qsTr("Comparar fuentes de apariencia")
        Keys.onPressed: event => {
            if (dialog.multi && event.key !== Qt.Key_0) return  // A/B keys do not apply to the grid
            if (event.key === Qt.Key_Space) { dialog.blink = true; dialog.showingB = !dialog.showingB; event.accepted = true }
            else if (event.key === Qt.Key_Right) { dialog.cycleB(1); event.accepted = true }
            else if (event.key === Qt.Key_Left) { dialog.cycleB(-1); event.accepted = true }
            else if (event.key === Qt.Key_0) { dialog.resetView(); event.accepted = true }
        }

        // ---- title
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: titleRow.implicitHeight + 2 * Theme.spacingMd
            color: Theme.header
            radius: Theme.radiusLarge
            RowLayout {
                id: titleRow
                anchors.fill: parent
                anchors.margins: Theme.spacingMd
                Text {
                    Layout.fillWidth: true
                    objectName: "sourceCompareTitle"
                    text: qsTr("Comparar fuentes de apariencia · %1  [%2]").arg(dialog.sources.matrix.name || "?").arg(dialog.sources.matrix.formKey || "?")
                    color: Theme.text
                    font.bold: true
                    font.pixelSize: Theme.large
                    elide: Text.ElideRight
                }
                Button {
                    objectName: "sourceCompareClose"
                    text: qsTr("Cerrar")
                    onClicked: dialog.close()
                }
            }
        }

        // ---- sources of the matrix: pick the A and B side of each
        ListView {
            id: strip
            objectName: "sourceStrip"
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.px(118)
            Layout.margins: Theme.spacingSm
            orientation: ListView.Horizontal
            spacing: Theme.spacingSm
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            // With more sources than fit (an NPC can have five or more) the rest must be reachable: a bar, and the wheel moves it sideways.
            ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
            WheelHandler {
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                onWheel: event => {
                    const delta = event.angleDelta.x !== 0 ? event.angleDelta.x : event.angleDelta.y
                    strip.contentX = Math.max(0, Math.min(Math.max(0, strip.contentWidth - strip.width), strip.contentX - delta))
                }
            }
            model: dialog.candidates
            delegate: Rectangle {
                id: card
                required property var modelData
                required property int index
                readonly property bool isA: dialog.sources.candidateA === modelData.id
                readonly property bool isB: dialog.sources.candidateB === modelData.id
                width: Theme.px(250)
                height: strip.height
                radius: Theme.radius
                color: isA || isB ? Theme.panelAlt : Theme.panel
                border.color: isA || isB ? Theme.accent : Theme.border
                border.width: isA || isB ? 2 : 1
                Accessible.role: Accessible.ListItem
                Accessible.name: qsTr("Fuente %1, estado %2").arg(modelData.label).arg(modelData.status)
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingSm
                    spacing: 2
                    Text {
                        Layout.fillWidth: true
                        text: card.modelData.label
                        color: Theme.text
                        font.bold: true
                        font.pixelSize: Theme.small
                        elide: Text.ElideRight
                    }
                    SourceStatusBadge { status: card.modelData.status }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Registro: %1").arg(card.modelData.recordPlugin || "?")
                        color: Theme.textDim
                        font.pixelSize: Theme.micro
                        elide: Text.ElideMiddle
                    }
                    Text {
                        Layout.fillWidth: true
                        text: (card.modelData.renderable ? qsTr("Se puede dibujar") : qsTr("No se dibuja"))
                              + ((card.modelData.codes || []).length > 0 ? " · " + card.modelData.codes.slice(0, 2).join(", ") : "")
                        color: card.modelData.renderable ? Theme.textDim : Theme.warn
                        font.pixelSize: Theme.micro
                        elide: Text.ElideRight
                    }
                    Item { Layout.fillHeight: true }
                    Button {
                        objectName: "sourceMulti_" + card.index
                        Layout.fillWidth: true
                        visible: dialog.multi
                        readonly property bool inGrid: dialog.multiIds.indexOf(card.modelData.id) >= 0
                        text: inGrid ? qsTr("En la cuadrícula ✓") : qsTr("Añadir a la cuadrícula")
                        checkable: true
                        checked: inGrid
                        enabled: card.modelData.renderable
                        Accessible.name: qsTr("Mostrar %1 en la cuadrícula").arg(card.modelData.label)
                        onClicked: dialog.toggleMulti(card.modelData.id)
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        visible: !dialog.multi
                        spacing: Theme.spacingXs
                        Button {
                            objectName: "sourceSetA_" + card.index
                            Layout.fillWidth: true
                            text: "A"
                            checkable: true
                            checked: card.isA
                            enabled: card.modelData.renderable
                            Accessible.name: qsTr("Mostrar %1 en el lado A").arg(card.modelData.label)
                            onClicked: dialog.sources.setCompare(card.modelData.id, dialog.sources.candidateB)
                        }
                        Button {
                            objectName: "sourceSetB_" + card.index
                            Layout.fillWidth: true
                            text: "B"
                            checkable: true
                            checked: card.isB
                            enabled: card.modelData.renderable
                            Accessible.name: qsTr("Mostrar %1 en el lado B").arg(card.modelData.label)
                            onClicked: dialog.sources.setCompare(dialog.sources.candidateA, card.modelData.id)
                        }
                    }
                }
            }
        }

        // ---- view controls
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingSm
            Layout.rightMargin: Theme.spacingSm
            spacing: Theme.spacingSm
            Switch {
                objectName: "sourceMultiSwitch"
                text: qsTr("Varias a la vez (3–4)")
                checked: dialog.multi
                enabled: dialog.renderableIds().length >= 3
                onToggled: dialog.setMulti(checked)
                Accessible.name: qsTr("Comparar tres o cuatro fuentes a la vez en una cuadrícula")
                ToolTip.visible: hovered && !enabled
                ToolTip.text: qsTr("Este NPC tiene menos de tres fuentes que se puedan dibujar: usa la vista A/B.")
            }
            Switch {
                objectName: "sourceBlinkSwitch"
                visible: !dialog.multi
                text: qsTr("Alternar A/B")
                checked: dialog.blink
                onToggled: dialog.blink = checked
                Accessible.name: qsTr("Alternar entre A y B en el mismo lugar")
            }
            Button {
                objectName: "sourceBlinkNow"
                visible: dialog.blink && !dialog.multi
                text: dialog.showingB ? qsTr("Viendo B (Espacio)") : qsTr("Viendo A (Espacio)")
                onClicked: dialog.showingB = !dialog.showingB
            }
            Button {
                objectName: "sourceZoomReset"
                text: qsTr("Zoom %1× · restablecer").arg(dialog.zoom.toFixed(1))
                enabled: dialog.zoom > 1.0
                onClicked: dialog.resetView()
            }
            Item { Layout.fillWidth: true }
            Text {
                text: dialog.multi ? qsTr("Rueda: zoom · arrastrar: mover (en todas a la vez) · 0: restablecer")
                                   : qsTr("Rueda: zoom · arrastrar: mover · ←/→: cambiar B · Espacio: alternar")
                color: Theme.textMuted
                font.pixelSize: Theme.micro
            }
        }

        // ---- the two pictures: same camera, lights and size
        // ---- the grid of 3-4 sources (same camera, lights and size; zoom and pan move all of them together)
        GridLayout {
            id: grid
            objectName: "sourceGrid"
            visible: dialog.multi
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: Theme.spacingSm
            columns: dialog.multiIds.length > 2 ? 2 : 1
            rowSpacing: Theme.spacingSm
            columnSpacing: Theme.spacingSm
            Repeater {
                model: dialog.multi ? dialog.multiIds : []
                delegate: SourceImagePane {
                    required property int index
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: 1
                    sources: dialog.sources
                    portraits: dialog.studio.portraits
                    tag: String(index + 1)
                    candidateId: modelData
                    compact: true
                    zoom: dialog.zoom
                    panX: dialog.panX
                    panY: dialog.panY
                    onZoomRequested: factor => dialog.applyZoom(factor)
                    onPanRequested: (dx, dy) => dialog.applyPan(dx, dy)
                }
            }
        }

        Text {
            objectName: "sourceGridNote"
            visible: dialog.multi
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingSm
            Layout.rightMargin: Theme.spacingSm
            Layout.bottomMargin: Theme.spacingSm
            text: qsTr("Las imágenes son una aproximación del editor, no una captura de Skyrim. Esta vista no cambia nada ni habilita ninguna exportación; "
                     + "para aplicar un rostro usa la «Resolución asistida». Pendiente de verificación dentro de Skyrim.")
            color: Theme.textMuted
            font.pixelSize: Theme.micro
            wrapMode: Text.WordWrap
        }

        RowLayout {
            id: panes
            visible: !dialog.multi
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: Theme.spacingSm
            spacing: Theme.spacingSm

            SourceImagePane {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: !dialog.blink || !dialog.showingB
                sources: dialog.sources
                portraits: dialog.studio.portraits
                tag: "A"
                candidateId: dialog.sources.candidateA
                zoom: dialog.zoom
                panX: dialog.panX
                panY: dialog.panY
                onZoomRequested: factor => dialog.applyZoom(factor)
                onPanRequested: (dx, dy) => dialog.applyPan(dx, dy)
            }
            SourceImagePane {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: !dialog.blink || dialog.showingB
                sources: dialog.sources
                portraits: dialog.studio.portraits
                tag: "B"
                candidateId: dialog.sources.candidateB
                zoom: dialog.zoom
                panX: dialog.panX
                panY: dialog.panY
                onZoomRequested: factor => dialog.applyZoom(factor)
                onPanRequested: (dx, dy) => dialog.applyPan(dx, dy)
            }
        }

        // ---- draft (non-destructive)
        Rectangle {
            visible: !dialog.multi
            Layout.fillWidth: true
            implicitHeight: draftColumn.implicitHeight + 2 * Theme.spacingSm
            color: Theme.panel
            border.color: Theme.border
            ColumnLayout {
                id: draftColumn
                anchors.fill: parent
                anchors.margins: Theme.spacingSm
                spacing: Theme.spacingXs
                Text {
                    objectName: "sourceDraftState"
                    Layout.fillWidth: true
                    readonly property var d: dialog.sources.draft
                    text: d.state === "fresh" ? qsTr("Borrador guardado el %1: A = %2, B = %3, preferida = %4. Vigente para este perfil y estos archivos.")
                              .arg((d.savedAt || "").replace("T", " ").substring(0, 16)).arg(d.a || "—").arg(d.b || "—").arg(d.chosen || qsTr("ninguna"))
                        : d.state === "stale-profile" ? qsTr("Hay un borrador, pero se hizo con otro perfil o orden de carga: caducado, no se aplica.")
                        : d.state === "stale-inputs" ? qsTr("Hay un borrador, pero los plugins o FaceGen de este NPC cambiaron desde que se guardó: caducado, no se aplica.")
                        : d.state === "unknown-candidate" ? qsTr("Hay un borrador, pero una de sus fuentes ya no existe en el perfil: caducado, no se aplica.")
                        : qsTr("Sin borrador para este NPC.")
                    color: d.state === "fresh" ? Theme.ok : d.state === "empty" ? Theme.textDim : Theme.warn
                    font.pixelSize: Theme.small
                    wrapMode: Text.WordWrap
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSm
                    TextField {
                        id: notes
                        objectName: "sourceDraftNotes"
                        Layout.fillWidth: true
                        placeholderText: qsTr("Notas (opcional)")
                        font.pixelSize: Theme.small
                    }
                    Button {
                        objectName: "sourceDraftSave"
                        text: qsTr("Guardar comparación")
                        onClicked: dialog.notice = dialog.sources.saveDraft("", notes.text) || qsTr("Borrador guardado.")
                    }
                    Button {
                        objectName: "sourceDraftPrefer"
                        text: qsTr("Marcar B como preferida")
                        enabled: dialog.sources.candidateB !== ""
                        onClicked: dialog.notice = dialog.sources.saveDraft(dialog.sources.candidateB, notes.text) || qsTr("Borrador guardado.")
                    }
                    Button {
                        objectName: "sourceDraftClear"
                        text: qsTr("Descartar borrador")
                        enabled: dialog.sources.draft.state !== "empty"
                        onClicked: { dialog.sources.clearDraft(); dialog.notice = qsTr("Borrador descartado.") }
                    }
                }
                Text {
                    objectName: "sourceNotice"
                    Layout.fillWidth: true
                    visible: dialog.notice !== ""
                    text: dialog.notice
                    color: Theme.textDim
                    font.pixelSize: Theme.micro
                    wrapMode: Text.WordWrap
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Las imágenes son una aproximación del editor, no una captura de Skyrim. Esta vista no cambia el orden de carga, "
                             + "MO2, plugins, proyectos ni FaceGen y no habilita ninguna exportación: para aplicar un rostro usa la «Resolución "
                             + "asistida» (con su revisión individual cuando haga falta). Pendiente de verificación dentro de Skyrim.")
                    color: Theme.textMuted
                    font.pixelSize: Theme.micro
                    wrapMode: Text.WordWrap
                }
            }
        }
    }
}
