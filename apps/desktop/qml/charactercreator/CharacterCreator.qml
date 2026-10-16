import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

Item {
    id: root
    required property StudioController studio
    property bool advancedMode: Theme.advancedMode

    ExportWizardDialog {
        id: exportWizard
        studio: root.studio
    }

    SplitView {
        anchors.fill: parent
        orientation: Qt.Horizontal

        // ==========================================
        // PANEL IZQUIERDO: BIBLIOTECA
        // ==========================================
        Rectangle {
            // Gives way on narrow windows so the three panels fit the 1100 px minimum window (280 + 380 + 420).
            SplitView.preferredWidth: Math.max(280, Math.min(380, root.width - 420 - 380 - 16))
            SplitView.minimumWidth: 280
            SplitView.maximumWidth: 500
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
                        studio: root.studio
                    }

                    ArmorCatalog {
                        studio: root.studio
                    }

                    // Panel de donantes / replacers
                    Rectangle {
                        color: Theme.bg
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 8

                            Text {
                                text: qsTr("Donantes de Apariencia (NPC Replacers)")
                                color: Theme.accent
                                font.bold: true
                                font.pixelSize: 12
                            }
                            Text {
                                // applyDonor copies the donor's record values; the FaceGen is rebuilt, never copied.
                                text: qsTr("Aplica el rostro de otro NPC instalado (morphs, Head Parts, tono y tintes de su registro). El FaceGen se regenera a partir de esos datos; no se copia el del donante.")
                                color: Theme.textDim
                                font.pixelSize: 11
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                            Text {
                                visible: root.studio.appearance.donorBlockedReason.length > 0
                                text: qsTr("No disponible para este NPC — %1").arg(root.studio.appearance.donorBlockedReason)
                                color: Theme.warn
                                font.pixelSize: 11
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }

                            TextField {
                                id: donorFilter
                                Layout.fillWidth: true
                                placeholderText: qsTr("Buscar NPC donante…")
                                onTextChanged: donorDebounce.restart()
                                onAccepted: root.studio.searchNpcs(text, "", "", "")
                            }
                            Timer { id: donorDebounce; interval: 250; onTriggered: root.studio.searchNpcs(donorFilter.text, "", "", "") }

                            ListView {
                                id: donorList
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                // The NPC search results (shared with the NPC list); it never had its own model.
                                model: root.studio.npcResults
                                delegate: Rectangle {
                                    id: donorRow
                                    required property string formKey
                                    required property string name
                                    required property string editorId
                                    width: donorList.width
                                    height: 38
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
                                        anchors.margins: 6
                                        spacing: 6
                                        Text {
                                            text: donorRow.name || donorRow.editorId || donorRow.formKey
                                            color: Theme.text
                                            font.pixelSize: 11
                                            elide: Text.ElideRight
                                            Layout.fillWidth: true
                                        }
                                        Button {
                                            text: qsTr("Usar Rostro")
                                            implicitHeight: 24
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
            SplitView.minimumWidth: 380
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
            }

            // Barra inferior de acciones rápidas
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 38
                color: Theme.header
                border.color: Theme.border
                border.width: 1

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    spacing: 8

                    Button {
                        text: qsTr("↶ Deshacer")
                        enabled: root.studio.appearance.canUndo
                        onClicked: root.studio.appearance.undo()
                    }

                    Button {
                        text: qsTr("↷ Rehacer")
                        enabled: root.studio.appearance.canRedo
                        onClicked: root.studio.appearance.redo()
                    }

                    Button {
                        text: qsTr("Restablecer")
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

                    Rectangle { width: 1; height: 20; color: Theme.border }

                    Button {
                        text: qsTr("🚀 Exportar Mod…")
                        highlighted: true
                        onClicked: exportWizard.open()
                    }
                }
            }
        }

        // ==========================================
        // PANEL DERECHO: PERSONALIZACIÓN
        // ==========================================
        Rectangle {
            objectName: "creatorRightPanel"
            SplitView.preferredWidth: 420
            // The customizer pages are laid out for 420 px; narrower, their rows spilled out of the window.
            SplitView.minimumWidth: 420
            SplitView.maximumWidth: 600
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
                    }
                }
            }
        }
    }
}
