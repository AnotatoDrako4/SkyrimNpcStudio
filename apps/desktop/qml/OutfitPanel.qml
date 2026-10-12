import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

// Outfit editor for the selected NPC: pieces, undo/redo, live validation and export entry point.
ScrollView {
    id: root
    required property StudioController studio
    readonly property OutfitEditor outfit: studio.outfit
    signal exportRequested()
    clip: true
    contentWidth: availableWidth

    ColumnLayout {
        width: root.availableWidth
        spacing: 6

        SectionHeader { title: qsTr("Atuendo"); detail: root.outfit.npcLabel }

        Text {
            visible: root.outfit.npcFormKey.length === 0
            text: qsTr("Selecciona un NPC para editar su atuendo predeterminado.")
            color: Theme.textDim
            font.pixelSize: Theme.small
            Layout.margins: 8
        }

        ColumnLayout {
            visible: root.outfit.npcFormKey.length > 0
            Layout.fillWidth: true
            Layout.margins: 8
            spacing: 6

            Text {
                text: qsTr("Actual en el perfil: ") + (root.studio.npc.defaultOutfit?.outfit?.editorId ?? qsTr("ninguno"))
                      + " (" + (root.studio.npc.defaultOutfit?.armors?.length ?? 0) + qsTr(" piezas)")
                color: Theme.textDim
                font.pixelSize: Theme.small
            }
            Text {
                visible: root.studio.npc.sleepingOutfit?.outfit?.editorId !== undefined
                text: qsTr("Dormir (SOFT): ") + (root.studio.npc.sleepingOutfit?.outfit?.editorId ?? qsTr("ninguno"))
                      + " (" + (root.studio.npc.sleepingOutfit?.armors?.length ?? 0) + qsTr(" piezas)")
                color: Theme.textDim
                font.pixelSize: Theme.small
            }

            RowLayout {
                Layout.fillWidth: true
                Button { text: qsTr("Deshacer"); enabled: root.outfit.canUndo; onClicked: root.outfit.undo(); ToolTip.visible: hovered && root.outfit.undoText; ToolTip.text: root.outfit.undoText }
                Button { text: qsTr("Rehacer"); enabled: root.outfit.canRedo; onClicked: root.outfit.redo(); ToolTip.visible: hovered && root.outfit.redoText; ToolTip.text: root.outfit.redoText }
                Button { text: qsTr("Restaurar actual"); onClicked: root.outfit.resetToCurrent() }
                Button { text: qsTr("Vaciar"); onClicked: root.outfit.clearPieces() }
            }

            Button {
                text: qsTr("Guardar NPC en proyecto actual")
                Layout.fillWidth: true
                onClicked: root.outfit.addCurrentToProject()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Añade o actualiza este NPC en la lista del proyecto para exportar en lote")
            }

            Repeater {
                model: root.outfit.pieces
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: pieceRow.implicitHeight + 10
                    radius: 3
                    color: Theme.panelAlt
                    border.color: Theme.border
                    RowLayout {
                        id: pieceRow
                        anchors.fill: parent
                        anchors.margins: 5
                        ColumnLayout {
                            spacing: 0
                            Layout.fillWidth: true
                            Text { text: modelData.name; color: Theme.text; font.pixelSize: Theme.normal; elide: Text.ElideRight; Layout.fillWidth: true }
                            Text { text: qsTr("slots ") + modelData.slots + " · " + modelData.plugin; color: Theme.textDim; font.pixelSize: Theme.small; elide: Text.ElideMiddle; Layout.fillWidth: true }
                        }
                        ToolButton { text: "✕"; onClicked: root.outfit.removePiece(modelData.formKey); ToolTip.visible: hovered; ToolTip.text: qsTr("Quitar pieza") }
                    }
                }
            }
            Text {
                visible: root.outfit.pieces.length === 0
                text: qsTr("Sin piezas. Añade prendas desde el catálogo «Ropa y armaduras» (doble clic).")
                color: Theme.textDim
                font.pixelSize: Theme.small
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }

            SectionHeader {
                visible: root.outfit.projectNpcCount > 0
                title: qsTr("NPCs en el proyecto")
                detail: root.outfit.projectNpcCount + qsTr(" NPCs")
            }
            Repeater {
                visible: root.outfit.projectNpcCount > 0
                model: root.outfit.projectNpcs
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: projectRow.implicitHeight + 8
                    radius: 3
                    color: modelData.isCurrent ? Theme.panel : Theme.panelAlt
                    border.color: modelData.isCurrent ? Theme.accent : Theme.border
                    RowLayout {
                        id: projectRow
                        anchors.fill: parent
                        anchors.margins: 4
                        ColumnLayout {
                            spacing: 0
                            Layout.fillWidth: true
                            Text {
                                text: modelData.name + (modelData.isCurrent ? qsTr(" (actual)") : "")
                                color: modelData.isCurrent ? Theme.accent : Theme.text
                                font.bold: modelData.isCurrent
                                font.pixelSize: Theme.normal
                                elide: Text.ElideRight
                            }
                            Text {
                                text: modelData.editorId + " · " + modelData.piecesCount + qsTr(" piezas")
                                color: Theme.textDim
                                font.pixelSize: Theme.small
                                elide: Text.ElideRight
                            }
                        }
                        ToolButton {
                            visible: !modelData.isCurrent
                            text: "↗"
                            onClicked: root.outfit.loadNpcFromProject(modelData.formKey)
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Cargar este NPC en el editor")
                        }
                        ToolButton {
                            text: "✕"
                            onClicked: root.outfit.removeNpcFromProject(modelData.formKey)
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Quitar del proyecto")
                        }
                    }
                }
            }

            SectionHeader {
                title: qsTr("Validación")
                detail: root.outfit.validating ? qsTr("comprobando…") : root.outfit.exportable ? qsTr("exportable") : qsTr("no exportable")
            }
            StatusBadge {
                text: root.outfit.validating ? qsTr("VALIDANDO") : root.outfit.exportable ? qsTr("LISTO PARA EXPORTAR") : qsTr("BLOQUEADO")
                tone: root.outfit.validating ? Theme.textDim : root.outfit.exportable ? Theme.ok : Theme.error
            }
            IssueList {
                issues: root.outfit.validation.issues ?? []
                emptyText: root.outfit.validating ? "" : qsTr("Sin incidencias")
            }

            Button {
                text: root.outfit.exporting ? qsTr("Exportando…") : qsTr("GUARDAR MOD (parche de atuendo)…")
                highlighted: true
                enabled: root.outfit.exportable && !root.outfit.exporting
                Layout.fillWidth: true
                onClicked: root.exportRequested()
            }

            SectionHeader { title: qsTr("Historial de cambios"); detail: root.outfit.dirty ? qsTr("sin guardar") : qsTr("guardado") }
            Repeater {
                model: root.outfit.history
                delegate: Text { required property string modelData; text: "• " + modelData; color: Theme.textDim; font.pixelSize: Theme.small; wrapMode: Text.Wrap; Layout.fillWidth: true }
            }
        }
    }
}
