import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

Item {
    id: root
    required property StudioController studio
    property bool advancedMode: Theme.advancedMode

    // Explorer on screen (the user asked for it), on the NPC search or on the armor catalog, search focused.
    // Used by the empty viewer and by the command palette.
    function searchNpc() {
        creatorSplit.layout.maximized = false
        creatorSplit.layout.leftVisible = true
        leftTabs.currentIndex = 0
        creatorNpcBrowser.focusSearch()
    }
    function showCatalog() {
        creatorSplit.layout.maximized = false
        creatorSplit.layout.leftVisible = true
        leftTabs.currentIndex = 1
        creatorArmorCatalog.focusSearch()
    }
    function showResources() {
        creatorSplit.layout.maximized = false
        creatorSplit.layout.leftVisible = true
        leftTabs.currentIndex = 2
        creatorArmorCatalog.showResources()
    }

    // Pestaña estilizada según el sistema de diseño Stitch MCP (fondo grafito, barra inferior azul acero #4C8DFF e icono vectorial).
    component DockTabButton: TabButton {
        id: dockTab
        property string glyph: ""
        property string shortText: ""
        implicitHeight: Theme.controlHeight + Theme.spacingXs
        leftPadding: Theme.sp(4)
        rightPadding: Theme.sp(4)
        Accessible.name: dockTab.text
        ToolTip.visible: hovered
        ToolTip.delay: 450
        ToolTip.text: dockTab.text
        contentItem: RowLayout {
            spacing: Theme.sp(3)
            Icon {
                visible: dockTab.glyph.length > 0
                name: dockTab.glyph
                size: Theme.small
                color: dockTab.checked ? Theme.accent : Theme.textDim
                Layout.alignment: Qt.AlignVCenter
            }
            Text {
                visible: dockTab.width >= Theme.px(42)
                text: (dockTab.width < Theme.px(74) && dockTab.shortText.length > 0) ? dockTab.shortText : dockTab.text
                color: dockTab.checked ? Theme.text : Theme.textDim
                font.family: Theme.fontFamily
                font.pixelSize: Theme.small
                font.bold: dockTab.checked
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                Layout.fillWidth: true
            }
        }
        background: Rectangle {
            color: dockTab.checked ? Theme.panelAlt : (dockTab.down ? Theme.accentDim : (dockTab.hovered ? Theme.accentSubtle : Theme.header))
            border.width: dockTab.visualFocus ? Theme.focusWidth : 1
            border.color: dockTab.visualFocus ? Theme.borderFocus : Theme.borderSubtle
            Rectangle {
                visible: dockTab.checked
                anchors.bottom: parent.bottom
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.max(Theme.px(16), parent.width - Theme.sp(12))
                height: 2
                radius: 1
                color: Theme.accent
            }
        }
    }

    ExportWizardDialog {
        id: exportWizard
        studio: root.studio
    }

    WorkbenchSplit {
        id: creatorSplit
        objectName: "creatorSplit"
        anchors.fill: parent
        layout: Workbench.layout("creator")
        leftPane: creatorLeftPanel
        rightPane: creatorRightPanel
        leftMinimum: 220
        // The customizer pages scroll and elide; below ~340 px their rows start to crowd.
        rightMinimum: 340
        centerMinimum: 380

        // ==========================================
        // PANEL IZQUIERDO: BIBLIOTECA
        // ==========================================
        Rectangle {
            id: creatorLeftPanel
            objectName: "creatorLeftPanel"
            color: Theme.panel

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                TabBar {
                    id: leftTabs
                    Layout.fillWidth: true
                    onCurrentIndexChanged: {
                        if (currentIndex === 1) creatorArmorCatalog.modeIndex = 0
                        else if (currentIndex === 2) creatorArmorCatalog.modeIndex = 1
                    }
                    DockTabButton { text: qsTr("NPCs"); glyph: "person" }
                    DockTabButton { text: qsTr("Prendas"); glyph: "tag" }
                    DockTabButton { text: qsTr("Recursos"); glyph: "library" }
                    DockTabButton { text: qsTr("Donantes"); glyph: "people" }
                }

                StackLayout {
                    currentIndex: leftTabs.currentIndex === 2 ? 1 : (leftTabs.currentIndex === 3 ? 2 : leftTabs.currentIndex)
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    NpcBrowser {
                        id: creatorNpcBrowser
                        objectName: "creatorNpcBrowser"
                        studio: root.studio
                        onEditRequested: rightTabs.currentIndex = 1  // Rostro
                    }

                    ArmorCatalog {
                        id: creatorArmorCatalog
                        studio: root.studio
                        onPreviewRequested: (formKey) => {
                            root.studio.armorPreview.selectArmor(formKey)
                        }
                        onMeshPreviewRequested: (dataRelativePath) => {
                            root.studio.armorPreview.selectMesh(dataRelativePath)
                        }
                    }

                    // Panel de donantes / replacers
                    Rectangle {
                        color: Theme.bg
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: Theme.spacingLg
                            spacing: Theme.spacingMd

                            Text {
                                text: qsTr("Donantes de Apariencia (NPC Replacers)")
                                color: Theme.accent
                                font.bold: true
                                font.pixelSize: Theme.normal
                            }
                            Text {
                                // applyDonor copies the donor's record values; the FaceGen is rebuilt, never copied.
                                text: qsTr("Aplica el rostro de otro NPC instalado (morphs, Head Parts, tono y tintes de su registro). El FaceGen se regenera a partir de esos datos; no se copia el del donante.")
                                color: Theme.textDim
                                font.pixelSize: Theme.small
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                            Text {
                                visible: root.studio.appearance.donorBlockedReason.length > 0
                                text: qsTr("No disponible para este NPC — %1").arg(root.studio.appearance.donorBlockedReason)
                                color: Theme.warn
                                font.pixelSize: Theme.small
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }

                            TextField {
                                id: donorFilter
                                Layout.fillWidth: true
                                placeholderText: qsTr("Buscar NPC donante…")
                                onTextChanged: donorDebounce.restart()
                                onAccepted: root.studio.searchDonors(text)
                            }
                            Timer { id: donorDebounce; interval: 250; onTriggered: root.studio.searchDonors(donorFilter.text) }

                            ListView {
                                id: donorList
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                // Donor search results: a model of its own, so the NPC browser keeps its rows.
                                model: root.studio.donorResults
                                delegate: Rectangle {
                                    id: donorRow
                                    required property string formKey
                                    required property string name
                                    required property string editorId
                                    width: donorList.width
                                    height: Theme.rowHeight + Theme.spacingSm
                                    color: donorMouse.containsMouse ? Theme.selection : "transparent"
                                    border.color: Theme.border
                                    border.width: 1
                                    radius: 3

                                    MouseArea {
                                        id: donorMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                    }
                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.margins: Theme.spacingSm
                                        spacing: Theme.spacingSm
                                        Text {
                                            text: donorRow.name || donorRow.editorId || donorRow.formKey
                                            color: Theme.text
                                            font.pixelSize: Theme.small
                                            elide: Text.ElideRight
                                            Layout.fillWidth: true
                                        }
                                        Button {
                                            text: qsTr("Usar Rostro")
                                            implicitHeight: Theme.controlHeight - Theme.spacingXs
                                            enabled: root.studio.appearance.donorBlockedReason.length === 0
                                            onClicked: root.studio.appearance.applyDonor(donorRow.formKey, donorRow.name || donorRow.editorId)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // ==========================================
        // PANEL CENTRAL: PERSONAJE 3D + TOOLBAR
        // ==========================================
        ColumnLayout {
            SplitView.fillWidth: true
            spacing: 0

            CharacterToolbar {
                id: charToolbar
                Layout.fillWidth: true
                studio: root.studio
                viewport: charViewport
                activeStep: leftTabs.currentIndex === 0 && rightTabs.currentIndex === 0 ? 0 : rightTabs.currentIndex
                onStepActivated: (stepIndex) => {
                    if (stepIndex === 0) {
                        root.searchNpc()
                    } else if (stepIndex === 1) {
                        creatorSplit.layout.rightVisible = true
                        rightTabs.currentIndex = 1
                        charViewport.applyCameraPreset(4)
                    } else if (stepIndex === 2) {
                        creatorSplit.layout.rightVisible = true
                        rightTabs.currentIndex = 2
                        charViewport.applyCameraPreset(4)
                    } else if (stepIndex === 3) {
                        creatorSplit.layout.rightVisible = true
                        rightTabs.currentIndex = 3
                        charViewport.applyCameraPreset(0)
                    } else if (stepIndex === 4 || stepIndex === 5) {
                        creatorSplit.layout.rightVisible = true
                        rightTabs.currentIndex = 4
                        if (stepIndex === 4) root.showCatalog()
                        charViewport.applyCameraPreset(0)
                    } else if (stepIndex === 6) {
                        exportWizard.open()
                    }
                }
            }

            CharacterViewport {
                id: charViewport
                Layout.fillWidth: true
                Layout.fillHeight: true
                studio: root.studio
                sceneOverride: ((leftTabs.currentIndex === 1 || leftTabs.currentIndex === 2) && (root.studio.armorPreview.selectedArmor !== "" || root.studio.armorPreview.selectedMesh !== "")) ? root.studio.armorPreview : null
                previewMode: (leftTabs.currentIndex === 1 || leftTabs.currentIndex === 2) && (root.studio.armorPreview.selectedArmor !== "" || root.studio.armorPreview.selectedMesh !== "")
                // The empty viewer's actions: the explorer comes back (a user action) on the right tab, search focused.
                onNpcSearchRequested: root.searchNpc()
                onCatalogRequested: root.showCatalog()
            }

            // Barra inferior de acciones rápidas
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: Theme.controlHeight + Theme.spacingLg
                color: Theme.header
                border.color: Theme.border
                border.width: 1

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacingLg
                    anchors.rightMargin: Theme.spacingLg
                    spacing: Theme.spacingMd

                    Button {
                        text: qsTr("↶ Deshacer")
                        Accessible.name: qsTr("Deshacer")
                        Layout.fillWidth: true
                        Layout.maximumWidth: implicitWidth
                        Layout.minimumWidth: Math.min(implicitWidth, Theme.px(44))
                        enabled: root.studio.appearance.canUndo
                        onClicked: root.studio.appearance.undo()
                    }

                    Button {
                        text: qsTr("↷ Rehacer")
                        Accessible.name: qsTr("Rehacer")
                        Layout.fillWidth: true
                        Layout.maximumWidth: implicitWidth
                        Layout.minimumWidth: Math.min(implicitWidth, Theme.px(44))
                        enabled: root.studio.appearance.canRedo
                        onClicked: root.studio.appearance.redo()
                    }

                    Button {
                        text: qsTr("Restablecer")
                        Layout.fillWidth: true
                        Layout.maximumWidth: implicitWidth
                        Layout.minimumWidth: Math.min(implicitWidth, Theme.px(44))
                        onClicked: root.studio.appearance.resetFace()
                    }

                    Button {
                        visible: !creatorSplit.layout.rightVisible
                        text: qsTr("⇤ Mostrar Personalización")
                        Accessible.name: qsTr("Mostrar panel derecho de personalización")
                        font.pixelSize: Theme.small
                        highlighted: true
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Volver a mostrar el panel derecho de personalización (Ctrl+Alt+B)")
                        onClicked: creatorSplit.layout.rightVisible = true
                    }

                    Item { Layout.fillWidth: true }

                    // Selector Modo Fácil vs Modo Avanzado
                    Switch {
                        id: modeSwitch
                        text: Theme.advancedMode ? qsTr("Modo Avanzado") : qsTr("Modo Fácil")
                        checked: Theme.advancedMode
                        onToggled: Theme.advancedMode = checked
                    }

                    // Exporting lives in the window header ("Exportar Mod…") and the Exportar menu.
                }
            }
        }

        // ==========================================
        // PANEL DERECHO: PERSONALIZACIÓN
        // ==========================================
        Rectangle {
            id: creatorRightPanel
            objectName: "creatorRightPanel"
            color: Theme.panel

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // Barra superior de ajuste y personalización de ancho del panel
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: Theme.px(28)
                    color: Theme.header
                    border.color: Theme.borderSubtle
                    border.width: 1

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spacingSm
                        anchors.rightMargin: Theme.spacingXs
                        spacing: Theme.spacingXs

                        Icon {
                            name: "settings"
                            size: Theme.micro
                            color: Theme.accent
                        }

                        Text {
                            text: qsTr("Personalización")
                            color: Theme.text
                            font.bold: true
                            font.pixelSize: Theme.micro
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }

                        // Controles de Ancho / Auto-ajuste
                        RowLayout {
                            spacing: 2

                            Button {
                                text: qsTr("↔ Auto")
                                Accessible.name: qsTr("Auto-ajustar ancho del panel derecho")
                                implicitHeight: Theme.px(22)
                                implicitWidth: Theme.px(54)
                                font.pixelSize: Theme.micro
                                highlighted: Math.abs((creatorSplit.layout.rightWidth || 0) - 480) < 15
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("Auto-ajustar al ancho óptimo (480 px) para que todas las pestañas y controles quepan cómodamente sin recortes")
                                onClicked: {
                                    creatorSplit.layout.rightVisible = true
                                    creatorSplit.layout.rightWidth = (creatorSplit.layout.rightWidth >= 480) ? 360 : 480
                                }
                            }

                            Button {
                                text: qsTr("⇱ 340")
                                Accessible.name: qsTr("Ajustar panel a ancho compacto de 340 píxeles")
                                implicitHeight: Theme.px(22)
                                implicitWidth: Theme.px(44)
                                font.pixelSize: Theme.micro
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("Modo compacto (340 px) para dar máxima prioridad al visor 3D")
                                onClicked: {
                                    creatorSplit.layout.rightVisible = true
                                    creatorSplit.layout.rightWidth = 340
                                }
                            }

                            Button {
                                text: qsTr("⬌ 460")
                                Accessible.name: qsTr("Ajustar panel a ancho cómodo de 460 píxeles")
                                implicitHeight: Theme.px(22)
                                implicitWidth: Theme.px(44)
                                font.pixelSize: Theme.micro
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("Modo estándar cómodo (460 px) con títulos completos de pestañas")
                                onClicked: {
                                    creatorSplit.layout.rightVisible = true
                                    creatorSplit.layout.rightWidth = 460
                                }
                            }

                            Button {
                                text: qsTr("⇲ 600")
                                Accessible.name: qsTr("Ajustar panel a ancho amplio de 600 píxeles")
                                implicitHeight: Theme.px(22)
                                implicitWidth: Theme.px(44)
                                font.pixelSize: Theme.micro
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("Modo amplio extendido (600 px) para edición detallada de controles")
                                onClicked: {
                                    creatorSplit.layout.rightVisible = true
                                    creatorSplit.layout.rightWidth = 600
                                }
                            }

                            ToolButton {
                                text: "⇥"
                                Accessible.name: qsTr("Ocultar panel derecho")
                                implicitHeight: Theme.px(22)
                                implicitWidth: Theme.px(24)
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("Ocultar panel derecho (Ctrl+Alt+B)")
                                onClicked: creatorSplit.layout.rightVisible = false
                            }
                        }
                    }
                }

                TabBar {
                    id: rightTabs
                    objectName: "creatorRightTabs"
                    Layout.fillWidth: true
                    DockTabButton { text: qsTr("Presets"); shortText: qsTr("Presets"); glyph: "document" }
                    DockTabButton { text: qsTr("Rostro"); shortText: qsTr("Rostro"); glyph: "person" }
                    DockTabButton { text: qsTr("Pelo/Ojos"); shortText: qsTr("Pelo"); glyph: "eye" }
                    DockTabButton { text: qsTr("Cuerpo"); shortText: qsTr("Cuerpo"); glyph: "people" }
                    DockTabButton { text: qsTr("Vestuario"); shortText: qsTr("Ropa"); glyph: "tag" }
                    DockTabButton { objectName: "creatorOriginTab"; text: qsTr("Origen"); shortText: qsTr("Origen"); glyph: "diagnostic" }
                }

                StackLayout {
                    currentIndex: rightTabs.currentIndex
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    // Pestaña 1: Presets
                    PresetManager {
                        studio: root.studio
                    }

                    // Pestaña 2: Rostro
                    FaceCustomizer {
                        studio: root.studio
                    }

                    // Pestaña 3: Cabello y Ojos
                    HairEyeCustomizer {
                        studio: root.studio
                    }

                    // Pestaña 4: Cuerpo y Piel
                    BodyCustomizer {
                        studio: root.studio
                    }

                    // Pestaña 5: Vestuario
                    OutfitPanel {
                        studio: root.studio
                        onExportRequested: exportWizard.open()
                    }

                    // Pestaña 6: Origen (qué mod controla cada parte y conflictos de FaceGen)
                    AppearanceControlPanel {
                        studio: root.studio
                        onResolutionRequested: exportWizard.open()
                    }
                }
            }
        }
    }
}
