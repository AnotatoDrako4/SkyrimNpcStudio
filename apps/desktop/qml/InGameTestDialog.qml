import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import SkyrimNpcStudio

// M2 «Preparar prueba en Skyrim»: packages the console batch file and acceptance report of an export already
// installed in the loaded (test) profile as a new mod, and tells the owner what to do in the game. Live state comes
// from StudioController.inGameTest; nothing here writes in MO2 or the game.
Dialog {
    id: root
    objectName: "inGameTestDialog"
    required property StudioController studio
    readonly property var testState: studio.inGameTest
    readonly property string phase: testState && testState.state ? testState.state : ""
    readonly property bool busy: phase === "preparing" || phase === "exporting"

    // An export folder or its manifest.json (path or file:// URL).
    function prepare(source) {
        root.studio.prepareInGameTest(source)
        root.open()
    }
    // Exports of earlier sessions, including Character Forge ones.
    function chooseExport() { exportFolderDialog.open() }

    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(Theme.px(720), (parent ? parent.width : Theme.px(720)) - 40)
    title: qsTr("Preparar prueba en Skyrim")
    standardButtons: Dialog.Close

    FolderDialog {
        id: exportFolderDialog
        title: qsTr("Carpeta de una exportación del estudio (la que contiene manifest.json)")
        onAccepted: root.prepare(selectedFolder.toString())
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingMd

        // ---- working
        RowLayout {
            visible: root.busy
            spacing: Theme.spacingMd
            BusyIndicator { running: root.busy; Layout.preferredWidth: Theme.px(32); Layout.preferredHeight: Theme.px(32) }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.text
                text: root.phase === "preparing"
                      ? qsTr("Comprobando que el perfil «%1» carga exactamente el plugin exportado…").arg(root.studio.selectedProfile)
                      : qsTr("Empaquetando el kit como mod nuevo (carpeta y ZIP) junto a la exportación…")
            }
        }

        // ---- refused or failed
        ColumnLayout {
            visible: root.phase === "error"
            Layout.fillWidth: true
            spacing: Theme.spacingSm
            Label {
                text: qsTr("No se preparó el kit. No se ha escrito nada.")
                color: Theme.error
                font.bold: true
            }
            Repeater {
                model: root.testState && root.testState.errors ? root.testState.errors : []
                delegate: Label {
                    required property string modelData
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.text
                    text: "• " + modelData
                }
            }
            Button {
                text: qsTr("Elegir otra exportación…")
                onClicked: root.chooseExport()
            }
        }

        // ---- ready
        ColumnLayout {
            visible: root.phase === "done"
            Layout.fillWidth: true
            spacing: Theme.spacingMd

            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.ok
                font.bold: true
                text: qsTr("Kit listo y verificado por lectura (%1). Falta la prueba en el juego: PENDIENTE_SKYRIM.")
                      .arg(root.testState && root.testState.status ? root.testState.status : "")
            }

            Label { text: qsTr("En la consola de Skyrim escribe:"); color: Theme.textDim }
            RowLayout {
                Layout.fillWidth: true
                TextField {
                    objectName: "batchCommandField"
                    Layout.fillWidth: true
                    readOnly: true
                    selectByMouse: true
                    font.family: Theme.mono
                    text: root.testState && root.testState.batchCommand ? root.testState.batchCommand : ""
                }
                Button {
                    text: qsTr("Copiar")
                    onClicked: root.studio.copyToClipboard(root.testState.batchCommand)
                }
            }

            KeyValue {
                Layout.fillWidth: true
                mono: true
                label: qsTr("ZIP para MO2")
                value: root.testState && root.testState.zipPath ? root.testState.zipPath : qsTr("(sin ZIP)")
            }
            KeyValue {
                Layout.fillWidth: true
                mono: true
                label: qsTr("Carpeta del kit")
                value: root.testState && root.testState.modDirectory ? root.testState.modDirectory : ""
            }
            KeyValue {
                Layout.fillWidth: true
                mono: true
                label: qsTr("Acta")
                value: root.testState && root.testState.reportFile ? root.testState.reportFile : ""
            }

            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.text
                textFormat: Text.PlainText
                text: qsTr("1. En MO2, con el perfil de prueba «%1», usa «Instalar mod desde archivo» con el ZIP y actívalo.\n"
                           + "2. Cierra MO2 si el estudio lo necesita y lanza Skyrim desde MO2 con ese perfil.\n"
                           + "3. Carga una partida de prueba, abre la consola (º / ~) y escribe el comando de arriba.\n"
                           + "4. Anota en el acta (ACTA_….md) lo que ves, con capturas. Solo tu observación lo marca VERIFICADO_EN_SKYRIM.")
                      .arg(root.studio.selectedProfile)
            }

            Label { text: qsTr("Registros del kit"); color: Theme.textDim }
            ListView {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(contentHeight, Theme.px(180))
                clip: true
                model: root.testState && root.testState.lines ? root.testState.lines : []
                ScrollBar.vertical: ScrollBar {}
                delegate: Label {
                    required property var modelData
                    width: ListView.view.width
                    elide: Text.ElideRight
                    font.family: Theme.mono
                    font.pixelSize: Theme.small
                    color: modelData.command.length > 0 ? Theme.text : Theme.textDim
                    text: modelData.recordType + "  " + (modelData.editorId || modelData.formKey) + "  →  "
                          + (modelData.command.length > 0 ? modelData.command : (modelData.note || qsTr("sin prueba por consola")))
                }
            }

            RowLayout {
                Button {
                    text: qsTr("Abrir carpeta")
                    enabled: !!(root.testState && root.testState.modDirectory)
                    onClicked: root.studio.openFolder(root.testState.zipPath ? root.testState.zipPath : root.testState.modDirectory)
                }
                Button {
                    text: qsTr("Preparar otra…")
                    onClicked: root.chooseExport()
                }
            }
        }
    }
}
