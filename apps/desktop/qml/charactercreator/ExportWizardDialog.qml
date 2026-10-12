import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import SkyrimNpcStudio

Dialog {
    id: root
    required property StudioController studio
    property var exportResult: null
    property string statusMessage: ""
    property bool exportSuccess: false
    property string exportedModDir: ""
    property string exportedZip: ""

    modal: true
    anchors.centerIn: Overlay.overlay
    width: 660
    title: qsTr("Exportar Mod — Character Creator 3.0")
    standardButtons: Dialog.Close

    Settings {
        id: prefs
        category: "export_wizard"
        property string lastDirectory: ""
    }

    onAboutToShow: {
        exportResult = null
        statusMessage = ""
        exportSuccess = false
        exportedModDir = ""
        exportedZip = ""
        if (pluginName.text.length === 0) {
            pluginName.text = root.studio.outfit.suggestedPluginName.length > 0 ?
                root.studio.outfit.suggestedPluginName : "CustomNpcOverhaul.esp"
        }
        if (outputDir.text.length === 0) {
            outputDir.text = prefs.lastDirectory.length > 0 ? prefs.lastDirectory : root.studio.outfit.defaultExportDirectory
        }
    }

    Connections {
        target: root.studio.appearance
        function onExportFinished(success, message, modDir, zipPath) {
            root.exportSuccess = success
            root.statusMessage = message
            root.exportedModDir = modDir
            root.exportedZip = zipPath
        }
    }

    FolderDialog {
        id: folderDialog
        title: qsTr("Carpeta de destino para el mod")
        onAccepted: {
            outputDir.text = decodeURIComponent(selectedFolder.toString().replace(/^file:\/{3}/, ""))
            prefs.lastDirectory = outputDir.text
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        Text {
            text: qsTr("Selecciona el tipo de mod a generar para Skyrim SE/AE:")
            color: Theme.text
            font.bold: true
            font.pixelSize: 12
        }

        // Selección de Modo
        ButtonGroup { id: modeGroup }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: modeCol.implicitHeight + 16
            color: Theme.control
            radius: 4
            border.color: Theme.border

            ColumnLayout {
                id: modeCol
                anchors.fill: parent
                anchors.margins: 8
                spacing: 8

                RadioButton {
                    id: modeB
                    ButtonGroup.group: modeGroup
                    checked: true
                    text: qsTr("Modo B: Personalización Facial (Face Overhaul)")
                    font.bold: true
                    ToolTip.text: qsTr("Genera FaceGen NIF y DDS empaquetados, morphs faciales, tintes y headparts compatibles.")
                    ToolTip.visible: hovered
                }
                Text {
                    text: qsTr("  → Incluye mallas FaceGen, texturas FaceTint, morphs de Skyrim y registro ESP/ESL.")
                    color: Theme.textDim
                    font.pixelSize: 10
                }

                RadioButton {
                    id: modeC
                    ButtonGroup.group: modeGroup
                    text: qsTr("Modo C: Overhaul Completo (Rostro + Atuendo)")
                    font.bold: true
                    ToolTip.text: qsTr("Combina el nuevo rostro personalizado y las prendas/armaduras asignadas en un solo mod.")
                    ToolTip.visible: hovered
                }
                Text {
                    text: qsTr("  → La solución todo-en-uno: apariencia visual completa + vestuario con piezas equipadas.")
                    color: Theme.textDim
                    font.pixelSize: 10
                }

                RadioButton {
                    id: modeA
                    ButtonGroup.group: modeGroup
                    text: qsTr("Modo A: Solo Atuendo (Outfit Patch)")
                    font.bold: true
                    ToolTip.text: qsTr("Genera únicamente el parche de armadura/ropa sin tocar los archivos de rostro.")
                    ToolTip.visible: hovered
                }
                Text {
                    text: qsTr("  → Modifica el atuendo (OTFT) y piezas equipadas para el NPC seleccionado.")
                    color: Theme.textDim
                    font.pixelSize: 10
                }
            }
        }

        // Configuración de exportación
        Label { text: qsTr("Nombre del archivo de plugin (.esp)") }
        TextField {
            id: pluginName
            Layout.fillWidth: true
            placeholderText: "MyCustomNpcOverhaul.esp"
        }

        Label { text: qsTr("Carpeta de salida (staging para MO2)") }
        RowLayout {
            Layout.fillWidth: true
            TextField {
                id: outputDir
                Layout.fillWidth: true
                placeholderText: "D:/ModOrganizer/mods/..."
            }
            Button {
                text: qsTr("Elegir…")
                onClicked: folderDialog.open()
            }
        }

        CheckBox {
            id: eslCheck
            text: qsTr("Marcar como ESL (ESP-FE: no consume slot de la carga máxima de 254 plugins)")
            checked: true
        }

        CheckBox {
            id: zipCheck
            text: qsTr("Empaquetar en archivo ZIP listo para instalar con Mod Organizer 2")
            checked: true
        }

        // Estado del proceso o resultados
        Rectangle {
            visible: root.statusMessage.length > 0
            Layout.fillWidth: true
            implicitHeight: statusCol.implicitHeight + 12
            radius: 4
            color: root.exportSuccess ? "#1B4D3E" : "#5A1A1A"
            border.color: root.exportSuccess ? Theme.ok : Theme.error

            ColumnLayout {
                id: statusCol
                anchors.fill: parent
                anchors.margins: 8
                spacing: 4

                Text {
                    text: root.exportSuccess ? qsTr("✓ ¡Exportación exitosa!") : qsTr("✕ Error en exportación")
                    color: "#FFFFFF"
                    font.bold: true
                }
                Text {
                    text: root.statusMessage
                    color: "#E0E0E0"
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                Text {
                    visible: root.exportedZip.length > 0
                    text: qsTr("Archivo ZIP: %1").arg(root.exportedZip)
                    color: Theme.accent
                    font.pixelSize: 10
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }

            Button {
                text: qsTr("Cancelar")
                onClicked: root.close()
            }

            Button {
                text: qsTr("Exportar Mod")
                highlighted: true
                enabled: pluginName.text.length > 0 && outputDir.text.length > 0 && !root.studio.appearance.exporting
                onClicked: {
                    var pName = pluginName.text.trim()
                    if (!pName.toLowerCase().endsWith(".esp")) pName += ".esp"
                    var oDir = outputDir.text.trim()

                    if (modeA.checked) {
                        // Modo A: Outfit Patch
                        root.studio.outfit.exportPatch(pName, oDir, eslCheck.checked, zipCheck.checked, "Exported via Studio", false, true)
                    } else if (modeB.checked) {
                        // Modo B: Face Overhaul
                        root.studio.appearance.exportFaceOverhaul(pName, oDir, eslCheck.checked, zipCheck.checked)
                    } else if (modeC.checked) {
                        // Modo C: Complete Overhaul
                        root.studio.appearance.exportCompleteOverhaul(pName, oDir, eslCheck.checked, zipCheck.checked)
                    }
                }
            }
        }
    }
}
