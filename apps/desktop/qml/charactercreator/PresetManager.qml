import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import SkyrimNpcStudio

ScrollView {
    id: root
    required property StudioController studio
    contentWidth: availableWidth
    clip: true

    FileDialog {
        id: jslotFileDialog
        title: qsTr("Seleccionar Preset RaceMenu (.jslot)")
        nameFilters: ["RaceMenu Presets (*.jslot)"]
        onAccepted: {
            var path = selectedFile.toString().replace(/^file:\/\/\//, "")
            var comp = root.studio.appearance.evaluatePresetCompatibility(path)
            if (!comp.compatible) {
                compatDialog.issues = comp.issues
                compatDialog.pendingPath = path
                compatDialog.open()
            } else {
                root.studio.appearance.applyJslotPreset(path)
            }
        }
    }

    Dialog {
        id: compatDialog
        title: qsTr("Informe de Compatibilidad de Preset")
        modal: true
        anchors.centerIn: Overlay.overlay
        width: 480
        property var issues: []
        property string pendingPath: ""
        standardButtons: Dialog.Ok | Dialog.Cancel

        ColumnLayout {
            spacing: 8
            width: parent.width
            Text {
                text: qsTr("Se detectaron advertencias o incompatibilidades en el preset:")
                color: Theme.text
                font.bold: true
            }
            Repeater {
                model: compatDialog.issues
                delegate: Text {
                    text: "• " + modelData
                    color: Theme.warning
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    font.pixelSize: 11
                }
            }
            Text {
                text: qsTr("¿Deseas aplicar el preset de todos modos?")
                color: Theme.textDim
                Layout.topMargin: 6
            }
        }
        onAccepted: {
            if (pendingPath.length > 0) root.studio.appearance.applyJslotPreset(pendingPath)
        }
    }

    ColumnLayout {
        width: root.availableWidth
        spacing: 14

        // Section: Presets RaceMenu
        RowLayout {
            Layout.fillWidth: true
            Text {
                text: qsTr("Presets de RaceMenu (.jslot)")
                color: Theme.text
                font.bold: true
                font.pixelSize: 14
            }
            Item { Layout.fillWidth: true }
            Button {
                text: qsTr("Escanear VFS")
                onClicked: root.studio.appearance.loadPresetsFromVfs()
            }
            Button {
                text: qsTr("Importar Archivo…")
                onClicked: jslotFileDialog.open()
            }
        }

        Text {
            text: qsTr("Presets detectados en mods instalados (SKSE\\Plugins\\CharGen\\Presets) o importados.")
            color: Theme.textDim
            font.pixelSize: 11
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        ListView {
            Layout.fillWidth: true
            implicitHeight: Math.min(220, count * 44 + 4)
            clip: true
            model: root.studio.appearance.presetsList
            boundsBehavior: Flickable.StopAtBounds

            Rectangle {
                anchors.fill: parent
                color: Theme.panel
                border.color: Theme.border
                radius: 4
                visible: parent.count === 0
                Text {
                    anchors.centerIn: parent
                    text: qsTr("No hay presets .jslot detectados. Haz clic en 'Escanear VFS' o 'Importar Archivo'.")
                    color: Theme.textDim
                    font.pixelSize: 11
                }
            }

            delegate: Rectangle {
                width: ListView.view.width
                height: 42
                color: index % 2 === 0 ? "transparent" : Theme.rowAlt
                border.color: Theme.border

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 6
                    spacing: 8

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            text: modelData.name
                            color: Theme.text
                            font.bold: true
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                        Text {
                            text: qsTr("Mod: ") + (modelData.sourceMod || qsTr("Desconocido")) + " · " + modelData.fileName
                            color: Theme.textDim
                            font.pixelSize: 10
                            elide: Text.ElideRight
                        }
                    }

                    Button {
                        text: qsTr("Aplicar")
                        onClicked: {
                            var p = modelData.diskPath
                            var comp = root.studio.appearance.evaluatePresetCompatibility(p)
                            if (!comp.compatible) {
                                compatDialog.issues = comp.issues
                                compatDialog.pendingPath = p
                                compatDialog.open()
                            } else {
                                root.studio.appearance.applyJslotPreset(p)
                            }
                        }
                    }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

        // Section: Donor NPC Replacer
        Text {
            text: qsTr("Transferencia de Apariencia (NPC Donante / Replacer)")
            color: Theme.text
            font.bold: true
            font.pixelSize: 14
        }

        Text {
            text: qsTr("Aplica la apariencia facial, FaceGen y Head Parts de otro NPC o replacer instalado en tu perfil.")
            color: Theme.textDim
            font.pixelSize: 11
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            TextField {
                id: donorSearchField
                Layout.fillWidth: true
                placeholderText: qsTr("Buscar NPC donante por nombre...")
                onAccepted: root.studio.searchNpcs(text, "", "", "")
            }
            Button {
                text: qsTr("Buscar")
                onClicked: root.studio.searchNpcs(donorSearchField.text, "", "", "")
            }
        }

        ListView {
            Layout.fillWidth: true
            implicitHeight: Math.min(200, count * 40)
            clip: true
            model: root.studio.npcResults
            boundsBehavior: Flickable.StopAtBounds

            delegate: Rectangle {
                width: ListView.view.width
                height: 38
                color: index % 2 === 0 ? "transparent" : Theme.rowAlt
                border.color: Theme.border

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 6
                    spacing: 8

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            text: model.name || model.editorId || model.formKey
                            color: Theme.text
                            font.bold: true
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                        Text {
                            text: (model.race || "") + " · " + (model.sex || "") + " · " + (model.winningPlugin || "")
                            color: Theme.textDim
                            font.pixelSize: 10
                            elide: Text.ElideRight
                        }
                    }

                    Button {
                        text: qsTr("Usar como Donante")
                        onClicked: {
                            root.studio.appearance.applyDonor(model.formKey, model.name || model.editorId)
                        }
                    }
                }
            }
        }
    }
}
