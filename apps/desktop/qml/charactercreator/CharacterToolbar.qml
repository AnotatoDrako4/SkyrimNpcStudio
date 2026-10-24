import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

Rectangle {
    id: root
    required property StudioController studio
    property var viewport: null

    implicitHeight: Theme.controlHeight + Theme.spacingLg
    color: Theme.header
    border.color: Theme.border
    border.width: 1

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingMd
        anchors.rightMargin: Theme.spacingMd
        spacing: Theme.spacingSm

        // Cámara: Enfoques rápidos
        Text {
            text: qsTr("Cámara:")
            color: Theme.textDim
            font.pixelSize: Theme.small
        }

        Button {
            text: qsTr("Rostro")
            implicitHeight: Theme.controlHeight
            // Shrink (eliding) before the row runs out of a narrow viewer.
            Layout.fillWidth: true
            Layout.maximumWidth: implicitWidth
            Layout.minimumWidth: Math.min(implicitWidth, Theme.px(44))
            onClicked: if (root.viewport) root.viewport.applyCameraPreset(4)
            ToolTip.text: qsTr("Enfocar rostro para detalle fino")
            ToolTip.visible: hovered
        }

        Button {
            text: qsTr("Cuerpo")
            implicitHeight: Theme.controlHeight
            // Shrink (eliding) before the row runs out of a narrow viewer.
            Layout.fillWidth: true
            Layout.maximumWidth: implicitWidth
            Layout.minimumWidth: Math.min(implicitWidth, Theme.px(44))
            onClicked: if (root.viewport) root.viewport.applyCameraPreset(0)
            ToolTip.text: qsTr("Enfocar cuerpo completo")
            ToolTip.visible: hovered
        }

        Button {
            text: qsTr("Frontal")
            implicitHeight: Theme.controlHeight
            // Shrink (eliding) before the row runs out of a narrow viewer.
            Layout.fillWidth: true
            Layout.maximumWidth: implicitWidth
            Layout.minimumWidth: Math.min(implicitWidth, Theme.px(44))
            onClicked: if (root.viewport) root.viewport.applyCameraPreset(1)
            ToolTip.text: qsTr("Vista frontal")
            ToolTip.visible: hovered
        }

        Button {
            text: qsTr("Lateral")
            implicitHeight: Theme.controlHeight
            // Shrink (eliding) before the row runs out of a narrow viewer.
            Layout.fillWidth: true
            Layout.maximumWidth: implicitWidth
            Layout.minimumWidth: Math.min(implicitWidth, Theme.px(44))
            onClicked: if (root.viewport) root.viewport.applyCameraPreset(2)
            ToolTip.text: qsTr("Vista lateral 90°")
            ToolTip.visible: hovered
        }

        Button {
            text: qsTr("Posterior")
            implicitHeight: Theme.controlHeight
            // Shrink (eliding) before the row runs out of a narrow viewer.
            Layout.fillWidth: true
            Layout.maximumWidth: implicitWidth
            Layout.minimumWidth: Math.min(implicitWidth, Theme.px(44))
            onClicked: if (root.viewport) root.viewport.applyCameraPreset(3)
            ToolTip.text: qsTr("Vista posterior 180°")
            ToolTip.visible: hovered
        }

        ToolButton {
            text: "↻"
            Accessible.name: qsTr("Restablecer cámara orbital")
            implicitHeight: Theme.controlHeight
            onClicked: if (root.viewport) root.viewport.reset()
            ToolTip.text: qsTr("Restablecer cámara orbital")
            ToolTip.visible: hovered
        }

        Rectangle { visible: compareBtn.visible; width: 1; height: Theme.px(20); color: Theme.border }

        // Comparador Antes / Después
        Button {
            id: compareBtn
            // Same switch as "✨ Mod / 👁 Original" on the viewer overlay, which is always there: this
            // larger copy only shows when the row has room for it.
            visible: root.width >= 760 * Theme.fontFactor
            implicitHeight: Theme.controlHeight
            checkable: true
            checked: root.studio.scene.original
            text: checked ? qsTr("👁 Viendo: ORIGINAL") : qsTr("✨ Viendo: MODIFICADO")
            Accessible.name: checked ? qsTr("Viendo la apariencia original") : qsTr("Viendo la apariencia modificada")
            palette.button: checked ? "#8B4513" : "#2E7D32"
            onToggled: root.studio.scene.original = checked
            ToolTip.text: qsTr("Alternar en tiempo real entre la apariencia original de Skyrim y tus modificaciones")
            ToolTip.visible: hovered
        }

        Item { Layout.fillWidth: true }

        // Origin of the current face (a donor or a preset); it follows undo/redo and reset.
        Rectangle {
            visible: root.studio.appearance.faceSource.length > 0
            implicitHeight: 24
            implicitWidth: donorRow.implicitWidth + 12
            radius: 12
            color: "#1E3A5F"
            border.color: "#3498DB"

            RowLayout {
                id: donorRow
                anchors.centerIn: parent
                spacing: 4
                Text {
                    text: root.studio.appearance.faceSource
                    color: "#FFFFFF"
                    font.bold: true
                    font.pixelSize: Theme.micro
                    elide: Text.ElideRight
                }
            }
        }
    }
}
