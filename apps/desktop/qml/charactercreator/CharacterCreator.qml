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
                    TabButton { text: qsTr("NPCs") }
                    TabButton { text: qsTr("Prendas") }
                    TabButton { text: qsTr("Donantes") }
                }

                StackLayout {
                    currentIndex: leftTabs.currentIndex
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
            }

            CharacterViewport {
                id: charViewport
                Layout.fillWidth: true
                Layout.fillHeight: true
                studio: root.studio
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

                TabBar {
                    id: rightTabs
                    objectName: "creatorRightTabs"
                    Layout.fillWidth: true
                    TabButton { text: qsTr("Presets") }
                    TabButton { text: qsTr("Rostro") }
                    TabButton { text: qsTr("Pelo/Ojos") }
                    TabButton { text: qsTr("Cuerpo") }
                    TabButton { text: qsTr("Vestuario") }
                    TabButton { objectName: "creatorOriginTab"; text: qsTr("Origen") }
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
