import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

Rectangle {
    id: root
    required property StudioController studio
    property var viewport: null

    implicitHeight: 40
    color: Theme.header
    border.color: Theme.border
    border.width: 1

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        spacing: 6

        // Cámara: Enfoques rápidos
        Text {
            text: qsTr("Cámara:")
            color: Theme.textDim
            font.pixelSize: 11
        }

        Button {
            text: qsTr("Rostro")
            implicitHeight: 28
            onClicked: if (root.viewport) root.viewport.applyCameraPreset(4)
            ToolTip.text: qsTr("Enfocar rostro para detalle fino")
            ToolTip.visible: hovered
        }

        Button {
            text: qsTr("Cuerpo")
            implicitHeight: 28
            onClicked: if (root.viewport) root.viewport.applyCameraPreset(0)
            ToolTip.text: qsTr("Enfocar cuerpo completo")
            ToolTip.visible: hovered
        }

        Button {
            text: qsTr("Frontal")
            implicitHeight: 28
            onClicked: if (root.viewport) root.viewport.applyCameraPreset(1)
            ToolTip.text: qsTr("Vista frontal")
            ToolTip.visible: hovered
        }

        Button {
            text: qsTr("Lateral")
            implicitHeight: 28
            onClicked: if (root.viewport) root.viewport.applyCameraPreset(2)
            ToolTip.text: qsTr("Vista lateral 90°")
            ToolTip.visible: hovered
        }

        Button {
            text: qsTr("Posterior")
            implicitHeight: 28
            onClicked: if (root.viewport) root.viewport.applyCameraPreset(3)
            ToolTip.text: qsTr("Vista posterior 180°")
            ToolTip.visible: hovered
        }

        ToolButton {
            text: "↻"
            implicitHeight: 28
            onClicked: if (root.viewport) root.viewport.reset()
            ToolTip.text: qsTr("Restablecer cámara orbital")
            ToolTip.visible: hovered
        }

        Rectangle { width: 1; height: 20; color: Theme.border }

        // Comparador Antes / Después
        Button {
            id: compareBtn
            implicitHeight: 28
            checkable: true
            checked: root.studio.scene.original
            text: checked ? qsTr("👁 Viendo: ORIGINAL") : qsTr("✨ Viendo: MODIFICADO")
            palette.button: checked ? "#8B4513" : "#2E7D32"
            onToggled: root.studio.scene.original = checked
            ToolTip.text: qsTr("Alternar en tiempo real entre la apariencia original de Skyrim y tus modificaciones")
            ToolTip.visible: hovered
        }

        Item { Layout.fillWidth: true }

        // Indicador si hay donante activo
        Rectangle {
            visible: root.studio.appearance.donorFormKey.length > 0
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
                    text: qsTr("Donante:")
                    color: "#A9CCE3"
                    font.pixelSize: 10
                }
                Text {
                    text: root.studio.appearance.donorName || root.studio.appearance.donorFormKey
                    color: "#FFFFFF"
                    font.bold: true
                    font.pixelSize: 10
                    elide: Text.ElideRight
                }
            }
        }
    }
}
