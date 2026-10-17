import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

// Vista → Preferencias de apariencia: interface scale, text size and density. Every change applies at
// once and is kept by the Workbench; only the window changes, never the 3D scene or a project.
Dialog {
    id: dialog
    title: qsTr("Preferencias de apariencia")
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(Theme.px(440), (parent ? parent.width : Theme.px(440)) - 32)
    standardButtons: Dialog.Close

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingLg

        Label {
            text: qsTr("Escala de interfaz: %1 %").arg(Workbench.uiScale)
            font.bold: true
        }
        Slider {
            objectName: "uiScaleSlider"
            Layout.fillWidth: true
            from: 85; to: 150; stepSize: 5
            value: Workbench.uiScale
            onMoved: Workbench.uiScale = Math.round(value)
        }

        Label {
            text: qsTr("Tamaño del texto: %1 %").arg(Workbench.fontScale)
            font.bold: true
        }
        Slider {
            objectName: "fontScaleSlider"
            Layout.fillWidth: true
            from: 80; to: 160; stepSize: 5
            value: Workbench.fontScale
            onMoved: Workbench.fontScale = Math.round(value)
        }

        Label { text: qsTr("Densidad"); font.bold: true }
        RowLayout {
            spacing: Theme.spacingMd
            RadioButton { text: qsTr("Compacta"); checked: Workbench.density === Workbench.Compact; onClicked: Workbench.density = Workbench.Compact }
            RadioButton { text: qsTr("Normal"); checked: Workbench.density === Workbench.Normal; onClicked: Workbench.density = Workbench.Normal }
            RadioButton { text: qsTr("Cómoda"); checked: Workbench.density === Workbench.Comfortable; onClicked: Workbench.density = Workbench.Comfortable }
        }

        Label {
            Layout.fillWidth: true
            text: qsTr("Atajos: Ctrl++ aumenta, Ctrl+- reduce y Ctrl+0 vuelve al 100 % la escala de interfaz.")
            color: Theme.textDim
            font.pixelSize: Theme.small
            wrapMode: Text.WordWrap
        }

        Button {
            Layout.alignment: Qt.AlignRight
            text: qsTr("Restablecer valores iniciales")
            enabled: !Workbench.appearanceAtDefaults
            onClicked: Workbench.resetAppearance()
        }
    }
}
