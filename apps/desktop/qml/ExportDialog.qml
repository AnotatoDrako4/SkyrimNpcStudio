import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import SkyrimNpcStudio

// "GUARDAR MOD": export the outfit draft as an MO2-installable mod (Mode A, Outfit Patch).
Dialog {
    id: root
    required property StudioController studio
    readonly property OutfitEditor outfit: studio.outfit
    property var exportResult: null
    property string failure: ""
    property var failureIssues: []
    property string installMessage: ""
    property bool installSuccess: false

    modal: true
    anchors.centerIn: Overlay.overlay
    width: 620
    title: standalone.checked ? qsTr("Guardar mod — outfit independiente") : qsTr("Guardar mod — parche de atuendo")
    standardButtons: Dialog.Close

    Settings { id: prefs; category: "export"; property string lastDirectory: "" }

    onAboutToShow: {
        exportResult = null
        failure = ""
        failureIssues = []
        installMessage = ""
        installSuccess = false
        pluginName.text = outfit.suggestedPluginName
        standaloneName.text = outfit.npcLabel.length > 0 ? outfit.npcLabel.split(" (")[0] + " Outfit" : "Outfit personalizado"
        if (outputDir.text.length === 0) outputDir.text = prefs.lastDirectory.length > 0 ? prefs.lastDirectory : outfit.defaultExportDirectory
    }

    Connections {
        target: root.outfit
        function onExported(r) { root.exportResult = r }
        function onExportFailed(message, issues) { root.failure = message; root.failureIssues = issues }
    }

    FolderDialog {
        id: folderDialog
        title: qsTr("Carpeta de destino")
        onAccepted: outputDir.text = decodeURIComponent(selectedFolder.toString().replace(/^file:\/{3}/, ""))
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        CheckBox {
            id: standalone
            text: qsTr("Crear OTFT independiente, sin modificar el NPC")
            checked: false
        }
        TextField {
            id: standaloneName
            visible: standalone.checked
            Layout.fillWidth: true
            placeholderText: qsTr("Nombre del outfit")
        }

        CheckBox {
            id: exportAll
            visible: !standalone.checked && root.outfit.projectNpcCount > 1
            text: qsTr("Exportar todos los NPCs del proyecto (%1 NPCs)").arg(root.outfit.projectNpcCount)
            checked: true
        }

        KeyValue {
            visible: !standalone.checked && (!exportAll.visible || !exportAll.checked)
            label: qsTr("NPC")
            value: root.outfit.npcLabel
        }
        KeyValue {
            visible: !exportAll.visible || !exportAll.checked
            label: qsTr("Piezas")
            value: root.outfit.pieces.map(p => p.name).join(", ")
        }
        KeyValue {
            visible: !standalone.checked && exportAll.visible && exportAll.checked
            label: qsTr("NPCs incluidos")
            value: root.outfit.projectNpcs.map(n => n.name + " (" + n.piecesCount + " piezas)").join(", ")
        }

        Label { text: qsTr("Nombre del plugin") }
        TextField { id: pluginName; Layout.fillWidth: true; validator: RegularExpressionValidator { regularExpression: /[A-Za-z0-9][A-Za-z0-9 _\-\.\(\)\[\]']{0,95}\.esp/ } }

        Label { text: qsTr("Carpeta de destino (se crea una subcarpeta con el nombre del mod)") }
        RowLayout {
            Layout.fillWidth: true
            TextField { id: outputDir; Layout.fillWidth: true }
            Button { text: qsTr("Elegir…"); onClicked: folderDialog.open() }
        }

        CheckBox { id: esl; text: qsTr("Marcar como ESL (ESP-FE, no ocupa índice de carga completo)"); checked: true }
        CheckBox { id: zip; text: qsTr("Crear ZIP instalable en MO2"); checked: true }
        CheckBox { id: sleepOutfit; visible: !standalone.checked; text: qsTr("Asignar también como atuendo para dormir (SOFT)"); checked: false }
        TextField { id: description; Layout.fillWidth: true; placeholderText: qsTr("Descripción (opcional)") }

        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.textDim
            font.pixelSize: Theme.small
            text: qsTr("Se escribe primero en una carpeta temporal, se valida releyendo el ESP y solo entonces se mueve al destino. "
                       + "Nunca se sobrescriben archivos existentes ni se modifican plugins de otros mods.")
        }

        Button {
            text: root.outfit.exporting ? qsTr("Exportando…") : qsTr("Exportar")
            highlighted: true
            enabled: !root.outfit.exporting && pluginName.acceptableInput && outputDir.text.length > 0 && root.outfit.exportable
                     && (!standalone.checked || standaloneName.text.trim().length > 0)
            Layout.alignment: Qt.AlignRight
            onClicked: {
                root.exportResult = null
                root.failure = ""
                prefs.lastDirectory = outputDir.text
                root.outfit.exportPatch(pluginName.text, outputDir.text, esl.checked, zip.checked, description.text,
                                        sleepOutfit.checked, exportAll.visible ? exportAll.checked : false,
                                        standalone.checked, standaloneName.text)
            }
        }

        ColumnLayout {
            visible: root.exportResult !== null
            Layout.fillWidth: true
            Text { text: qsTr("✔ Mod generado y verificado"); color: Theme.ok; font.bold: true }
            KeyValue { label: qsTr("Plugin"); value: root.exportResult?.pluginPath ?? ""; mono: true }
            KeyValue { label: qsTr("ZIP"); value: root.exportResult?.zipPath ?? qsTr("(no solicitado)"); mono: true }
            KeyValue { label: qsTr("Masters"); value: (root.exportResult?.masters ?? []).join(", ") }
            KeyValue { label: qsTr("SHA-256"); value: root.exportResult?.pluginSha256 ?? ""; mono: true }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.warn
                font.pixelSize: Theme.small
                text: qsTr("Pendiente: instalar el mod en MO2 y comprobarlo dentro del juego. "
                           + "El estudio no marca el cambio como verificado en el juego.")
            }
            RowLayout {
                spacing: 8
                Button { text: qsTr("Abrir carpeta"); onClicked: root.studio.openFolder(root.exportResult.modDirectory) }
                Button {
                    text: qsTr("Instalar en MO2 (mods/)…")
                    highlighted: true
                    enabled: root.studio.mo2ModsDirectory.length > 0 && !root.installSuccess
                    onClicked: installConfirmDialog.open()
                }
            }
            Text {
                visible: root.installMessage.length > 0
                text: root.installMessage
                color: root.installSuccess ? Theme.ok : Theme.error
                wrapMode: Text.Wrap
                font.pixelSize: Theme.small
                Layout.fillWidth: true
            }
        }

        ColumnLayout {
            visible: root.failure.length > 0
            Layout.fillWidth: true
            Text { text: qsTr("✖ ") + root.failure; color: Theme.error; wrapMode: Text.Wrap; Layout.fillWidth: true }
            IssueList { issues: root.failureIssues; emptyText: "" }
        }
    }

    Dialog {
        id: installConfirmDialog
        title: qsTr("Instalación asistida en MO2")
        modal: true
        anchors.centerIn: Overlay.overlay
        width: 480
        standardButtons: Dialog.Ok | Dialog.Cancel
        ColumnLayout {
            spacing: 8
            width: parent.width
            Text {
                text: qsTr("¿Instalar el mod generado directamente en la carpeta mods/ de Mod Organizer 2?")
                color: Theme.text
                font.bold: true
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Text {
                text: qsTr("Destino: ") + root.studio.mo2ModsDirectory
                color: Theme.textDim
                font.pixelSize: Theme.small
                font.family: Theme.mono
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Text {
                text: qsTr("Importante: Mod Organizer 2 debe estar cerrado antes de confirmar.")
                color: Theme.warn
                font.pixelSize: Theme.small
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }
        onAccepted: {
            const res = root.studio.installModToMo2(root.exportResult.modDirectory)
            if (res.success) {
                root.installSuccess = true
                root.installMessage = qsTr("✔ Mod instalado en MO2: ") + res.destination + qsTr(" (actívalo en MO2 al volver a abrirlo)")
            } else {
                root.installSuccess = false
                root.installMessage = qsTr("✖ ") + res.error
            }
        }
    }
}
