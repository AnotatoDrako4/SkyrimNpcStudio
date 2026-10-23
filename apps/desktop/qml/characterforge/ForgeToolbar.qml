import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

// Actions of the Character Forge draft. "Abrir" and "Guardar" live once, in the window header (and Ctrl+S /
// Proyecto menu); this bar keeps what is particular to the draft: new, undo/redo, the running operation and
// the validation. The open/save signals stay for callers that still use them.
ToolBar {
    id: root
    required property CharacterForgeViewModel forge
    signal openRequested
    signal saveRequested
    implicitHeight: Theme.controlHeight + Theme.sp(10)
    background: Rectangle {
        color: Theme.panelHeader
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.borderSubtle }
    }
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingMd
        anchors.rightMargin: Theme.spacingMd
        spacing: Theme.spacingXs
        IconToolButton {
            objectName: "forgeNewButton"
            glyph: "add"
            label: qsTr("Nuevo proyecto")
            enabled: !root.forge.busy
            tip: qsTr("Empieza un borrador .sncf vacío (el actual no se guarda solo)")
            onClicked: root.forge.newProject(qsTr("Nuevo personaje"))
        }
        ToolSeparator {}
        IconToolButton {
            glyph: "undo"
            label: qsTr("Deshacer")
            shortcutText: "Ctrl+Z"
            enabled: root.forge.canUndo && !root.forge.busy
            onClicked: root.forge.undo()
        }
        IconToolButton {
            glyph: "redo"
            label: qsTr("Rehacer")
            shortcutText: "Ctrl+Y"
            enabled: root.forge.canRedo && !root.forge.busy
            onClicked: root.forge.redo()
        }
        Item {
            Layout.fillWidth: true
        }
        BusyIndicator {
            running: root.forge.busy
            visible: running
            implicitWidth: Theme.controlHeight
            implicitHeight: Theme.controlHeight
        }
        IconToolButton {
            glyph: "close"
            label: qsTr("Cancelar")
            visible: root.forge.busy
            tip: qsTr("Cancelar la operación en curso")
            onClicked: root.forge.cancelOperation()
        }
        IconToolButton {
            glyph: "ok"
            label: qsTr("Validar NPC")
            enabled: root.forge.hasProject && root.forge.catalogReady && !root.forge.busy
            tip: !root.forge.hasProject ? qsTr("Crea o abre un proyecto para validarlo")
                 : !root.forge.catalogReady ? qsTr("Carga un perfil MO2 indexado: la validación usa los registros reales")
                 : qsTr("Validación estructural y de comportamiento contra el perfil indexado")
            onClicked: root.forge.validateNpc()
        }
    }
}
