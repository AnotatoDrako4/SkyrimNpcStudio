import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

Item {
    id: root
    required property StudioController studio
    property bool advancedMode: false

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
            SplitView.preferredWidth: 380
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
                                text: qsTr("Transfiere el rostro, FaceGen y HeadParts de cualquier NPC instalado al personaje actual:")
                                color: Theme.textDim
                                font.pixelSize: 11
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }

                            TextField {
                                id: donorFilter
                                Layout.fillWidth: true
                                placeholderText: qsTr("Buscar NPC donante…")
                            }

                            ListView {
                                id: donorList
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                model: root.studio.npcs
                                delegate: Rectangle {
                                    width: donorList.width
                                    height: 38
                                    color: donorMouse.containsMouse ? Theme.selection : "transparent"
                                    border.color: Theme.border
                                    border.width: 1
                                    radius: 3
                                    visible: donorFilter.text.length === 0 || (modelData.name && modelData.name.toLowerCase().includes(donorFilter.text.toLowerCase()))

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.margins: 6
                                        spacing: 6
                                        Text {
                                            text: modelData.name || modelData.formKey
                                            color: Theme.text
                                            font.pixelSize: 11
                                            elide: Text.ElideRight
                                            Layout.fillWidth: true
                                        }
                                        Button {
                                            text: qsTr("Usar Rostro")
                                            implicitHeight: 24
                                            onClicked: root.studio.appearance.applyDonor(modelData.formKey, modelData.name)
                                        }
                                    }
                                    MouseArea {
                                        id: donorMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
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
            SplitView.minimumWidth: 400
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
                        text: root.advancedMode ? qsTr("Modo Avanzado") : qsTr("Modo Fácil")
                        checked: root.advancedMode
                        onToggled: root.advancedMode = checked
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
            SplitView.preferredWidth: 420
            SplitView.minimumWidth: 320
            SplitView.maximumWidth: 600
            color: Theme.panel

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                TabBar {
                    id: rightTabs
                    Layout.fillWidth: true
                    TabButton { text: qsTr("Presets") }
                    TabButton { text: qsTr("Rostro") }
                    TabButton { text: qsTr("Pelo/Ojos") }
                    TabButton { text: qsTr("Cuerpo") }
                    TabButton { text: qsTr("Vestuario") }
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
                }
            }
        }
    }
}
