import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import SkyrimNpcStudio

ScrollView {
    id: root
    required property StudioController studio
    property Item viewportTarget: null
    signal requestOpenExportWizard()

    contentWidth: availableWidth
    clip: true

    property int activeSection: 0
    property bool showInlineNpcFinder: false
    property string inlineNpcQuery: ""

    // Next-Gen style rule toggles (FASE 4 manual control)
    property bool styleApplyHair: true
    property bool styleApplyEyes: true
    property bool styleApplyMorphs: true
    property bool styleApplyTints: true
    property real styleIntensity: 0.85

    // Library save fields (FASE 6)
    property string presetNameInput: ""
    property string presetCategoryInput: "Custom"
    property string presetNotesInput: ""
    property int librarySubTab: 0

    // Workshop tint color picker state
    property int editingTintIndex: -1
    property real editingTintAlpha: 1.0

    readonly property var lab: root.studio.beautyLab
    readonly property var app: root.studio.appearance
    readonly property var comp: root.lab ? root.lab.comparison : ({})
    readonly property var caps: root.lab ? root.lab.workshopCapabilities : ({})
    readonly property var qcheck: root.lab ? root.lab.qualityCheck : ({})

    ColorDialog {
        id: labHairColorDialog
        title: qsTr("Color de Cabello (HCLF / HairTint)")
        selectedColor: root.app.hairColor
        onAccepted: root.app.setHairColor(selectedColor)
    }

    ColorDialog {
        id: labSkinColorDialog
        title: qsTr("Tono de Piel Base (QNAM)")
        selectedColor: root.app.skinTone
        onAccepted: root.app.setSkinTone(selectedColor)
    }

    ColorDialog {
        id: labTintColorDialog
        title: qsTr("Color de Capa de Tinte Facial")
        onAccepted: {
            if (root.editingTintIndex >= 0) {
                root.app.setTint(root.editingTintIndex, selectedColor, root.editingTintAlpha)
            }
        }
    }

    FileDialog {
        id: importResourceDialog
        title: qsTr("Importar recurso compatible (.jslot o .json) sin redistribuir contenido protegido")
        nameFilters: [
            qsTr("Presets compatibles (*.jslot *.json)"),
            qsTr("RaceMenu Preset (*.jslot)"),
            qsTr("Beauty Lab Preset (*.json)")
        ]
        onAccepted: {
            if (root.lab) {
                root.lab.importCompatibleResource(selectedFile.toString(), root.presetCategoryInput || "Importado")
            }
        }
    }

    function statusTone(status) {
        if (status === "LISTA_PARA_EXPORTAR" || status === "ok" || status === "compatible") return Theme.ok
        if (status === "PENDIENTE_RECONSTRUCCION" || status === "warning") return Theme.warn
        if (status === "BLOQUEADA_RECURSOS_FALTANTES" || status === "error" || status === "incompatible") return Theme.error
        return Theme.textDim
    }

    ColumnLayout {
        width: root.availableWidth
        spacing: 12

        // =====================================================================
        // CABECERA PRINCIPAL: NPC BEAUTY LAB + BUSCADOR INTEGRADO + DESHACER/REHACER
        // =====================================================================
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: topHeaderCol.implicitHeight + 20
            color: Theme.panelAlt
            radius: Theme.radius
            border.color: Theme.border
            border.width: 1

            ColumnLayout {
                id: topHeaderCol
                anchors.fill: parent
                anchors.margins: 10
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        RowLayout {
                            spacing: 8
                            Text {
                                text: qsTr("NPC BEAUTY LAB")
                                color: Theme.accent
                                font.bold: true
                                font.pixelSize: Theme.normal
                            }
                            StatusBadge {
                                text: root.lab ? root.lab.proposalStatus : "SIN_CAMBIOS"
                                tone: root.statusTone(root.lab ? root.lab.proposalStatus : "")
                            }
                            StatusBadge {
                                visible: root.app.loaded
                                text: "PENDIENTE_SKYRIM"
                                tone: Theme.warn
                            }
                        }

                        Text {
                            text: root.app.loaded
                                  ? (root.studio.outfit.npcLabel + " · " + (root.app.raceEditorId || root.caps.raceEditorId || qsTr("Raza")) + (root.app.female ? qsTr(" · Mujer") : qsTr(" · Hombre")))
                                  : qsTr("Ningún NPC seleccionado — usa el buscador inferior o la lista lateral")
                            color: Theme.text
                            font.bold: true
                            font.pixelSize: Theme.small
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }

                    Button {
                        text: root.showInlineNpcFinder ? qsTr("Ocultar Buscador NPC") : qsTr("🔍 Buscar NPC")
                        Accessible.name: qsTr("Alternar buscador rápido de NPC por nombre, EditorID o FormKey")
                        onClicked: root.showInlineNpcFinder = !root.showInlineNpcFinder
                    }
                }

                // Buscador integrado de NPC por nombre, EditorID y FormKey (FASE 1)
                Rectangle {
                    visible: root.showInlineNpcFinder || !root.app.loaded
                    Layout.fillWidth: true
                    implicitHeight: finderCol.implicitHeight + 14
                    color: Theme.panel
                    radius: Theme.radius
                    border.color: Theme.borderSubtle

                    ColumnLayout {
                        id: finderCol
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 6

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            TextField {
                                id: npcSearchInput
                                Layout.fillWidth: true
                                placeholderText: qsTr("Buscar NPC por nombre (ej. Serana, Lydia), EditorID o FormKey (ej. 002B6C:Dawnguard.esm)…")
                                text: root.inlineNpcQuery
                                onTextEdited: {
                                    root.inlineNpcQuery = text
                                    root.studio.searchNpcs(text, "", "unique", "")
                                }
                                onAccepted: root.studio.searchNpcs(text, "", "", "")
                            }

                            Button {
                                text: qsTr("Buscar")
                                onClicked: root.studio.searchNpcs(npcSearchInput.text, "", "", "")
                            }
                        }

                        ListView {
                            Layout.fillWidth: true
                            Layout.preferredHeight: Math.min(150, count * 30 + 6)
                            clip: true
                            model: root.studio.npcResults
                            delegate: Rectangle {
                                required property string formKey
                                required property string name
                                required property string editorId
                                required property string race
                                required property string winningPlugin
                                required property string winningMod
                                width: ListView.view.width
                                height: 28
                                radius: 4
                                color: formKey === root.app.npcFormKey
                                       ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.22)
                                       : (rowMouse.containsMouse ? Theme.hover : "transparent")

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 6
                                    anchors.rightMargin: 6
                                    spacing: 8

                                    Text {
                                        text: (name || editorId) + " (" + editorId + ")"
                                        color: Theme.text
                                        font.bold: formKey === root.app.npcFormKey
                                        font.pixelSize: Theme.small
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }

                                    Text {
                                        text: formKey + (winningMod ? " · " + winningMod : " · " + winningPlugin)
                                        color: Theme.textDim
                                        font.pixelSize: Theme.micro
                                        font.family: Theme.mono
                                    }
                                }

                                MouseArea {
                                    id: rowMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.studio.selectNpc(formKey)
                                }
                            }
                        }
                    }
                }

                // Barra de acciones rápidas: Deshacer / Rehacer / Comparar A-B / Restablecer / Guardar o Exportar
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Button {
                        text: "↶ " + qsTr("Deshacer")
                        enabled: root.studio.history.canUndo
                        ToolTip.visible: hovered && root.studio.history.canUndo
                        ToolTip.text: qsTr("Deshacer: %1").arg(root.studio.history.undoLabel)
                        onClicked: root.studio.history.undo()
                    }

                    Button {
                        text: "↷ " + qsTr("Rehacer")
                        enabled: root.studio.history.canRedo
                        ToolTip.visible: hovered && root.studio.history.canRedo
                        ToolTip.text: qsTr("Rehacer: %1").arg(root.studio.history.redoLabel)
                        onClicked: root.studio.history.redo()
                    }

                    Button {
                        text: (root.lab && root.lab.comparatorMode === "original")
                              ? qsTr("👁 Viendo ORIGINAL (clic para Propuesta)")
                              : qsTr("⇄ Comparar con Original")
                        enabled: root.app.loaded && root.lab && root.lab.hasChanges
                        highlighted: Boolean(root.lab && root.lab.comparatorMode === "original")
                        onClicked: {
                            if (root.lab) {
                                root.lab.setComparatorMode(root.lab.comparatorMode === "original" ? "live" : "original")
                            }
                        }
                    }

                    Item { Layout.fillWidth: true }

                    Button {
                        text: qsTr("Restablecer")
                        enabled: Boolean(root.app.hasFaceEdits)
                        onClicked: root.app.resetFace()
                    }

                    Button {
                        text: (root.lab && root.lab.proposalStatus === "LISTA_PARA_EXPORTAR")
                              ? qsTr("Exportar Mod…")
                              : qsTr("Guardar Propuesta Pendiente")
                        enabled: root.app.loaded && root.lab && root.lab.hasChanges
                        highlighted: Boolean(root.lab && root.lab.proposalStatus === "LISTA_PARA_EXPORTAR")
                        onClicked: {
                            if (root.lab && root.lab.proposalStatus === "LISTA_PARA_EXPORTAR") {
                                root.requestOpenExportWizard()
                            } else if (root.lab) {
                                const autoName = (root.studio.outfit.npcLabel || "NPC") + " - Propuesta"
                                root.lab.saveCurrentAsPreset(autoName, "Propuesta Pendiente", root.lab.proposalStatusDetail, root.viewportTarget)
                                root.activeSection = 5
                            }
                        }
                    }
                }

                // Explicación del estado de la propuesta (impide generar mods aparentemente funcionales cuando está pendiente/bloqueada)
                Text {
                    visible: root.app.loaded
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: (root.lab && root.lab.proposalStatus === "BLOQUEADA_RECURSOS_FALTANTES")
                           ? Theme.error
                           : ((root.lab && root.lab.proposalStatus === "PENDIENTE_RECONSTRUCCION") ? Theme.warn : Theme.textDim)
                    font.pixelSize: Theme.micro
                    text: root.lab ? (root.lab.proposalStatusLabel + " — " + root.lab.proposalStatusDetail) : ""
                }
            }
        }

        // =====================================================================
        // ALERTAS VISUALES SOBRE RECURSOS INCOMPATIBLES O FALTANTES (FASE 1)
        // =====================================================================
        ColumnLayout {
            visible: root.lab && (root.lab.incompatibleAlerts || []).length > 0
            Layout.fillWidth: true
            spacing: 6

            Repeater {
                model: root.lab ? root.lab.incompatibleAlerts : []
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: alertRow.implicitHeight + 12
                    radius: Theme.radius
                    color: modelData.severity === "error"
                           ? Qt.rgba(Theme.error.r, Theme.error.g, Theme.error.b, 0.14)
                           : Qt.rgba(Theme.warn.r, Theme.warn.g, Theme.warn.b, 0.12)
                    border.color: modelData.severity === "error" ? Theme.error : Theme.warn
                    border.width: 1

                    RowLayout {
                        id: alertRow
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 8

                        StatusBadge {
                            text: modelData.severity === "error" ? qsTr("RECURSO FALTANTE") : qsTr("COMPATIBILIDAD")
                            tone: modelData.severity === "error" ? Theme.error : Theme.warn
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                text: modelData.title || ""
                                color: Theme.text
                                font.bold: true
                                font.pixelSize: Theme.small
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                            Text {
                                text: modelData.message || ""
                                color: Theme.textDim
                                font.pixelSize: Theme.micro
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                        }
                    }
                }
            }
        }

        // =====================================================================
        // NAVEGACIÓN DE SECCIONES DE NPC BEAUTY LAB (FASES 1 a 6)
        // =====================================================================
        SegmentedControl {
            Layout.fillWidth: true
            stretch: true
            model: [
                { label: qsTr("Hair Studio"), glyph: "tag", tip: qsTr("FASE 2: Descubrimiento de peinados MO2, paquetes, mallas/texturas e inspector NIF") },
                { label: qsTr("Face Workshop"), glyph: "Sliders", tip: qsTr("FASE 3: Morfología NAM9, presets raciales, ojos, piel, maquillaje y cicatrices") },
                { label: qsTr("Comparador & MO2"), glyph: "check", tip: qsTr("FASE 1: Comparador Original vs Propuesta, procedencia MO2, variantes e historial") },
                { label: qsTr("Estilos Next-Gen"), glyph: "sparkle", tip: qsTr("FASE 4: Vanilla Remastered, Lore Friendly HD y Cinematic Fantasy") },
                { label: qsTr("Calidad Visual"), glyph: "eye", tip: qsTr("FASE 5: Verificación geométrica de cuello, cabello, texturas y capturas reproducibles") },
                { label: qsTr("Biblioteca"), glyph: "folder", tip: qsTr("FASE 6: Biblioteca persistente de presets, cabellos favoritos y recursos") }
            ]
            currentIndex: root.activeSection
            onActivated: index => root.activeSection = index
            accessibleName: qsTr("Secciones del laboratorio de belleza de NPC")
        }

        // =====================================================================
        // SECCIÓN 0 — FASE 2: HAIR STUDIO (Y SELECTOR AVANZADO DE HEADPARTS)
        // =====================================================================
        ColumnLayout {
            visible: root.activeSection === 0
            Layout.fillWidth: true
            spacing: 10

            SectionHeader {
                title: qsTr("Hair Studio · Descubrimiento y Auditoría de HeadParts en MO2")
                detail: qsTr("%1 opciones filtradas").arg(root.lab ? (root.lab.filteredHeadParts || []).length : 0)
            }

            // Selector de tipo de HeadPart
            RowLayout {
                Layout.fillWidth: true
                spacing: 4

                Repeater {
                    model: [
                        { id: "Hair", label: qsTr("Cabello") },
                        { id: "Eyes", label: qsTr("Ojos") },
                        { id: "Eyebrows", label: qsTr("Cejas") },
                        { id: "FacialHair", label: qsTr("Barba") },
                        { id: "Scar", label: qsTr("Cicatrices") }
                    ]
                    delegate: Button {
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: 28
                        text: modelData.label
                        highlighted: Boolean(root.lab && root.lab.hairCategory === modelData.id)
                        onClicked: if (root.lab) root.lab.setHairCategory(modelData.id)
                    }
                }
            }

            // Filtros por paquete detectado en MO2 (KS Hairdos, ApachiiSkyHair, HG Hairdos, SMP, Vanilla, etc.)
            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                Text {
                    text: qsTr("Paquete MO2:")
                    color: Theme.textDim
                    font.pixelSize: Theme.small
                }

                ComboBox {
                    Layout.fillWidth: true
                    model: root.lab ? root.lab.availablePackages : []
                    textRole: "label"
                    valueRole: "id"
                    onActivated: {
                        if (root.lab && currentValue !== undefined) {
                            root.lab.setHairPackageFilter(String(currentValue))
                        }
                    }
                }

                ComboBox {
                    Layout.preferredWidth: 190
                    model: [
                        { id: "all", label: qsTr("Todos los estados") },
                        { id: "compatible-only", label: qsTr("Solo verificados OK") },
                        { id: "favorites-only", label: qsTr("★ Solo favoritos") },
                        { id: "smp-only", label: qsTr("Con física HDT-SMP") },
                        { id: "overridden", label: qsTr("Sobrescritos en MO2") },
                        { id: "missing-assets", label: qsTr("Con malla ausente") },
                        { id: "bald-only", label: qsTr("Opciones sin pelo/calvo") }
                    ]
                    textRole: "label"
                    valueRole: "id"
                    onActivated: {
                        if (root.lab && currentValue !== undefined) {
                            root.lab.setHairCompatibilityFilter(String(currentValue))
                        }
                    }
                }
            }

            // Buscador de peinados + botón de cancelar carga + modo calvo intencional
            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                TextField {
                    Layout.fillWidth: true
                    placeholderText: qsTr("Filtrar por nombre, EditorID, FormKey, plugin o mod de MO2…")
                    text: root.lab ? root.lab.hairSearchText : ""
                    onTextEdited: if (root.lab) root.lab.setHairSearchText(text)
                }

                Button {
                    visible: Boolean(root.app.searchingHeadParts)
                    text: qsTr("Cancelar carga")
                    onClicked: root.app.cancelHeadPartSearch()
                }

                Button {
                    text: "↺"
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Recargar catálogo de HeadParts del perfil MO2")
                    onClicked: if (root.lab) root.lab.refreshHeadPartGallery()
                }
            }

            // Controles rápidos de cabello (Calvo intencional + Color de cabello)
            Rectangle {
                visible: Boolean(root.lab && root.lab.hairCategory === "Hair")
                Layout.fillWidth: true
                implicitHeight: hairQuickRow.implicitHeight + 12
                color: Theme.panelAlt
                radius: Theme.radius
                border.color: Theme.borderSubtle

                RowLayout {
                    id: hairQuickRow
                    anchors.fill: parent
                    anchors.margins: 6
                    spacing: 10

                    CheckBox {
                        text: qsTr("NPC Calvo Intencionalmente")
                        checked: root.app.intentionallyBald
                        enabled: Boolean(root.caps.allowsHeadPartSwap)
                        onToggled: root.app.setIntentionallyBald(checked)
                    }

                    Item { Layout.fillWidth: true }

                    Rectangle {
                        width: 22
                        height: 22
                        radius: 4
                        color: root.app.hairColor
                        border.color: Theme.border
                    }

                    Button {
                        text: qsTr("Color HairTint (%1)").arg(root.app.hairColor.toString().toUpperCase())
                        enabled: Boolean(root.caps.allowsHairColor)
                        onClicked: labHairColorDialog.open()
                    }
                }
            }

            // Lista de HeadParts con miniatura/icono, procedencia MO2, alertas de mallas faltantes y botón Favorito
            ListView {
                id: headPartListView
                Layout.fillWidth: true
                Layout.preferredHeight: 280
                clip: true
                spacing: 4
                model: root.lab ? root.lab.filteredHeadParts : []

                delegate: Rectangle {
                    required property var modelData
                    width: ListView.view.width
                    height: 62
                    radius: Theme.radius
                    color: modelData.isCurrent
                           ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.18)
                           : (hpMouse.containsMouse ? Theme.hover : Theme.panelAlt)
                    border.color: !modelData.meshFound
                                  ? Theme.error
                                  : (modelData.isCurrent ? Theme.accent : Theme.borderSubtle)
                    border.width: 1

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 8

                        // Miniatura generada / insignia visual del tipo y paquete
                        Rectangle {
                            width: 46
                            height: 46
                            radius: 6
                            color: !modelData.meshFound
                                   ? Qt.rgba(Theme.error.r, Theme.error.g, Theme.error.b, 0.2)
                                   : Qt.rgba(Theme.control.r, Theme.control.g, Theme.control.b, 0.7)
                            border.color: modelData.hasSmp ? Theme.accent : Theme.borderSubtle

                            ColumnLayout {
                                anchors.centerIn: parent
                                spacing: 1
                                Text {
                                    Layout.alignment: Qt.AlignHCenter
                                    text: modelData.isBaldPart ? "○" : (modelData.type === "Eyes" ? "👁" : "✂")
                                    font.pixelSize: Theme.normal
                                    color: Theme.text
                                }
                                Text {
                                    Layout.alignment: Qt.AlignHCenter
                                    text: modelData.hasSmp ? "SMP" : (modelData.assetKind || "NIF").toUpperCase()
                                    font.pixelSize: 9
                                    font.bold: true
                                    color: modelData.hasSmp ? Theme.accent : Theme.textDim
                                }
                            }
                        }

                        // Datos principales de la pieza
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6

                                Text {
                                    text: modelData.name || modelData.editorId || modelData.formKey
                                    color: Theme.text
                                    font.bold: true
                                    font.pixelSize: Theme.small
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }

                                StatusBadge {
                                    text: modelData.packageFamily || modelData.plugin || "Mod"
                                    tone: Theme.textDim
                                }

                                StatusBadge {
                                    visible: !modelData.meshFound
                                    text: qsTr("FALTA MALLA NIF")
                                    tone: Theme.error
                                }

                                StatusBadge {
                                    visible: (modelData.overrideCount || 0) > 1
                                    text: qsTr("%1 mods").arg(modelData.overrideCount)
                                    tone: Theme.warn
                                }

                                StatusBadge {
                                    visible: Boolean(modelData.requiresFaceGenRebuild)
                                    text: qsTr("Reconstruye FaceGen")
                                    tone: Theme.accent
                                }
                            }

                            Text {
                                text: qsTr("FormKey: %1 · Plugin: %2 · Proveedor MO2: %3")
                                      .arg(modelData.formKey || "")
                                      .arg(modelData.plugin || "")
                                      .arg(modelData.effectiveProvider || modelData.pluginMod || qsTr("Juego base / BSA"))
                                color: Theme.textDim
                                font.pixelSize: Theme.micro
                                font.family: Theme.mono
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }

                        // Botón de favorito
                        Button {
                            text: modelData.isFavorite ? "★" : "☆"
                            implicitWidth: 30
                            implicitHeight: 28
                            ToolTip.visible: hovered
                            ToolTip.text: modelData.isFavorite ? qsTr("Quitar de cabellos favoritos") : qsTr("Guardar en cabellos favoritos")
                            onClicked: if (root.lab) root.lab.toggleFavoriteHair(modelData)
                        }

                        // Botón de inspeccionar NIF/DDS
                        Button {
                            text: qsTr("Inspeccionar")
                            implicitHeight: 28
                            onClicked: if (root.lab) root.lab.inspectHeadPart(modelData)
                        }

                        // Botón de aplicar al NPC en el visor 3D real
                        Button {
                            text: modelData.isCurrent ? qsTr("Activo") : qsTr("Previsualizar")
                            implicitHeight: 28
                            enabled: Boolean(root.caps.allowsHeadPartSwap) && Boolean(modelData.meshFound)
                            highlighted: !modelData.isCurrent && Boolean(modelData.meshFound)
                            onClicked: {
                                if (root.lab) {
                                    root.lab.inspectHeadPart(modelData)
                                    if (modelData.isBaldPart && modelData.type === "Hair") {
                                        root.app.setIntentionallyBald(true)
                                    } else {
                                        root.lab.selectAndApplyHeadPart(modelData.formKey, modelData.type)
                                    }
                                }
                            }
                        }
                    }

                    MouseArea {
                        id: hpMouse
                        anchors.fill: parent
                        acceptedButtons: Qt.NoButton
                        hoverEnabled: true
                    }
                }
            }

            // Inspector Técnico Profundo de la pieza seleccionada (NIF + Texturas DDS + TRI + Overrides MO2)
            Rectangle {
                id: inspectCard
                visible: Boolean(root.lab && root.lab.selectedHeadPartInspection && root.lab.selectedHeadPartInspection.formKey)
                Layout.fillWidth: true
                implicitHeight: inspectCol.implicitHeight + 16
                color: Theme.panelAlt
                radius: Theme.radius
                border.color: Theme.border
                border.width: 1

                readonly property var insp: (root.lab && root.lab.selectedHeadPartInspection) ? root.lab.selectedHeadPartInspection : ({})

                ColumnLayout {
                    id: inspectCol
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Text {
                            text: qsTr("INSPECTOR DE RECURSO: %1 (%2)").arg(inspectCard.insp.name || "").arg(inspectCard.insp.formKey || "")
                            color: Theme.accent
                            font.bold: true
                            font.pixelSize: Theme.small
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }

                        StatusBadge {
                            text: inspectCard.insp.compatibilityState || "compatible"
                            tone: root.statusTone(inspectCard.insp.compatibilityState || "ok")
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        font.family: Theme.mono
                        font.pixelSize: Theme.micro
                        color: Theme.text
                        wrapMode: Text.WrapAnywhere
                        text: qsTr("Malla NIF: %1\nPlugin propietario: %2 (Mod: %3) · Proveedor efectivo MO2: %4 (%5)\nGeometría: %6 shape(s), %7 vértices, %8 triángulos · Shader HairTint: %9 · Física SMP: %10")
                              .arg(inspectCard.insp.modelPath || qsTr("(sin malla / calvo)"))
                              .arg(inspectCard.insp.plugin || "-")
                              .arg(inspectCard.insp.pluginMod || "-")
                              .arg(inspectCard.insp.effectiveProvider || "-")
                              .arg(inspectCard.insp.assetKind || "-")
                              .arg(inspectCard.insp.shapeCount || 0)
                              .arg(inspectCard.insp.vertexCount || 0)
                              .arg(inspectCard.insp.triangleCount || 0)
                              .arg(inspectCard.insp.supportsHairTint ? qsTr("Sí") : qsTr("No"))
                              .arg(inspectCard.insp.hasSmp ? (qsTr("Sí (") + (inspectCard.insp.smpXmlPath || "XML embebido") + ")") : qsTr("No"))
                    }

                    Text {
                        visible: (inspectCard.insp.overriddenBy || []).length > 1
                        Layout.fillWidth: true
                        color: Theme.warn
                        font.pixelSize: Theme.micro
                        wrapMode: Text.WordWrap
                        text: qsTr("Cadena de sobrescritura en MO2 (%1 mods): %2")
                              .arg((inspectCard.insp.overriddenBy || []).length)
                              .arg((inspectCard.insp.overriddenBy || []).join(" → "))
                    }

                    Text {
                        visible: (inspectCard.insp.missingTextures || []).length > 0
                        Layout.fillWidth: true
                        color: Theme.error
                        font.bold: true
                        font.pixelSize: Theme.micro
                        wrapMode: Text.WordWrap
                        text: qsTr("⚠ Texturas DDS faltantes (%1): %2")
                              .arg((inspectCard.insp.missingTextures || []).length)
                              .arg((inspectCard.insp.missingTextures || []).join(", "))
                    }

                    Text {
                        Layout.fillWidth: true
                        color: Theme.textDim
                        font.pixelSize: Theme.micro
                        wrapMode: Text.WordWrap
                        text: inspectCard.insp.faceGenNotice || ""
                    }
                }
            }
        }

        // =====================================================================
        // SECCIÓN 1 — FASE 3: FACE BEAUTY WORKSHOP
        // =====================================================================
        ColumnLayout {
            visible: root.activeSection === 1
            Layout.fillWidth: true
            spacing: 12

            // Tarjeta de diagnóstico de geometría paramétrica vs importada y límites técnicos
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: fwCapCol.implicitHeight + 16
                radius: Theme.radius
                color: root.caps.allowsParametricMorphs
                       ? Qt.rgba(Theme.ok.r, Theme.ok.g, Theme.ok.b, 0.08)
                       : Qt.rgba(Theme.warn.r, Theme.warn.g, Theme.warn.b, 0.14)
                border.color: root.caps.allowsParametricMorphs ? Theme.ok : Theme.warn
                border.width: 1

                ColumnLayout {
                    id: fwCapCol
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 4

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        StatusBadge {
                            text: root.caps.geometryBadge || qsTr("Estado de Geometría")
                            tone: root.caps.allowsParametricMorphs ? Theme.ok : Theme.warn
                        }
                        Text {
                            text: qsTr("Sliders NAM9 [-1.0, +1.0] · Presets NAMA: %1 · Máscaras Tinte: %2")
                                  .arg(root.caps.presetSlotCount || 0)
                                  .arg(root.caps.tintMaskCount || 0)
                            color: Theme.textDim
                            font.pixelSize: Theme.micro
                            font.family: Theme.mono
                        }
                        Item { Layout.fillWidth: true }
                        Button {
                            visible: Boolean(root.app.faceCompatibility.canUnlockForModding)
                            text: qsTr("Desbloquear rostro para modear")
                            highlighted: true
                            onClicked: root.app.unlockForModding()
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.text
                        font.pixelSize: Theme.small
                        text: root.caps.geometrySummary || ""
                    }
                }
            }

            // Controles escalares de Color Facial (QNAM), Peso (NAM8), Altura y Rasgos Raciales (NAMA)
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: racialCol.implicitHeight + 16
                color: Theme.panelAlt
                radius: Theme.radius
                border.color: Theme.borderSubtle

                ColumnLayout {
                    id: racialCol
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 8

                    SectionHeader {
                        title: qsTr("Color Facial, Proporción Corporal y Rasgos Raciales (NAMA)")
                        detail: root.app.raceEditorId || root.caps.raceEditorId || ""
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Rectangle {
                            width: 24
                            height: 24
                            radius: 4
                            color: root.app.skinTone
                            border.color: Theme.border
                        }

                        Button {
                            text: qsTr("Tono de Piel QNAM (%1)").arg(root.app.skinTone.toString().toUpperCase())
                            enabled: Boolean(root.caps.allowsSkinToneAndTints)
                            onClicked: labSkinColorDialog.open()
                        }

                        Item { Layout.fillWidth: true }

                        Text {
                            text: qsTr("Peso: %1").arg(Math.round(root.app.weight))
                            color: Theme.text
                            font.pixelSize: Theme.small
                        }

                        Slider {
                            Layout.preferredWidth: 110
                            from: 0
                            to: 100
                            stepSize: 1
                            value: root.app.weight
                            onMoved: root.app.setWeight(value)
                        }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 10
                        rowSpacing: 6

                        Repeater {
                            model: root.app.presetSlots || []
                            delegate: RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 6

                                Text {
                                    text: (modelData.label || modelData.displayNameEs || "") + ":"
                                    color: Theme.text
                                    font.pixelSize: Theme.small
                                    Layout.preferredWidth: 115
                                    elide: Text.ElideRight
                                }

                                SpinBox {
                                    from: -1
                                    to: Math.max(0, modelData.maxPreset !== undefined ? modelData.maxPreset : (modelData.max || 0))
                                    value: modelData.value !== undefined ? modelData.value : 0
                                    enabled: Boolean(root.caps.allowsPresets) && Boolean(modelData.editable !== undefined ? modelData.editable : true)
                                    onValueModified: root.app.setFacePreset(modelData.slot, value)
                                }
                            }
                        }
                    }
                }
            }

            // Categorías de Morphs Paramétricos (NAM9)
            Repeater {
                model: [
                    { key: "Nose", title: qsTr("Nariz (Morphs NAM9)") },
                    { key: "Eyes", title: qsTr("Ojos (Morphs NAM9)") },
                    { key: "Brows", title: qsTr("Cejas (Morphs NAM9)") },
                    { key: "Cheeks", title: qsTr("Mejillas y Pómulos (Morphs NAM9)") },
                    { key: "Lips", title: qsTr("Labios y Boca (Morphs NAM9)") },
                    { key: "Jaw", title: qsTr("Mandíbula (Morphs NAM9)") },
                    { key: "Chin", title: qsTr("Barbilla (Morphs NAM9)") }
                ]
                delegate: FaceSliderGroup {
                    required property var modelData
                    Layout.fillWidth: true
                    enabled: Boolean(root.caps.allowsParametricMorphs)
                    title: modelData.title
                    morphValues: root.app.morphs
                    sliders: root.app.standardSliders.filter(s => s.category === modelData.key)
                    onSliderChanged: (k, val) => root.app.setSlider(k, val)
                }
            }

            // Capas de Tinte Facial, Maquillaje, Pintura y Cicatrices
            SectionHeader {
                title: qsTr("Capas de Tinte Facial, Maquillaje y Cicatrices")
                detail: qsTr("%1 capas disponibles").arg((root.app.tintCatalog || []).length)
            }

            Repeater {
                model: root.app.tintCatalog || []
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: fwTintRow.implicitHeight + 10
                    color: modelData.active ? Theme.panelAlt : Qt.rgba(Theme.panel.r, Theme.panel.g, Theme.panel.b, 0.5)
                    radius: Theme.radius
                    border.color: modelData.active ? Theme.accent : Theme.borderSubtle

                    RowLayout {
                        id: fwTintRow
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 8

                        Rectangle {
                            width: 20
                            height: 20
                            radius: 4
                            color: modelData.color || "#ffffff"
                            border.color: Theme.border
                            MouseArea {
                                anchors.fill: parent
                                enabled: Boolean(root.caps.allowsSkinToneAndTints)
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    root.editingTintIndex = modelData.index
                                    root.editingTintAlpha = modelData.value > 0 ? modelData.value : 0.5
                                    labTintColorDialog.selectedColor = Qt.color(modelData.color || "#ffffff")
                                    labTintColorDialog.open()
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1
                            Text {
                                text: modelData.label + " [" + (modelData.category || "") + "]"
                                color: Theme.text
                                font.bold: Boolean(modelData.active)
                                font.pixelSize: Theme.small
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Text {
                                text: (modelData.texture || "") + (modelData.provider ? " · MO2: " + modelData.provider : "")
                                color: modelData.textureFound ? Theme.textDim : Theme.error
                                font.pixelSize: Theme.micro
                                font.family: Theme.mono
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                            }
                        }

                        Slider {
                            Layout.preferredWidth: 95
                            from: 0.0
                            to: 1.0
                            value: modelData.value || 0.0
                            enabled: Boolean(root.caps.allowsSkinToneAndTints)
                            onMoved: root.app.setTint(modelData.index, Qt.color(modelData.color || "#ffffff"), value)
                        }

                        Text {
                            text: Math.round((modelData.value || 0) * 100) + "%"
                            color: Theme.text
                            font.pixelSize: Theme.micro
                            Layout.preferredWidth: 32
                        }
                    }
                }
            }
        }

        // =====================================================================
        // SECCIÓN 2 — FASE 1: COMPARADOR ORIGINAL VS PROPUESTA, PROCEDENCIA MO2 E HISTORIAL
        // =====================================================================
        ColumnLayout {
            visible: root.activeSection === 2
            Layout.fillWidth: true
            spacing: 12

            SectionHeader {
                title: qsTr("Comparador de Apariencia: Original vs Propuesta")
                detail: qsTr("%1 cambio(s) activo(s)").arg(root.lab ? root.lab.changeCount : 0)
            }

            // Conmutador directo del visor 3D (Original vs Propuesta)
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Button {
                    Layout.fillWidth: true
                    text: qsTr("Mostrar Apariencia PROPUESTA en el Visor 3D")
                    highlighted: Boolean(root.lab && root.lab.comparatorMode !== "original")
                    onClicked: if (root.lab) root.lab.setComparatorMode("live")
                }

                Button {
                    Layout.fillWidth: true
                    text: qsTr("Mostrar Apariencia ORIGINAL en el Visor 3D")
                    highlighted: Boolean(root.lab && root.lab.comparatorMode === "original")
                    onClicked: if (root.lab) root.lab.setComparatorMode("original")
                }
            }

            // Tabla de diferencias (HeadParts, Morphs, Presets, Tints, Scalars) con botón de revertir individual
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: diffCol.implicitHeight + 16
                color: Theme.panelAlt
                radius: Theme.radius
                border.color: Theme.borderSubtle

                ColumnLayout {
                    id: diffCol
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 6

                    Text {
                        visible: !root.comp.hasChanges
                        text: qsTr("No hay diferencias entre la apariencia original del perfil y la propuesta actual.")
                        color: Theme.textDim
                        font.pixelSize: Theme.small
                    }

                    // Cambios de HeadParts
                    Repeater {
                        model: root.comp.headPartChanges || []
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 8
                            StatusBadge { text: modelData.label || modelData.type; tone: Theme.accent }
                            Text {
                                Layout.fillWidth: true
                                color: Theme.text
                                font.pixelSize: Theme.small
                                elide: Text.ElideRight
                                text: (modelData.originalName || "-") + " → " + (modelData.proposedName || "-")
                            }
                            Button {
                                text: qsTr("Revertir")
                                implicitHeight: 22
                                onClicked: root.lab.revertSingleChange("headPart", modelData.type)
                            }
                        }
                    }

                    // Cambios Escalares
                    Repeater {
                        model: root.comp.scalarChanges || []
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 8
                            StatusBadge { text: modelData.label; tone: Theme.ok }
                            Text {
                                Layout.fillWidth: true
                                color: Theme.text
                                font.pixelSize: Theme.small
                                text: String(modelData.originalValue) + " → " + String(modelData.proposedValue)
                            }
                            Button {
                                text: qsTr("Revertir")
                                implicitHeight: 22
                                onClicked: root.lab.revertSingleChange("scalar", modelData.key)
                            }
                        }
                    }

                    // Cambios de Morphs
                    Repeater {
                        model: root.comp.morphChanges || []
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 8
                            StatusBadge { text: modelData.category || "Morph"; tone: Theme.textDim }
                            Text {
                                Layout.fillWidth: true
                                color: Theme.text
                                font.pixelSize: Theme.small
                                text: modelData.label + ": " + Number(modelData.originalValue).toFixed(2) + " → " + Number(modelData.proposedValue).toFixed(2)
                            }
                            Button {
                                text: qsTr("Revertir")
                                implicitHeight: 22
                                onClicked: root.lab.revertSingleChange(modelData.category === "Custom" ? "customMorph" : "morph", modelData.key)
                            }
                        }
                    }

                    // Cambios de Presets raciales
                    Repeater {
                        model: root.comp.presetChanges || []
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 8
                            StatusBadge { text: "NAMA"; tone: Theme.textDim }
                            Text {
                                Layout.fillWidth: true
                                color: Theme.text
                                font.pixelSize: Theme.small
                                text: modelData.label + ": #" + modelData.originalValue + " → #" + modelData.proposedValue
                            }
                            Button {
                                text: qsTr("Revertir")
                                implicitHeight: 22
                                onClicked: root.lab.revertSingleChange("preset", String(modelData.slot))
                            }
                        }
                    }

                    // Cambios de Tintes
                    Repeater {
                        model: root.comp.tintChanges || []
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 8
                            StatusBadge { text: qsTr("Tinte #%1").arg(modelData.index); tone: Theme.textDim }
                            Text {
                                Layout.fillWidth: true
                                color: Theme.text
                                font.pixelSize: Theme.small
                                elide: Text.ElideRight
                                text: modelData.label + ": " + Math.round((modelData.originalAlpha || 0) * 100) + "% → " + Math.round((modelData.proposedAlpha || 0) * 100) + "% (" + (modelData.proposedColor || "") + ")"
                            }
                            Button {
                                text: qsTr("Revertir")
                                implicitHeight: 22
                                onClicked: root.lab.revertSingleChange("tint", String(modelData.index))
                            }
                        }
                    }
                }
            }

            // Panel de Recursos Utilizados y Procedencia MO2 (con cadena de overrides)
            SectionHeader {
                title: qsTr("Panel de Recursos Utilizados y Procedencia MO2")
                detail: qsTr("%1 recursos rastreados").arg(root.lab ? (root.lab.provenance || []).length : 0)
            }

            Repeater {
                model: root.lab ? root.lab.provenance : []
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: provCol.implicitHeight + 12
                    color: Theme.panelAlt
                    radius: Theme.radius
                    border.color: modelData.found === false ? Theme.error : Theme.borderSubtle

                    ColumnLayout {
                        id: provCol
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 2

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            StatusBadge {
                                text: modelData.category || modelData.type
                                tone: modelData.found === false ? Theme.error : Theme.accent
                            }
                            Text {
                                text: (modelData.editorId || modelData.formKey || "") + (modelData.plugin ? " (" + modelData.plugin + ")" : "")
                                color: Theme.text
                                font.bold: true
                                font.pixelSize: Theme.small
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            StatusBadge {
                                text: (modelData.assetKind || "bsa").toUpperCase() + " · " + (modelData.effectiveProvider || qsTr("Base"))
                                tone: (modelData.overrideCount || 0) > 1 ? Theme.warn : Theme.ok
                            }
                        }

                        Text {
                            visible: Boolean(modelData.assetPath)
                            text: modelData.assetPath || ""
                            color: Theme.textDim
                            font.pixelSize: Theme.micro
                            font.family: Theme.mono
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                        }

                        Text {
                            visible: (modelData.overriddenBy || []).length > 1
                            text: qsTr("Sobrescrito en MO2 (%1): %2")
                                  .arg((modelData.overriddenBy || []).length)
                                  .arg((modelData.overriddenBy || []).join(" → "))
                            color: Theme.warn
                            font.pixelSize: Theme.micro
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }
                }
            }

            // Selector de Variantes de Apariencia de Mods instalados (AppearanceSourceSection)
            SectionHeader {
                title: qsTr("Selector de Variantes de Apariencia en el Perfil MO2")
                detail: qsTr("Comparar o adoptar rostros de otros mods")
            }

            AppearanceSourceSection {
                Layout.fillWidth: true
                studio: root.studio
            }

            // Historial de Modificaciones (FASE 1)
            SectionHeader {
                title: qsTr("Historial de Modificaciones (Undo / Redo)")
                detail: qsTr("Paso %1 de %2").arg(root.studio.history.position).arg(root.studio.history.count)
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: histCol.implicitHeight + 12
                color: Theme.panelAlt
                radius: Theme.radius
                border.color: Theme.borderSubtle

                ColumnLayout {
                    id: histCol
                    anchors.fill: parent
                    anchors.margins: 6
                    spacing: 4

                    Text {
                        visible: (root.studio.history.entries || []).length === 0
                        text: qsTr("Aún no se han registrado ediciones en la sesión actual de este NPC.")
                        color: Theme.textDim
                        font.pixelSize: Theme.small
                    }

                    Repeater {
                        model: root.studio.history.entries || []
                        delegate: RowLayout {
                            required property var modelData
                            required property int index
                            Layout.fillWidth: true
                            spacing: 6

                            StatusBadge {
                                text: "#" + (index + 1)
                                tone: modelData.undone ? Theme.textDim : Theme.ok
                            }
                            Text {
                                Layout.fillWidth: true
                                text: modelData.label || ""
                                color: modelData.undone ? Theme.textDim : Theme.text
                                font.strikeout: Boolean(modelData.undone)
                                font.pixelSize: Theme.small
                            }
                        }
                    }
                }
            }
        }

        // =====================================================================
        // SECCIÓN 3 — FASE 4: NEXT-GEN VISUAL PRESETS
        // =====================================================================
        ColumnLayout {
            visible: root.activeSection === 3
            Layout.fillWidth: true
            spacing: 12

            SectionHeader {
                title: qsTr("Next-Gen Visual Presets · Biblioteca de Reglas Estéticas")
                detail: qsTr("Control manual sin asumir mods externos")
            }

            // Panel de control manual sobre los grupos de reglas
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: styleOptCol.implicitHeight + 16
                color: Theme.panelAlt
                radius: Theme.radius
                border.color: Theme.borderSubtle

                ColumnLayout {
                    id: styleOptCol
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 6

                    Text {
                        text: qsTr("Grupos de reglas que se aplicarán al ejecutar un estilo:")
                        color: Theme.text
                        font.bold: true
                        font.pixelSize: Theme.small
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        CheckBox { text: qsTr("Cabello"); checked: root.styleApplyHair; onToggled: root.styleApplyHair = checked }
                        CheckBox { text: qsTr("Ojos"); checked: root.styleApplyEyes; onToggled: root.styleApplyEyes = checked }
                        CheckBox { text: qsTr("Morfología"); checked: root.styleApplyMorphs; onToggled: root.styleApplyMorphs = checked }
                        CheckBox { text: qsTr("Maquillaje/Tintes"); checked: root.styleApplyTints; onToggled: root.styleApplyTints = checked }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Text {
                            text: qsTr("Intensidad de reglas (%1%):").arg(Math.round(root.styleIntensity * 100))
                            color: Theme.textDim
                            font.pixelSize: Theme.small
                        }
                        Slider {
                            Layout.fillWidth: true
                            from: 0.15
                            to: 1.0
                            value: root.styleIntensity
                            onMoved: root.styleIntensity = value
                        }
                    }
                }
            }

            // Tarjetas de los 3 estilos Next-Gen (Vanilla Remastered, Lore Friendly HD, Cinematic Fantasy)
            Repeater {
                model: root.lab ? root.lab.stylePresets : []
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: styleCardCol.implicitHeight + 18
                    color: Theme.panelAlt
                    radius: Theme.radius
                    border.color: Theme.border

                    ColumnLayout {
                        id: styleCardCol
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 6

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Text {
                                text: modelData.name || ""
                                color: Theme.text
                                font.bold: true
                                font.pixelSize: Theme.normal
                            }
                            StatusBadge {
                                text: modelData.badge || ""
                                tone: Theme.accent
                            }
                            Item { Layout.fillWidth: true }
                            Button {
                                text: qsTr("Aplicar %1").arg(modelData.name)
                                enabled: root.app.loaded
                                highlighted: true
                                onClicked: {
                                    if (root.lab) {
                                        root.lab.applyStylePreset(modelData.id, {
                                            applyHair: root.styleApplyHair,
                                            applyEyes: root.styleApplyEyes,
                                            applyMorphs: root.styleApplyMorphs,
                                            applyTints: root.styleApplyTints,
                                            intensity: root.styleIntensity
                                        })
                                    }
                                }
                            }
                        }

                        Text {
                            text: modelData.tagline || ""
                            color: Theme.accent
                            font.pixelSize: Theme.small
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }

                        Text {
                            text: modelData.description || ""
                            color: Theme.textDim
                            font.pixelSize: Theme.micro
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }

                        Repeater {
                            model: modelData.rules || []
                            delegate: Text {
                                required property string modelData
                                text: "• " + modelData
                                color: Theme.text
                                font.pixelSize: Theme.micro
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                }
            }

            // Resultado de la última ejecución de estilo
            Rectangle {
                visible: Boolean(root.lab && root.lab.lastStyleResult && root.lab.lastStyleResult.summary)
                Layout.fillWidth: true
                implicitHeight: styleResCol.implicitHeight + 14
                color: Theme.panel
                radius: Theme.radius
                border.color: Theme.accent

                ColumnLayout {
                    id: styleResCol
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 4

                    Text {
                        text: (root.lab && root.lab.lastStyleResult) ? (root.lab.lastStyleResult.summary || "") : ""
                        color: Theme.text
                        font.bold: true
                        font.pixelSize: Theme.small
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    Repeater {
                        model: (root.lab && root.lab.lastStyleResult) ? (root.lab.lastStyleResult.appliedRules || []) : []
                        delegate: Text {
                            required property string modelData
                            text: "✓ " + modelData
                            color: Theme.ok
                            font.pixelSize: Theme.micro
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }

                    Repeater {
                        model: (root.lab && root.lab.lastStyleResult) ? (root.lab.lastStyleResult.skippedRules || []) : []
                        delegate: Text {
                            required property string modelData
                            text: "↷ " + modelData
                            color: Theme.warn
                            font.pixelSize: Theme.micro
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }
                }
            }

            // Importar recursos compatibles (.jslot / .json) sin redistribuir contenido protegido
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Button {
                    text: qsTr("Importar Recurso Compatible (.jslot / .json)…")
                    onClicked: importResourceDialog.open()
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Importa definiciones paramétricas y referencias de FormKey sin copiar ni redistribuir mallas o texturas protegidas.")
                    color: Theme.textDim
                    font.pixelSize: Theme.micro
                    wrapMode: Text.WordWrap
                }
            }
        }

        // =====================================================================
        // SECCIÓN 4 — FASE 5: VISUAL QUALITY CHECK
        // =====================================================================
        ColumnLayout {
            visible: root.activeSection === 4
            Layout.fillWidth: true
            spacing: 12

            SectionHeader {
                title: qsTr("Visual Quality Check · Auditoría Geométrica y de Recursos")
                detail: qsTr("Estado: %1").arg((root.qcheck.overallStatus || "idle").toUpperCase())
            }

            // Aviso explícito de limitación del motor de renderizado frente a Skyrim (FASE 5)
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: limitCol.implicitHeight + 14
                color: Qt.rgba(Theme.warn.r, Theme.warn.g, Theme.warn.b, 0.12)
                radius: Theme.radius
                border.color: Theme.warn

                ColumnLayout {
                    id: limitCol
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 4

                    RowLayout {
                        spacing: 6
                        StatusBadge { text: "PENDIENTE_SKYRIM"; tone: Theme.warn }
                        Text {
                            text: qsTr("Limitación explícita del motor de renderizado (Qt Quick 3D vs Skyrim SE/AE)")
                            color: Theme.text
                            font.bold: true
                            font.pixelSize: Theme.small
                        }
                    }

                    Repeater {
                        model: root.qcheck.rendererLimitations || []
                        delegate: Text {
                            required property string modelData
                            text: "• " + modelData
                            color: Theme.textDim
                            font.pixelSize: Theme.micro
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }
                }
            }

            // Acciones de auditoría, captura con metadata reproducible e invalidación de caché
            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                Button {
                    text: qsTr("Ejecutar Revisión Visual")
                    highlighted: true
                    onClicked: if (root.lab) root.lab.runQualityCheck()
                }

                Button {
                    text: qsTr("📸 Capturar Visor + Metadata JSON")
                    onClicked: if (root.lab) root.lab.captureViewportWithMetadata(root.viewportTarget, "Captura de revisión visual")
                }

                Button {
                    text: qsTr("Invalidar Cachés 3D/FaceGen")
                    onClicked: if (root.lab) root.lab.invalidateCaches()
                }
            }

            Text {
                visible: Boolean(root.lab && root.lab.lastCaptureReport && root.lab.lastCaptureReport.summary)
                text: (root.lab && root.lab.lastCaptureReport) ? (root.lab.lastCaptureReport.summary || "") : ""
                color: Theme.ok
                font.pixelSize: Theme.micro
                font.family: Theme.mono
                wrapMode: Text.WrapAnywhere
                Layout.fillWidth: true
            }

            // Tarjetas de los 6 cheques técnicos
            Repeater {
                model: root.qcheck.checks || []
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: chkCol.implicitHeight + 14
                    color: Theme.panelAlt
                    radius: Theme.radius
                    border.color: modelData.status === "error" ? Theme.error : (modelData.status === "warning" ? Theme.warn : Theme.borderSubtle)

                    ColumnLayout {
                        id: chkCol
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 4

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            StatusBadge {
                                text: (modelData.status || "ok").toUpperCase()
                                tone: root.statusTone(modelData.status || "ok")
                            }
                            Text {
                                text: modelData.title || ""
                                color: Theme.text
                                font.bold: true
                                font.pixelSize: Theme.small
                                Layout.fillWidth: true
                            }
                            Text {
                                text: modelData.code || ""
                                color: Theme.textDim
                                font.pixelSize: Theme.micro
                                font.family: Theme.mono
                            }
                        }

                        Text {
                            text: modelData.summary || ""
                            color: Theme.textDim
                            font.pixelSize: Theme.micro
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }
                }
            }
        }

        // =====================================================================
        // SECCIÓN 5 — FASE 6: NPC BEAUTY LIBRARY (PERSISTENTE)
        // =====================================================================
        ColumnLayout {
            visible: root.activeSection === 5
            Layout.fillWidth: true
            spacing: 12

            SectionHeader {
                title: qsTr("NPC Beauty Library · Almacenamiento Persistente")
                detail: qsTr("Sobrevive al reinicio de la aplicación")
            }

            // Formulario de guardado rápido
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: saveFormCol.implicitHeight + 16
                color: Theme.panelAlt
                radius: Theme.radius
                border.color: Theme.border

                ColumnLayout {
                    id: saveFormCol
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        TextField {
                            Layout.fillWidth: true
                            placeholderText: qsTr("Nombre del preset / combinación (ej. Serana Cinematográfica)…")
                            text: root.presetNameInput
                            onTextEdited: root.presetNameInput = text
                        }

                        ComboBox {
                            Layout.preferredWidth: 150
                            model: ["Custom", "Vanilla Remastered", "Lore Friendly HD", "Cinematic Fantasy", "Propuesta Pendiente"]
                            onCurrentTextChanged: root.presetCategoryInput = currentText
                        }
                    }

                    TextField {
                        Layout.fillWidth: true
                        placeholderText: qsTr("Notas opcionales sobre compatibilidad o estilo…")
                        text: root.presetNotesInput
                        onTextEdited: root.presetNotesInput = text
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        Button {
                            text: qsTr("💾 Guardar Preset Completo")
                            enabled: root.app.loaded
                            highlighted: true
                            onClicked: {
                                const title = root.presetNameInput.trim() || ((root.studio.outfit.npcLabel || "NPC") + " Preset")
                                root.lab.saveCurrentAsPreset(title, root.presetCategoryInput, root.presetNotesInput, root.viewportTarget)
                            }
                        }

                        Button {
                            text: qsTr("Guardar Cabeza + Ojos")
                            enabled: root.app.loaded
                            onClicked: {
                                const title = root.presetNameInput.trim() || ((root.studio.outfit.npcLabel || "NPC") + " Cabeza+Ojos")
                                root.lab.saveCurrentHeadEyeCombo(title, root.presetNotesInput)
                            }
                        }

                        Button {
                            text: qsTr("Guardar Config. Facial")
                            enabled: root.app.loaded
                            onClicked: {
                                const title = root.presetNameInput.trim() || ((root.studio.outfit.npcLabel || "NPC") + " Morfología")
                                root.lab.saveCurrentFacialConfig(title, root.presetNotesInput)
                            }
                        }

                        Button {
                            text: "↺"
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Recargar biblioteca desde disco")
                            onClicked: if (root.lab) root.lab.reloadLibrary()
                        }
                    }

                    Text {
                        visible: Boolean(root.lab && root.lab.lastLibraryStatus && root.lab.lastLibraryStatus.message)
                        text: (root.lab && root.lab.lastLibraryStatus) ? (root.lab.lastLibraryStatus.message || "") : ""
                        color: (root.lab && root.lab.lastLibraryStatus && root.lab.lastLibraryStatus.ok) ? Theme.ok : Theme.error
                        font.pixelSize: Theme.micro
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }
            }

            // Subpestañas de la biblioteca
            SegmentedControl {
                Layout.fillWidth: true
                stretch: true
                model: [
                    { label: qsTr("Presets (%1)").arg(root.lab ? (root.lab.libraryPresets || []).length : 0) },
                    { label: qsTr("★ Cabellos (%1)").arg(root.lab ? (root.lab.favoriteHairs || []).length : 0) },
                    { label: qsTr("Cabeza+Ojos (%1)").arg(root.lab ? (root.lab.headEyeCombos || []).length : 0) },
                    { label: qsTr("Rostros (%1)").arg(root.lab ? (root.lab.facialConfigs || []).length : 0) },
                    { label: qsTr("Importados (%1)").arg(root.lab ? (root.lab.importedResources || []).length : 0) }
                ]
                currentIndex: root.librarySubTab
                onActivated: index => root.librarySubTab = index
            }

            // Lista unificada según la categoría seleccionada
            Repeater {
                model: {
                    if (!root.lab) return []
                    if (root.librarySubTab === 0) return root.lab.libraryPresets
                    if (root.librarySubTab === 1) return root.lab.favoriteHairs
                    if (root.librarySubTab === 2) return root.lab.headEyeCombos
                    if (root.librarySubTab === 3) return root.lab.facialConfigs
                    return root.lab.importedResources
                }
                delegate: Rectangle {
                    id: libCard
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: libItemCol.implicitHeight + 14
                    color: Theme.panelAlt
                    radius: Theme.radius
                    border.color: Theme.borderSubtle

                    readonly property var liveCompat: (modelData && modelData.liveCompatibility)
                                                      ? modelData.liveCompatibility
                                                      : ({ compatible: true, state: "compatible", reasons: [], warnings: [] })

                    ColumnLayout {
                        id: libItemCol
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 4

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Text {
                                text: modelData.name || modelData.id || ""
                                color: Theme.text
                                font.bold: true
                                font.pixelSize: Theme.small
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }

                            StatusBadge {
                                visible: Boolean(modelData.category)
                                text: modelData.category || ""
                                tone: Theme.accent
                            }

                            StatusBadge {
                                visible: Boolean(modelData.exportState)
                                text: modelData.exportState || ""
                                tone: root.statusTone(modelData.exportState || "")
                            }

                            StatusBadge {
                                text: libCard.liveCompat.state === "incompatible"
                                      ? qsTr("Incompatible")
                                      : (libCard.liveCompat.state === "warning" ? qsTr("Con aviso") : qsTr("Compatible"))
                                tone: root.statusTone(libCard.liveCompat.state || "compatible")
                            }

                            Button {
                                visible: root.librarySubTab < 4
                                text: qsTr("Aplicar")
                                implicitHeight: 24
                                enabled: root.app.loaded && Boolean(libCard.liveCompat.compatible)
                                onClicked: root.lab.applyLibraryEntry(modelData)
                            }

                            Button {
                                text: "✕"
                                implicitWidth: 26
                                implicitHeight: 24
                                onClicked: {
                                    if (root.librarySubTab === 0) root.lab.deletePreset(modelData.id)
                                    else if (root.librarySubTab === 1) root.lab.toggleFavoriteHair(modelData)
                                    else if (root.librarySubTab === 2) root.lab.deleteHeadEyeCombo(modelData.id)
                                    else if (root.librarySubTab === 3) root.lab.deleteFacialConfig(modelData.id)
                                    else root.lab.deleteImportedResource(modelData.id)
                                }
                            }
                        }

                        Text {
                            text: qsTr("Raza: %1 · Sexo: %2 · Creado: %3")
                                  .arg(modelData.raceEditorId || qsTr("Cualquiera"))
                                  .arg(modelData.female === undefined ? qsTr("Cualquiera") : (modelData.female ? qsTr("Mujer") : qsTr("Hombre")))
                                  .arg(modelData.createdAt || "")
                            color: Theme.textDim
                            font.pixelSize: Theme.micro
                            font.family: Theme.mono
                        }

                        Text {
                            visible: (libCard.liveCompat.reasons || []).length > 0
                            text: (libCard.liveCompat.reasons || []).join(" · ")
                            color: Theme.error
                            font.pixelSize: Theme.micro
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }
                }
            }
        }
    }
}
