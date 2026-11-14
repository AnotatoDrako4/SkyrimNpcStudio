import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

// NPC fidelity report of the 3D view (SceneViewModel.fidelity, sns/rendering/fidelity.hpp): for every piece, what the game would
// draw against what this view draws, and every finding with its category (data, compatibility, renderer), how sure it is, the
// measured evidence and what to do. «Aislar» shows only the parts a finding is about; «Ver todo» restores the default visibility.
// Read-only: nothing here changes the NPC or writes files.
ScrollView {
    id: root
    objectName: "fidelityPanel"
    required property StudioController studio
    readonly property var scene: studio.scene
    readonly property var report: scene ? scene.fidelity : ({})
    readonly property bool ready: !!report && report.npcFormKey !== undefined
    property string filter: "all"  // "all" | "data" | "compatibility" | "renderer"
    contentWidth: availableWidth
    clip: true

    function categoryText(c) {
        return c === "data" ? qsTr("Datos") : c === "compatibility" ? qsTr("Compatibilidad") : qsTr("Visor")
    }
    function confidenceText(c) {
        return c === "confirmed" ? qsTr("confirmado") : c === "probable" ? qsTr("probable") : qsTr("incierto")
    }
    function severityColor(s) {
        return s === "error" ? Theme.error : s === "warning" ? Theme.warn : Theme.border
    }
    function gameText(state) {
        return state === "drawn" ? qsTr("se ve") : state === "partial" ? qsTr("se ve en parte")
             : state === "not-drawn" ? qsTr("no se ve") : qsTr("desconocido")
    }
    function viewerText(state) {
        return state === "drawn" ? qsTr("se ve") : state === "partial" ? qsTr("en parte")
             : state === "hidden" ? qsTr("oculta") : qsTr("nada")
    }
    // Shows only `ids` (scene part ids); the rest of the character is hidden until «Ver todo».
    function isolate(ids) {
        if (!root.scene || !ids || ids.length === 0) return
        for (const part of root.scene.parts) root.scene.setVisible(part.id, ids.indexOf(part.id) >= 0)
        root.scene.select(ids[0])
        root.scene.frameParts(ids)
    }
    function findingsShown() {
        const all = root.report.findings || []
        return root.filter === "all" ? all : all.filter(f => f.category === root.filter)
    }

    ColumnLayout {
        width: root.availableWidth
        spacing: Theme.spacingMd

        SectionHeader {
            title: qsTr("Fidelidad: visor frente al juego")
            detail: root.ready ? qsTr("%1 ms").arg(Math.round(root.report.analysisMs || 0)) : ""
        }

        EmptyState {
            Layout.fillWidth: true
            visible: !root.ready
            glyph: "diagnostic"
            title: root.scene && root.scene.loading ? qsTr("Construyendo la escena…") : qsTr("Sin informe todavía")
            message: qsTr("El informe se calcula tras dibujar al NPC completo, sin bloquear el visor.")
        }

        // -- summary ------------------------------------------------------------------------------------------------------
        ColumnLayout {
            visible: root.ready
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingMd
            Layout.rightMargin: Theme.spacingMd
            spacing: Theme.spacingXs
            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.text
                font.pixelSize: Theme.small
                text: {
                    const b = root.report.body || {}
                    if (!b.partId) return qsTr("No se dibuja ningún cuerpo: no hay con qué medir el ajuste de las prendas.")
                    const source = b.source === "outfit" ? qsTr("dentro de una prenda") : b.source === "skin" ? qsTr("piel del NPC")
                                 : qsTr("piel oculta por el atuendo")
                    return qsTr("Cuerpo medido: %1 · %2 · %3").arg(source).arg(b.mesh || "").arg(b.provider || "")
                }
            }
            Flow {
                Layout.fillWidth: true
                spacing: Theme.spacingXs
                StatusBadge { text: qsTr("Datos %1").arg((root.report.counts || {}).data || 0); tone: Theme.warn }
                StatusBadge { text: qsTr("Compat. %1").arg((root.report.counts || {}).compatibility || 0); tone: Theme.accent }
                StatusBadge { text: qsTr("Visor %1").arg((root.report.counts || {}).renderer || 0); tone: Theme.textDim }
                StatusBadge {
                    readonly property int blockers: (root.report.exportBlockers || []).length
                    text: blockers > 0 ? qsTr("Bloquea exportar: %1").arg(blockers) : qsTr("Sin bloqueos")
                    tone: blockers > 0 ? Theme.error : Theme.ok
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Button {
                    objectName: "fidelityRestore"
                    text: qsTr("Ver todo")
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Vuelve a la visibilidad del juego (deshace «Aislar»)")
                    onClicked: {
                        root.scene.restoreVisibility()
                        root.scene.frameParts([])
                    }
                }
                ComboBox {
                    objectName: "fidelityFilter"
                    Layout.fillWidth: true
                    model: [qsTr("Todos"), qsTr("Datos"), qsTr("Compatibilidad"), qsTr("Visor")]
                    onActivated: (i) => root.filter = ["all", "data", "compatibility", "renderer"][i]
                }
            }
        }

        // -- pieces -------------------------------------------------------------------------------------------------------
        SectionHeader {
            visible: root.ready
            title: qsTr("Piezas")
            detail: {
                const pieces = root.report.pieces || []
                return qsTr("%1 de %2 como en el juego").arg(pieces.filter(p => p.viewerMatchesGame).length).arg(pieces.length)
            }
        }
        Repeater {
            model: root.ready ? (root.report.pieces || []) : []
            delegate: Rectangle {
                id: pieceRow
                required property var modelData
                objectName: "fidelityPiece"
                Layout.fillWidth: true
                Layout.leftMargin: Theme.spacingMd
                Layout.rightMargin: Theme.spacingMd
                implicitHeight: pieceColumn.implicitHeight + Theme.spacingMd
                color: Theme.panelAlt
                border.color: pieceRow.modelData.viewerMatchesGame ? Theme.border : Theme.warn
                radius: 4
                ColumnLayout {
                    id: pieceColumn
                    anchors.fill: parent
                    anchors.margins: Theme.spacingXs
                    spacing: 2
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            color: Theme.text
                            font.pixelSize: Theme.small
                            font.bold: true
                            text: pieceRow.modelData.name
                        }
                        StatusBadge {
                            text: pieceRow.modelData.viewerMatchesGame ? qsTr("igual") : qsTr("distinto")
                            tone: pieceRow.modelData.viewerMatchesGame ? Theme.ok : Theme.warn
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: Theme.textDim
                        font.pixelSize: Theme.small
                        text: qsTr("Juego: %1 · Visor: %2 · %3 formas dibujadas, %4 ocultas%5")
                              .arg(root.gameText(pieceRow.modelData.game)).arg(root.viewerText(pieceRow.modelData.viewer))
                              .arg(pieceRow.modelData.shapesDrawn).arg(pieceRow.modelData.shapesHidden)
                              .arg((pieceRow.modelData.findings || []).length ? " · " + pieceRow.modelData.findings.join(", ") : "")
                    }
                    Button {
                        objectName: "fidelityIsolatePiece"
                        visible: (pieceRow.modelData.partIds || []).length > 0
                        text: qsTr("Aislar pieza")
                        onClicked: root.isolate(pieceRow.modelData.partIds)
                    }
                }
            }
        }

        // -- findings -----------------------------------------------------------------------------------------------------
        SectionHeader {
            visible: root.ready
            title: qsTr("Hallazgos")
            detail: root.ready ? String(root.findingsShown().length) : ""
        }
        Repeater {
            model: root.ready ? root.findingsShown() : []
            delegate: Rectangle {
                id: findingRow
                required property var modelData
                objectName: "fidelityFinding"
                Layout.fillWidth: true
                Layout.leftMargin: Theme.spacingMd
                Layout.rightMargin: Theme.spacingMd
                implicitHeight: findingColumn.implicitHeight + Theme.spacingMd
                color: Theme.panel
                border.color: root.severityColor(findingRow.modelData.severity)
                radius: 4
                ColumnLayout {
                    id: findingColumn
                    anchors.fill: parent
                    anchors.margins: Theme.spacingXs
                    spacing: 2
                    Flow {
                        Layout.fillWidth: true
                        spacing: Theme.spacingXs
                        StatusBadge {
                            text: root.categoryText(findingRow.modelData.category)
                            tone: findingRow.modelData.category === "data" ? Theme.warn
                                : findingRow.modelData.category === "compatibility" ? Theme.accent : Theme.textDim
                        }
                        StatusBadge {
                            text: findingRow.modelData.severity === "error" ? qsTr("error") : findingRow.modelData.severity === "warning" ? qsTr("aviso") : qsTr("nota")
                            tone: findingRow.modelData.severity === "error" ? Theme.error : findingRow.modelData.severity === "warning" ? Theme.warn : Theme.textDim
                        }
                        StatusBadge { text: root.confidenceText(findingRow.modelData.confidence); tone: Theme.textDim }
                        Text {
                            color: Theme.textDim
                            font.pixelSize: Theme.micro
                            text: findingRow.modelData.code
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: findingRow.modelData.piece.length > 0
                        elide: Text.ElideRight
                        color: Theme.text
                        font.pixelSize: Theme.small
                        font.bold: true
                        text: findingRow.modelData.piece
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: Theme.text
                        font.pixelSize: Theme.small
                        text: findingRow.modelData.message
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: findingRow.modelData.evidence.length > 0
                        wrapMode: Text.Wrap
                        color: Theme.textDim
                        font.pixelSize: Theme.small
                        text: qsTr("Evidencia: %1").arg(findingRow.modelData.evidence)
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: findingRow.modelData.action.length > 0
                        wrapMode: Text.Wrap
                        color: Theme.textDim
                        font.pixelSize: Theme.small
                        text: qsTr("Qué hacer: %1").arg(findingRow.modelData.action)
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: findingRow.modelData.subject.length > 0
                        elide: Text.ElideMiddle
                        color: Theme.textMuted
                        font.pixelSize: Theme.micro
                        text: findingRow.modelData.subject
                    }
                    Button {
                        objectName: "fidelityIsolateFinding"
                        visible: (findingRow.modelData.partIds || []).length > 0
                        text: qsTr("Aislar")
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Muestra solo las formas de este hallazgo (también las que el juego no dibuja)")
                        onClicked: root.isolate(findingRow.modelData.partIds)
                    }
                }
            }
        }

        // -- where every resource came from ----------------------------------------------------------------------------------
        SectionHeader {
            visible: root.ready
            title: qsTr("Procedencia")
            detail: qsTr("%1 recursos").arg(root.ready ? (root.report.resources || []).length : 0)
        }
        Repeater {
            model: root.ready ? (root.report.resources || []).filter(r => r.kind !== "texture" || r.provider === "missing") : []
            delegate: Text {
                required property var modelData
                Layout.fillWidth: true
                Layout.leftMargin: Theme.spacingMd
                Layout.rightMargin: Theme.spacingMd
                wrapMode: Text.Wrap
                color: modelData.provider === "missing" ? Theme.error : Theme.textDim
                font.pixelSize: Theme.small
                text: qsTr("%1 · %2 ← %3%4").arg(modelData.kind).arg(modelData.key).arg(modelData.provider || qsTr("registro"))
                      .arg(modelData.piece ? " (" + modelData.piece + ")" : "")
            }
        }
        Item { Layout.preferredHeight: Theme.spacingLg }
    }
}
