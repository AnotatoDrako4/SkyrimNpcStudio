import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

Rectangle {
    id: root
    required property StudioController studio
    property var viewport: null
    property int activeStep: 1
    signal stepActivated(int stepIndex)

    implicitHeight: Theme.controlHeight + Theme.spacingSm
    color: Theme.header
    border.color: Theme.border
    border.width: 1

    readonly property var workflowSteps: [
        { step: 0, label: qsTr("1. NPC"), glyph: "person", tip: qsTr("Seleccionar NPC en el catálogo") },
        { step: 1, label: qsTr("2. Rostro"), glyph: "person", tip: qsTr("Editar facciones, morphs y tono de piel") },
        { step: 2, label: qsTr("3. Cabello"), glyph: "eye", tip: qsTr("Editar peinado, ojos, cejas y barba") },
        { step: 3, label: qsTr("4. Cuerpo"), glyph: "people", tip: qsTr("Peso corporal, preset BodySlide y familia corporal") },
        { step: 4, label: qsTr("5. Vestuario"), glyph: "tag", tip: qsTr("Combinar prendas ARMO y revisar slots ocupados") },
        { step: 5, label: qsTr("6. Inventario"), glyph: "folder", tip: qsTr("Editar objetos y cantidades en el inventario del NPC") },
        { step: 6, label: qsTr("7. Exportar"), glyph: "export", tip: qsTr("Exportar parche o mod completo verificado") }
    ]

    RowLayout {
        id: toolbarRow
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingSm
        anchors.rightMargin: Theme.spacingSm
        spacing: Theme.spacingXs

        Text {
            visible: root.width >= Theme.px(640)
            text: qsTr("Flujo:")
            color: Theme.textMuted
            font.pixelSize: Theme.micro
            font.bold: true
        }

        Repeater {
            model: root.workflowSteps
            delegate: AbstractButton {
                id: stepPill
                required property var modelData
                readonly property bool isCurrent: root.activeStep === modelData.step
                readonly property bool isExport: modelData.step === 6
                Layout.fillWidth: true
                implicitHeight: Theme.px(26)
                implicitWidth: stepRow.implicitWidth + Theme.sp(12)
                Accessible.name: modelData.label + ": " + modelData.tip
                ToolTip.visible: hovered
                ToolTip.delay: 400
                ToolTip.text: modelData.tip
                onClicked: root.stepActivated(modelData.step)

                background: Rectangle {
                    radius: Theme.radius
                    color: stepPill.isCurrent
                           ? Theme.accentDim
                           : (stepPill.isExport ? Theme.accentSubtle : (stepPill.hovered ? Theme.panelAlt : Theme.panel))
                    border.width: stepPill.visualFocus ? Theme.focusWidth : 1
                    border.color: stepPill.isCurrent || stepPill.isExport ? Theme.accent : Theme.borderSubtle
                }

                contentItem: RowLayout {
                    id: stepRow
                    spacing: Theme.sp(4)
                    Icon {
                        name: stepPill.modelData.glyph
                        size: Theme.micro + 1
                        color: stepPill.isCurrent || stepPill.isExport ? Theme.accent : Theme.textDim
                        Layout.leftMargin: Theme.sp(4)
                    }
                    Text {
                        text: stepPill.modelData.label
                        color: stepPill.isCurrent ? Theme.text : (stepPill.isExport ? Theme.accent : Theme.textDim)
                        font.pixelSize: Theme.micro
                        font.bold: stepPill.isCurrent || stepPill.isExport
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }
            }
        }

        Item { Layout.fillWidth: true }

        // Origen de rostro / donante activo
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
