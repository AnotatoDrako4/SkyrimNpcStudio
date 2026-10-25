import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import SkyrimNpcStudio

// Editor de atuendos para el NPC seleccionado (Creative Workspace).
ScrollView {
    id: root
    required property StudioController studio
    readonly property OutfitEditor outfit: studio.outfit
    signal exportRequested()
    clip: true
    // Qt creates the scroll view's bars unnamed; screen readers announce them by these names.
    Component.onCompleted: {
        root.ScrollBar.vertical.Accessible.name = qsTr("Desplazar el panel de atuendo")
        root.ScrollBar.horizontal.Accessible.name = qsTr("Desplazar el panel de atuendo en horizontal")
    }
    contentWidth: availableWidth

    function getSlotIcon(slots) {
        if (!slots) return "📦"
        const s = String(slots)
        if (s.indexOf("30") !== -1 || s.indexOf("42") !== -1) return "🪖"
        if (s.indexOf("31") !== -1) return "💇"
        if (s.indexOf("32") !== -1) return "🥋"
        if (s.indexOf("33") !== -1 || s.indexOf("34") !== -1) return "🧤"
        if (s.indexOf("37") !== -1 || s.indexOf("38") !== -1) return "👢"
        if (s.indexOf("39") !== -1) return "🛡️"
        if (s.indexOf("35") !== -1) return "📿"
        if (s.indexOf("36") !== -1) return "💍"
        if (s.indexOf("46") !== -1) return "🧣"
        return "👕"
    }

    // ---------------------------------------------------------------- DIÁLOGOS
    Dialog {
        id: presetNameDialog
        modal: true
        anchors.centerIn: Overlay.overlay
        title: action === "save" ? qsTr("Guardar outfit como preset") : action === "duplicate" ? qsTr("Duplicar preset") : qsTr("Renombrar preset")
        standardButtons: Dialog.Save | Dialog.Cancel
        property string action: "save"
        property string presetId: ""
        onAccepted: {
            const name = presetNameField.text.trim()
            if (action === "save") root.outfit.saveCurrentPreset(name)
            else if (action === "duplicate") root.outfit.duplicatePreset(presetId, name)
            else root.outfit.renamePreset(presetId, name)
        }
        TextField { id: presetNameField; width: Theme.px(320); placeholderText: qsTr("Nombre del conjunto") }
    }

    Dialog {
        id: deletePresetDialog
        modal: true
        anchors.centerIn: Overlay.overlay
        title: qsTr("Eliminar preset propio")
        standardButtons: Dialog.Yes | Dialog.Cancel
        // Fixed width and a layout around the text: a bare Label made the closed dialog's implicit size loop on every
        // font scale change.
        width: Math.min(Theme.px(420), Overlay.overlay ? Overlay.overlay.width - 40 : Theme.px(420))
        property string presetId: ""
        onAccepted: root.outfit.deletePreset(presetId)
        ColumnLayout {
            anchors.fill: parent
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("Se eliminará este preset de tu biblioteca personal.")
            }
        }
    }

    ColorDialog {
        id: materialColorDialog
        title: qsTr("Elegir color de tinte para la pieza")
        selectedColor: materialDialog.selectedColor
        onAccepted: materialDialog.selectedColor = selectedColor
    }

    Dialog {
        id: materialDialog
        title: qsTr("Configuración de material pendiente: ") + targetPieceName
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Theme.px(440)
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
                text: qsTr("Configuración pendiente: el tinte y estas rutas se guardan en el proyecto, pero aún no cambian la malla del visor ni generan DDS para Skyrim. Aplicarlos bloqueará la exportación hasta que restaures el material original.")
                color: Theme.textDim
                font.pixelSize: Theme.small
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
                    radius: Theme.radiusSmall
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
                            radius: Theme.radiusSmall
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
                    text: qsTr("Guardar solicitud")
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

    // ---------------------------------------------------------------- CONTENIDO PRINCIPAL
    ColumnLayout {
        width: root.availableWidth
        spacing: 6

        // Cabecera del Atuendo
        SectionHeader {
            title: qsTr("Diseñador de Atuendos")
            detail: root.outfit.npcLabel.length > 0 ? root.outfit.npcLabel : qsTr("Outfit independiente")
        }

        // Estado sin NPC seleccionado
        ColumnLayout {
            visible: root.outfit.npcFormKey.length === 0
            Layout.fillWidth: true
            Layout.margins: 8
            spacing: 8

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: noNpcCol.implicitHeight + 16
                radius: Theme.radius
                color: Theme.panelAlt
                border.color: Theme.borderSubtle

                ColumnLayout {
                    id: noNpcCol
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 8

                    Text {
                        text: qsTr("Modo Outfit Autónomo")
                        color: Theme.accent
                        font.bold: true
                        font.pixelSize: Theme.normal
                    }
                    Text {
                        text: qsTr("Diseña un conjunto de ropa/armadura sin necesidad de vincularlo previamente a un NPC específico:")
                        color: Theme.textDim
                        font.pixelSize: Theme.small
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    Button {
                        text: qsTr("✨ Crear Nuevo Outfit Vacío")
                        Accessible.name: qsTr("Crear nuevo outfit vacío")
                        highlighted: true
                        Layout.fillWidth: true
                        onClicked: root.outfit.createNewOutfit()
                    }
                }
            }
        }

        // Estado con NPC o Outfit activo
        ColumnLayout {
            visible: root.outfit.npcFormKey.length > 0 || root.outfit.pieces.length > 0
            Layout.fillWidth: true
            Layout.margins: 8
            spacing: 6

            // Barra de acciones superiores: Deshacer / Rehacer / Vaciar
            RowLayout {
                Layout.fillWidth: true
                spacing: 4

                Button {
                    text: "↶ " + qsTr("Deshacer")
                    Accessible.name: qsTr("Deshacer")
                    enabled: root.outfit.canUndo
                    implicitHeight: 26
                    onClicked: root.outfit.undo()
                    ToolTip.visible: hovered && root.outfit.undoText
                    ToolTip.text: root.outfit.undoText
                }

                Button {
                    text: "↷ " + qsTr("Rehacer")
                    Accessible.name: qsTr("Rehacer")
                    enabled: root.outfit.canRedo
                    implicitHeight: 26
                    onClicked: root.outfit.redo()
                    ToolTip.visible: hovered && root.outfit.redoText
                    ToolTip.text: root.outfit.redoText
                }

                Item { Layout.fillWidth: true }

                Button {
                    text: qsTr("Restablecer")
                    implicitHeight: 26
                    onClicked: root.outfit.resetToCurrent()
                }

                Button {
                    text: qsTr("Vaciar")
                    implicitHeight: 26
                    onClicked: root.outfit.clearPieces()
                }
            }

            // Galería de Presets de Vestuario
            SectionHeader {
                title: qsTr("Presets de Vestuario")
                detail: root.outfit.outfitPresets.length + qsTr(" disponibles")
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: presetCol.implicitHeight + 12
                radius: Theme.radius
                color: Theme.panelAlt
                border.color: Theme.borderSubtle

                ColumnLayout {
                    id: presetCol
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 6

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
                            text: qsTr("Aplicar")
                            highlighted: true
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

                    // Acciones de Preset
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        Button {
                            text: qsTr("Guardar actual")
                            enabled: root.outfit.exportable
                            implicitHeight: 24
                            onClicked: {
                                presetNameDialog.action = "save"
                                presetNameDialog.presetId = ""
                                presetNameField.text = ""
                                presetNameDialog.open()
                            }
                        }
                        Button {
                            text: qsTr("Duplicar")
                            enabled: presetCombo.currentIndex >= 0
                            implicitHeight: 24
                            onClicked: {
                                const preset = root.outfit.outfitPresets[presetCombo.currentIndex]
                                presetNameDialog.action = "duplicate"
                                presetNameDialog.presetId = preset.id
                                presetNameField.text = preset.name + qsTr(" (copia)")
                                presetNameDialog.open()
                            }
                        }
                        Button {
                            text: "★ " + qsTr("Favorito")
                            Accessible.name: qsTr("Favorito")
                            enabled: presetCombo.currentIndex >= 0 && !root.outfit.outfitPresets[presetCombo.currentIndex]?.builtin
                            checkable: true
                            checked: presetCombo.currentIndex >= 0 && !!root.outfit.outfitPresets[presetCombo.currentIndex]?.favorite
                            implicitHeight: 24
                            onClicked: {
                                const preset = root.outfit.outfitPresets[presetCombo.currentIndex]
                                root.outfit.setPresetFavorite(preset.id, !preset.favorite)
                            }
                        }
                        Button {
                            text: "✕"
                            Accessible.name: qsTr("Eliminar preset propio")
                            enabled: presetCombo.currentIndex >= 0 && !root.outfit.outfitPresets[presetCombo.currentIndex]?.builtin
                            implicitHeight: 24
                            onClicked: {
                                deletePresetDialog.presetId = root.outfit.outfitPresets[presetCombo.currentIndex].id
                                deletePresetDialog.open()
                            }
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Eliminar preset")
                        }
                    }
                }
            }

            // Piezas Equipadas
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
                    radius: Theme.radius
                    color: Theme.panelAlt
                    border.color: modelData.hasMaterial ? Theme.accent : Theme.borderSubtle

                    ColumnLayout {
                        id: pieceCol
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 3

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            // Icono slot silhouette
                            Rectangle {
                                implicitWidth: 26
                                implicitHeight: 26
                                radius: Theme.radiusSmall
                                color: Theme.surface
                                border.color: Theme.borderSubtle
                                Text {
                                    anchors.centerIn: parent
                                    text: root.getSlotIcon(modelData.slots)
                                    font.pixelSize: Theme.fs(13)
                                }
                            }

                            ColumnLayout {
                                spacing: 0
                                Layout.fillWidth: true
                                Text {
                                    text: modelData.name
                                    color: Theme.text
                                    font.pixelSize: Theme.normal
                                    font.bold: true
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                                Text {
                                    text: qsTr("Slot ") + modelData.slots + " · " + modelData.plugin
                                    color: Theme.textDim
                                    font.pixelSize: Theme.small
                                    elide: Text.ElideMiddle
                                    Layout.fillWidth: true
                                }
                            }

                            ToolButton {
                                text: "🎨"
                                Accessible.name: qsTr("Material de %1").arg(modelData.name)
                                implicitHeight: 26
                                // At least 26 px, wider when the glyph needs it (the style elides a clipped label).
                                implicitWidth: Math.max(26, implicitContentWidth + leftPadding + rightPadding)
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
                                ToolTip.text: qsTr("Guardar configuración de material pendiente (sin efecto visual ni exportación)")
                            }

                            ToolButton {
                                text: "+Inv"
                                Accessible.name: qsTr("Añadir %1 al inventario del NPC").arg(modelData.name)
                                implicitHeight: 26
                                implicitWidth: Math.max(34, implicitContentWidth + leftPadding + rightPadding)
                                onClicked: root.outfit.addInventoryItem(modelData, 1)
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("Añadir una copia de esta prenda al inventario del NPC")
                            }

                            ToolButton {
                                text: "✕"
                                Accessible.name: qsTr("Quitar %1 del atuendo").arg(modelData.name)
                                implicitHeight: 26
                                // At least 26 px, wider when the glyph needs it (the style elides a clipped label).
                                implicitWidth: Math.max(26, implicitContentWidth + leftPadding + rightPadding)
                                onClicked: root.outfit.removePiece(modelData.formKey)
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("Quitar pieza del atuendo")
                            }
                        }

                        // A saved material request is intentionally neither rendered nor exportable.
                        RowLayout {
                            visible: modelData.hasMaterial
                            spacing: 4
                            Rectangle {
                                width: 10
                                height: 10
                                radius: 2
                                color: modelData.tintColor !== undefined ? modelData.tintColor : "transparent"
                                border.color: Theme.border
                                border.width: 1
                            }
                            Text {
                                text: qsTr("Material pendiente (sin efecto visual): ") + (modelData.materialName && modelData.materialName.length > 0 ? modelData.materialName : qsTr("Personalizado"))
                                color: Theme.accent
                                font.pixelSize: Theme.micro
                                font.bold: true
                            }
                        }
                    }
                }
            }

            // Mensaje si no hay piezas
            Rectangle {
                visible: root.outfit.pieces.length === 0
                Layout.fillWidth: true
                implicitHeight: 60
                radius: Theme.radius
                color: Theme.panelAlt
                border.color: Theme.borderSubtle
                Text {
                    anchors.centerIn: parent
                    text: qsTr("Sin piezas equipadas.\nAñade prendas desde el catálogo o aplica un preset.")
                    color: Theme.textDim
                    font.pixelSize: Theme.small
                    horizontalAlignment: Text.AlignHCenter
                }
            }

            // ========================================================= INVENTARIO DEL NPC (AG-UI-03)
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                SectionHeader {
                    Layout.fillWidth: true
                    title: qsTr("Inventario del NPC")
                    detail: String((root.outfit.inventoryItems ?? []).length) + (root.outfit.inventoryModified ? qsTr(" · modificado") : "")
                }
                Button {
                    visible: root.outfit.inventoryModified
                    text: qsTr("Restaurar")
                    implicitHeight: 22
                    font.pixelSize: Theme.micro
                    onClicked: root.outfit.resetInventory()
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Restaurar el inventario original del registro del NPC")
                }
            }

            Repeater {
                model: root.outfit.inventoryItems ?? []
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: invRow.implicitHeight + 8
                    radius: Theme.radiusSmall
                    color: Theme.panelAlt
                    border.color: Theme.borderSubtle
                    border.width: 1

                    RowLayout {
                        id: invRow
                        anchors.fill: parent
                        anchors.margins: 4
                        spacing: 4

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1
                            Text {
                                text: String(modelData.name || modelData.editorId || modelData.formKey || "")
                                color: Theme.text
                                font.bold: true
                                font.pixelSize: Theme.small
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Text {
                                text: String(modelData.recordType || "ITEM") + " · " + String(modelData.formKey || "")
                                color: Theme.textDim
                                font.pixelSize: Theme.micro
                                font.family: Theme.mono
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }

                        ToolButton {
                            text: "−"
                            implicitWidth: 24
                            implicitHeight: 24
                            Accessible.name: qsTr("Reducir cantidad de %1").arg(String(modelData.name || modelData.formKey || ""))
                            onClicked: root.outfit.setInventoryItemCount(String(modelData.formKey || ""), (modelData.count ?? 1) - 1)
                        }
                        Text {
                            text: "×" + (modelData.count ?? 1)
                            color: Theme.accent
                            font.bold: true
                            font.pixelSize: Theme.small
                            horizontalAlignment: Text.AlignHCenter
                            Layout.minimumWidth: 28
                        }
                        ToolButton {
                            text: "+"
                            implicitWidth: 24
                            implicitHeight: 24
                            Accessible.name: qsTr("Aumentar cantidad de %1").arg(String(modelData.name || modelData.formKey || ""))
                            onClicked: root.outfit.setInventoryItemCount(String(modelData.formKey || ""), (modelData.count ?? 1) + 1)
                        }
                        ToolButton {
                            text: "✕"
                            implicitWidth: 24
                            implicitHeight: 24
                            Accessible.name: qsTr("Quitar %1 del inventario").arg(String(modelData.name || modelData.formKey || ""))
                            onClicked: root.outfit.removeInventoryItem(String(modelData.formKey || ""))
                        }
                    }
                }
            }

            Rectangle {
                visible: (root.outfit.inventoryItems ?? []).length === 0
                Layout.fillWidth: true
                implicitHeight: 36
                radius: Theme.radiusSmall
                color: Theme.panelAlt
                border.color: Theme.borderSubtle
                Text {
                    anchors.centerIn: parent
                    text: qsTr("Sin objetos en el inventario del NPC (usa '+Inv' en una pieza para añadir).")
                    color: Theme.textDim
                    font.pixelSize: Theme.micro
                }
            }

            // Guardian de Compatibilidad
            SectionHeader {
                title: qsTr("Guardián de Compatibilidad")
                detail: root.outfit.compatibilityReport.badgeText ?? ""
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: guardianCol.implicitHeight + 12
                color: Theme.panelAlt
                radius: Theme.radius
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
                        Text {
                            // Long body names elide instead of widening the panel.
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            horizontalAlignment: Text.AlignRight
                            elide: Text.ElideRight
                            text: (root.outfit.compatibilityReport.body?.name ?? "Vanilla")
                                  + (root.outfit.compatibilityReport.body?.physicsEnabled ? qsTr(" · Físicas detectadas") : "")
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
                            radius: Theme.radiusSmall
                            color: Theme.panel
                            border.color: Theme.severityColor(modelData.severity)

                            RowLayout {
                                id: issueRow
                                anchors.fill: parent
                                anchors.margins: 6
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
                                        text: "<b>" + modelData.code + "</b>: " + modelData.message
                                        color: Theme.text
                                        font.pixelSize: Theme.small
                                        wrapMode: Text.Wrap
                                        Layout.fillWidth: true
                                    }
                                    Text {
                                        visible: Boolean(modelData.remediation && modelData.remediation.length > 0)
                                        text: "→ " + (modelData.remediation ?? "")
                                        color: Theme.textDim
                                        font.pixelSize: Theme.micro
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

            // Botones de acción principales
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Layout.topMargin: 4

                Button {
                    text: qsTr("💾 Guardar en Proyecto")
                    Layout.fillWidth: true
                    implicitHeight: 32
                    onClicked: root.outfit.addCurrentToProject()
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Añade este atuendo al proyecto para exportación individual o por lotes")
                }

                Button {
                    text: root.outfit.exporting ? qsTr("Exportando…") : qsTr("🚀 Exportar Mod…")
                    highlighted: true
                    enabled: root.outfit.exportable && !root.outfit.exporting
                    Layout.fillWidth: true
                    implicitHeight: 32
                    onClicked: root.exportRequested()
                }
            }
        }
    }
}
