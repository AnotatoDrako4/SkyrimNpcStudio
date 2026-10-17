import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

// Instance and profile selection. Everything here is read-only towards MO2.
Dialog {
    id: root
    required property StudioController studio
    signal browseInstance()
    signal browseGame()
    signal forgeWithoutProfile()

    modal: true
    anchors.centerIn: Overlay.overlay
    width: Theme.px(640)
    title: qsTr("Instancia de Mod Organizer 2")
    closePolicy: root.studio.phase === StudioController.Ready ? Popup.CloseOnEscape : Popup.NoAutoClose
    standardButtons: root.studio.phase === StudioController.Ready ? Dialog.Close : Dialog.NoButton

    Connections {
        target: root.studio
        function onLoaded() { root.close() }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.textDim
            font.pixelSize: Theme.small
            text: qsTr("El estudio lee la instancia y el perfil en modo solo lectura: no modifica modlist.txt, plugins.txt, "
                       + "la carpeta Overwrite ni la carpeta Data del juego. Funciona aunque MO2 esté cerrado.")
        }

        SectionHeader { title: qsTr("Instancias detectadas"); detail: root.studio.instances.count + "" }
        ListView {
            id: instanceList
            Layout.fillWidth: true
            Layout.preferredHeight: 110
            clip: true
            model: root.studio.instances
            delegate: ItemDelegate {
                required property string name
                required property string kind
                required property string path
                width: ListView.view.width
                highlighted: path === root.studio.instancePath
                contentItem: ColumnLayout {
                    spacing: 0
                    Text { text: name + "  (" + (kind === "global" ? qsTr("global") : qsTr("portable")) + ")"; color: Theme.text; font.pixelSize: Theme.normal }
                    Text { text: path; color: Theme.textDim; font.pixelSize: Theme.small; elide: Text.ElideMiddle; Layout.fillWidth: true }
                }
                onClicked: root.studio.openInstance(path)
                background: Rectangle {
                    color: parent.highlighted ? Theme.accentDim : parent.hovered ? Theme.panelAlt : "transparent"
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border; opacity: 0.5 }
                }
            }
            Text {
                anchors.centerIn: parent
                visible: instanceList.count === 0
                text: qsTr("No hay instancias globales. Abre el ModOrganizer.ini de tu instalación portable.")
                color: Theme.textDim
                font.pixelSize: Theme.small
            }
        }

        RowLayout {
            Button { text: qsTr("Abrir ModOrganizer.ini…"); onClicked: root.browseInstance() }
            Button { text: qsTr("Buscar de nuevo"); onClicked: root.studio.discoverInstances() }
        }

        SectionHeader { title: qsTr("Perfil"); detail: root.studio.instanceName }
        RowLayout {
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                text: root.studio.gamePath
                elide: Text.ElideMiddle
                ToolTip.visible: gameHover.hovered
                ToolTip.text: text
                HoverHandler { id: gameHover }
            }
            Button { text: qsTr("Carpeta de Skyrim..."); onClicked: root.browseGame() }
        }
        RowLayout {
            Layout.fillWidth: true
            ComboBox {
                id: profileBox
                Layout.fillWidth: true
                model: root.studio.profiles
                enabled: root.studio.profiles.length > 0
                currentIndex: Math.max(0, root.studio.profiles.indexOf(root.studio.selectedProfile))
            }
            Button {
                text: qsTr("Cargar perfil")
                highlighted: true
                enabled: profileBox.currentIndex >= 0 && root.studio.profiles.length > 0
                         && root.studio.phase !== StudioController.Scanning && root.studio.phase !== StudioController.Indexing
                onClicked: root.studio.loadProfile(profileBox.currentText)
            }
        }

        ColumnLayout {
            visible: root.studio.phase === StudioController.Scanning || root.studio.phase === StudioController.Indexing
                     || root.studio.phase === StudioController.Error
            Layout.fillWidth: true
            Text {
                text: root.studio.progressText
                color: root.studio.phase === StudioController.Error ? Theme.error : Theme.text
                font.pixelSize: Theme.small
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            RowLayout {
                Layout.fillWidth: true
                ProgressBar {
                    Layout.fillWidth: true
                    visible: root.studio.phase !== StudioController.Error
                    indeterminate: root.studio.progress < 0
                    value: Math.max(0, root.studio.progress)
                }
                Button {
                    text: qsTr("Cancelar")
                    visible: root.studio.phase !== StudioController.Error
                    onClicked: root.studio.cancel()
                }
            }
        }

        // Character Forge creates projects and previews external models without MO2; only the
        // record pickers and the export need an indexed profile.
        Button {
            Layout.alignment: Qt.AlignRight
            visible: root.studio.phase !== StudioController.Ready
            text: qsTr("Continuar en Character Forge sin perfil")
            onClicked: root.forgeWithoutProfile()
        }
    }
}
