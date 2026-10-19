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
        width: Theme.px(480)
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
                    font.pixelSize: Theme.small
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
                font.pixelSize: Theme.large
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
            font.pixelSize: Theme.small
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
                    font.pixelSize: Theme.small
                }
            }

            delegate: Rectangle {
                width: ListView.view.width
                height: 42
                color: index % 2 === 0 ? "transparent" : Theme.panelAlt
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
                            font.pixelSize: Theme.normal
                            elide: Text.ElideRight
                        }
                        Text {
                            text: qsTr("Mod: ") + (modelData.sourceMod || qsTr("Desconocido")) + " · " + modelData.fileName
                            color: Theme.textDim
                            font.pixelSize: Theme.micro
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
            font.pixelSize: Theme.large
        }

        Text {
            text: qsTr("Aplica el rostro de otro NPC o replacer instalado (morphs, Head Parts, tono y tintes de su registro). El FaceGen se regenera; no se copia el del donante.")
            color: Theme.textDim
            font.pixelSize: Theme.small
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
                onAccepted: root.studio.searchDonors(text)
            }
            Button {
                text: qsTr("Buscar")
                onClicked: root.studio.searchDonors(donorSearchField.text)
            }
        }

        // List or face gallery (remembered: Configuración › NPC y retratos). The gallery shows each donor's
        // studio portrait; ones rendered in earlier sessions appear at once from the local cache.
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Button {
                objectName: "donorListModeButton"
                Layout.fillWidth: true
                text: qsTr("Lista")
                checkable: true
                checked: AppSettings.donorView === "list"
                onClicked: AppSettings.donorView = "list"
            }
            Button {
                objectName: "donorGalleryModeButton"
                Layout.fillWidth: true
                text: qsTr("Galería de rostros")
                checkable: true
                checked: AppSettings.donorView === "gallery"
                onClicked: AppSettings.donorView = "gallery"
            }
        }
        Text {
            Layout.fillWidth: true
            visible: root.studio.donorResults.count === 0
            text: qsTr("Busca un NPC por nombre para ver sus rostros.")
            color: Theme.textMuted
            font.pixelSize: Theme.small
            wrapMode: Text.WordWrap
        }

        GridView {
            id: donorGallery
            objectName: "donorGallery"
            visible: AppSettings.donorView === "gallery" && count > 0
            Layout.fillWidth: true
            implicitHeight: Math.min(cellHeight * 2.6, Math.ceil(count / columns) * cellHeight)
            clip: true
            model: AppSettings.donorView === "gallery" ? root.studio.donorResults : null
            boundsBehavior: Flickable.StopAtBounds
            readonly property int columns: Math.max(1, Math.floor(width / AppSettings.galleryCardWidth))
            cellWidth: Math.floor(width / columns)
            cellHeight: cellWidth - 10 + 74
            cacheBuffer: cellHeight
            ScrollBar.vertical: ScrollBar {}

            delegate: Item {
                id: donorCard
                required property string formKey
                required property string name
                required property string editorId
                required property string race
                required property string sex
                required property string winningPlugin
                width: donorGallery.cellWidth
                height: donorGallery.cellHeight
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 4
                    radius: 4
                    color: donorHover.hovered ? Theme.panelAlt : Theme.panel
                    border.color: Theme.border
                    HoverHandler { id: donorHover }
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 5
                        spacing: 1
                        PortraitThumb {
                            Layout.fillWidth: true
                            Layout.preferredHeight: width
                            portraits: root.studio.portraits
                            formKey: donorCard.formKey
                        }
                        Text {
                            Layout.fillWidth: true
                            text: donorCard.name || donorCard.editorId || donorCard.formKey
                            color: Theme.text
                            font.bold: true
                            font.pixelSize: Theme.small
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: (donorCard.race || "?") + " · " + (donorCard.sex === "female" ? "♀" : "♂") + " · " + donorCard.winningPlugin
                            color: Theme.textDim
                            font.pixelSize: Theme.micro
                            elide: Text.ElideRight
                        }
                        Button {
                            Layout.fillWidth: true
                            implicitHeight: Theme.controlHeight - 4
                            text: qsTr("Usar")
                            font.pixelSize: Theme.small
                            enabled: root.studio.appearance.donorBlockedReason.length === 0
                            ToolTip.visible: hovered
                            ToolTip.text: enabled ? qsTr("Aplicar el rostro de %1 (se regenera el FaceGen; no se copia el del donante)").arg(donorCard.name || donorCard.editorId)
                                                  : root.studio.appearance.donorBlockedReason
                            onClicked: root.studio.appearance.applyDonor(donorCard.formKey, donorCard.name || donorCard.editorId)
                        }
                    }
                }
            }
        }

        ListView {
            visible: AppSettings.donorView === "list"
            Layout.fillWidth: true
            implicitHeight: Math.min(200, count * 40)
            clip: true
            model: root.studio.donorResults
            boundsBehavior: Flickable.StopAtBounds

            delegate: Rectangle {
                width: ListView.view.width
                height: 38
                color: index % 2 === 0 ? "transparent" : Theme.panelAlt
                border.color: Theme.border

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 6
                    spacing: 8

                    ColumnLayout {
                        // Long names/plugins elide instead of pushing the button out of the panel.
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        spacing: 2
                        Text {
                            Layout.fillWidth: true
                            text: model.name || model.editorId || model.formKey
                            color: Theme.text
                            font.bold: true
                            font.pixelSize: Theme.small
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: (model.race || "") + " · " + (model.sex || "") + " · " + (model.winningPlugin || "")
                            color: Theme.textDim
                            font.pixelSize: Theme.micro
                            elide: Text.ElideRight
                        }
                    }

                    Button {
                        text: qsTr("Usar como Donante")
                        enabled: root.studio.appearance.donorBlockedReason.length === 0
                        ToolTip.visible: hovered && !enabled
                        ToolTip.text: root.studio.appearance.donorBlockedReason
                        onClicked: {
                            root.studio.appearance.applyDonor(model.formKey, model.name || model.editorId)
                        }
                    }
                }
            }
        }
    }
}
