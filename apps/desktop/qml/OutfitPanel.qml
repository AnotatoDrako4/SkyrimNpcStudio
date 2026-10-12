import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import SkyrimNpcStudio

// Outfit editor for the selected NPC: pieces, presets, materials, guardian, undo/redo, validation and export.
ScrollView {
    id: root
    required property StudioController studio
    readonly property OutfitEditor outfit: studio.outfit
    signal exportRequested()
    clip: true
    contentWidth: availableWidth

    ColorDialog {
        id: materialColorDialog
        title: qsTr("Elegir color de tinte para la pieza")
        selectedColor: materialDialog.selectedColor
        onAccepted: materialDialog.selectedColor = selectedColor
    }

    Dialog {
        id: materialDialog
        title: qsTr("Estudio de Material: ") + targetPieceName
        modal: true
        anchors.centerIn: Overlay.overlay
        width: 440
        standardButtons: Dialog.Close

        property string targetPieceFormKey: ""
        property string targetPieceName: ""
        property color selectedColor: "#900C3F"
        property string diffusePathText: ""
        property string normalPathText: ""
        property string materialNameText: ""

        ColumnLayout {
            width: parent.width
            spacing: 8

            Text {
                text: qsTr("Personaliza el material de esta armadura/ropa. Al exportar, se generarán registros reales TXST y ARMA vinculados a un nuevo ARMO en el plugin.")
                color: Theme.textDim
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            Label { text: qsTr("Nombre de la variante:") }
            TextField {
                text: materialDialog.materialNameText
                placeholderText: qsTr("Ej. Acero Carmesí, Ébano Nocturno...")
                Layout.fillWidth: true
                onTextChanged: materialDialog.materialNameText = text
            }

            Label { text: qsTr("Tinte de Color:") }
            RowLayout {
                spacing: 8
                Rectangle {
                    width: 28
                    height: 28
                    radius: 3
                    color: materialDialog.selectedColor
                    border.color: Theme.border
                    border.width: 1
                }
                Text {
                    text: materialDialog.selectedColor.toString().toUpperCase()
                    color: Theme.text
                    font.bold: true
                }
                Item { Layout.fillWidth: true }
                Button {
                    text: qsTr("Selector libre…")
                    onClicked: materialColorDialog.open()
                }
            }

            // Muestrario rápido de tintes
            GridLayout {
                Layout.fillWidth: true
                columns: 5
                rowSpacing: 4
                columnSpacing: 4
                Repeater {
                    model: [
                        { name: "Carmesí", color: "#900C3F" },
                        { name: "Oro", color: "#D4AC0D" },
                        { name: "Noche", color: "#1B4F72" },
                        { name: "Bosque", color: "#1E8449" },
                        { name: "Ébano", color: "#212121" },
                        { name: "Plata", color: "#BDC3C7" },
                        { name: "Cobre", color: "#D35400" },
                        { name: "Púrpura", color: "#6C3483" },
                        { name: "Hueso", color: "#F5EEF8" },
                        { name: "Sombra", color: "#37474F" }
                    ]
                    delegate: Button {
                        Layout.fillWidth: true
                        implicitHeight: 22
                        ToolTip.text: modelData.name
                        ToolTip.visible: hovered
                        contentItem: Rectangle {
                            color: modelData.color
                            radius: 2
                            border.color: Theme.border
                        }
                        onClicked: materialDialog.selectedColor = Qt.color(modelData.color)
                    }
                }
            }

            Label { text: qsTr("Textura difusa override (opcional DDS):") }
            TextField {
                text: materialDialog.diffusePathText
                placeholderText: "textures/armor/..._d.dds"
                Layout.fillWidth: true
                onTextChanged: materialDialog.diffusePathText = text
            }

            Label { text: qsTr("Normal map override (opcional DDS):") }
            TextField {
                text: materialDialog.normalPathText
                placeholderText: "textures/armor/..._n.dds"
                Layout.fillWidth: true
                onTextChanged: materialDialog.normalPathText = text
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Layout.topMargin: 4

                Button {
                    text: qsTr("Restablecer Original")
                    onClicked: {
                        root.outfit.clearPieceMaterial(materialDialog.targetPieceFormKey)
                        materialDialog.close()
                    }
                }
                Item { Layout.fillWidth: true }
                Button {
                    text: qsTr("Aplicar Material")
                    highlighted: true
                    onClicked: {
                        root.outfit.setPieceMaterial(
                            materialDialog.targetPieceFormKey,
                            materialDialog.selectedColor,
                            materialDialog.diffusePathText.trim(),
                            materialDialog.normalPathText.trim(),
                            materialDialog.materialNameText.trim().length > 0 ? materialDialog.materialNameText.trim() : "CustomVariant"
                        )
                        materialDialog.close()
                    }
                }
            }
        }
    }

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

            // Presets de Vestuario Canónicos
            SectionHeader {
                title: qsTr("Presets de Vestuario")
                detail: root.outfit.outfitPresets.length + qsTr(" arquetipos")
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                ComboBox {
                    id: presetCombo
                    Layout.fillWidth: true
                    model: root.outfit.outfitPresets
                    textRole: "name"
                }
                Button {
                    text: qsTr("Aplicar Preset")
                    onClicked: {
                        if (presetCombo.currentIndex >= 0) {
                            root.outfit.applyPreset(root.outfit.outfitPresets[presetCombo.currentIndex].id)
                        }
                    }
                }
            }
            Text {
                visible: presetCombo.currentIndex >= 0 && root.outfit.outfitPresets.length > 0
                text: (root.outfit.outfitPresets[presetCombo.currentIndex]?.description ?? "")
                      + " (" + (root.outfit.outfitPresets[presetCombo.currentIndex]?.pieceCount ?? 0) + qsTr(" piezas)")
                color: Theme.textDim
                font.pixelSize: Theme.small
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }

            SectionHeader {
                title: qsTr("Piezas Equipadas")
                detail: root.outfit.pieces.length + qsTr(" piezas")
            }

            Repeater {
                model: root.outfit.pieces
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: pieceCol.implicitHeight + 10
                    radius: 3
                    color: Theme.panelAlt
                    border.color: modelData.hasMaterial ? Theme.accent : Theme.border
                    ColumnLayout {
                        id: pieceCol
                        anchors.fill: parent
                        anchors.margins: 5
                        spacing: 2
                        RowLayout {
                            Layout.fillWidth: true
                            ColumnLayout {
                                spacing: 0
                                Layout.fillWidth: true
                                Text { text: modelData.name; color: Theme.text; font.pixelSize: Theme.normal; elide: Text.ElideRight; Layout.fillWidth: true }
                                Text { text: qsTr("slots ") + modelData.slots + " · " + modelData.plugin; color: Theme.textDim; font.pixelSize: Theme.small; elide: Text.ElideMiddle; Layout.fillWidth: true }
                            }
                            ToolButton {
                                text: "🎨"
                                onClicked: {
                                    materialDialog.targetPieceFormKey = modelData.formKey
                                    materialDialog.targetPieceName = modelData.name
                                    materialDialog.selectedColor = modelData.tintColor !== undefined ? modelData.tintColor : Qt.color("#900C3F")
                                    materialDialog.diffusePathText = modelData.diffuseOverride ?? ""
                                    materialDialog.normalPathText = modelData.normalOverride ?? ""
                                    materialDialog.materialNameText = modelData.materialName ?? ""
                                    materialDialog.open()
                                }
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("Personalizar textura / tinte de material (TXST/ARMA)")
                            }
                            ToolButton { text: "✕"; onClicked: root.outfit.removePiece(modelData.formKey); ToolTip.visible: hovered; ToolTip.text: qsTr("Quitar pieza") }
                        }
                        RowLayout {
                            visible: modelData.hasMaterial
                            spacing: 4
                            Rectangle {
                                width: 10; height: 10; radius: 2
                                color: modelData.tintColor !== undefined ? modelData.tintColor : "transparent"
                                border.color: Theme.border
                                border.width: 1
                            }
                            Text {
                                text: qsTr("Material TXST: ") + (modelData.materialName && modelData.materialName.length > 0 ? modelData.materialName : qsTr("Personalizado"))
                                color: Theme.accent
                                font.pixelSize: Theme.small - 1
                                font.bold: true
                            }
                        }
                    }
                }
            }
            Text {
                visible: root.outfit.pieces.length === 0
                text: qsTr("Sin piezas. Añade prendas desde el catálogo «Prendas» o aplica un preset.")
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

            // Compatibility Guardian
            SectionHeader {
                title: qsTr("Guardian de Compatibilidad")
                detail: root.outfit.compatibilityReport.badgeText ?? ""
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: guardianCol.implicitHeight + 12
                color: Theme.panelAlt
                radius: 4
                border.color: root.outfit.compatibilityReport.badge === "verified" ? Theme.ok
                              : root.outfit.compatibilityReport.badge === "warning" ? Theme.warn : Theme.error
                border.width: 1

                ColumnLayout {
                    id: guardianCol
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true
                        StatusBadge {
                            text: root.outfit.compatibilityReport.badgeText ?? qsTr("ANALIZANDO")
                            tone: root.outfit.compatibilityReport.badge === "verified" ? Theme.ok
                                  : root.outfit.compatibilityReport.badge === "warning" ? Theme.warn : Theme.error
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: (root.outfit.compatibilityReport.body?.name ?? "Vanilla")
                                  + (root.outfit.compatibilityReport.body?.physicsEnabled ? qsTr(" · Físicas FSMP") : "")
                            color: Theme.accent
                            font.pixelSize: Theme.small
                            font.bold: true
                        }
                    }

                    Text {
                        text: root.outfit.compatibilityReport.summary ?? ""
                        color: Theme.text
                        font.pixelSize: Theme.small
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }

                    Repeater {
                        model: root.outfit.compatibilityReport.issues ?? []
                        delegate: Rectangle {
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: issueRow.implicitHeight + 8
                            radius: 3
                            color: Theme.panel
                            border.color: Theme.severityColor(modelData.severity)

                            RowLayout {
                                id: issueRow
                                anchors.fill: parent
                                anchors.margins: 4
                                spacing: 6
                                Text {
                                    text: modelData.severity === "error" ? "✖" : modelData.severity === "warning" ? "⚠" : "ℹ"
                                    color: Theme.severityColor(modelData.severity)
                                    font.pixelSize: Theme.normal
                                    Layout.alignment: Qt.AlignTop
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Text {
                                        text: "<b>" + modelData.code + "</b> (" + modelData.component + "): " + modelData.message
                                        color: Theme.text
                                        font.pixelSize: Theme.small
                                        wrapMode: Text.Wrap
                                        Layout.fillWidth: true
                                    }
                                    Text {
                                        visible: modelData.remediation && modelData.remediation.length > 0
                                        text: "→ " + modelData.remediation
                                        color: Theme.textDim
                                        font.pixelSize: Theme.small - 1
                                        font.italic: true
                                        wrapMode: Text.Wrap
                                        Layout.fillWidth: true
                                    }
                                }
                            }
                        }
                    }
                }
            }

            SectionHeader {
                title: qsTr("Validación de Plugins")
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
