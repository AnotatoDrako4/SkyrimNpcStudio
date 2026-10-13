import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

ToolBar {
    id: root
    required property CharacterForgeViewModel forge
    signal openRequested
    signal saveRequested
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        ToolButton {
            text: qsTr("Nuevo")
            enabled: !root.forge.busy
            onClicked: root.forge.newProject(qsTr("Nuevo personaje"))
        }
        ToolButton {
            text: qsTr("Abrir")
            enabled: !root.forge.busy
            onClicked: root.openRequested()
        }
        ToolButton {
            text: qsTr("Guardar")
            enabled: root.forge.hasProject && !root.forge.busy
            onClicked: root.saveRequested()
        }
        ToolSeparator {}
        ToolButton {
            text: qsTr("Deshacer")
            enabled: root.forge.canUndo && !root.forge.busy
            onClicked: root.forge.undo()
        }
        ToolButton {
            text: qsTr("Rehacer")
            enabled: root.forge.canRedo && !root.forge.busy
            onClicked: root.forge.redo()
        }
        Item {
            Layout.fillWidth: true
        }
        BusyIndicator {
            running: root.forge.busy
            visible: running
            implicitWidth: 24
            implicitHeight: 24
        }
        ToolButton {
            text: qsTr("Cancelar")
            visible: root.forge.busy
            onClicked: root.forge.cancelOperation()
        }
        ToolButton {
            text: qsTr("Validar NPC")
            enabled: root.forge.hasProject && root.forge.catalogReady && !root.forge.busy
            onClicked: root.forge.validateNpc()
        }
    }
}
