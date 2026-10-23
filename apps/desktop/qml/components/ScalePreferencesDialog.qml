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
    // Quick access kept for the View menu and the status bar; everything else lives in Configuración (Ctrl+,).
    signal moreRequested()

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingLg

        // Readability presets: scale, text and density together. Applied as a preview (the banner asks to keep
        // it and reverts on its own otherwise), through the same AppSettings preview as the theme.
        Label { text: qsTr("Presentación"); font.bold: true }
        RowLayout {
            objectName: "readabilityPresets"
            Layout.fillWidth: true
            spacing: Theme.spacingSm
            Repeater {
                model: Workbench.readabilityPresets
                delegate: Button {
                    required property var modelData
                    objectName: "readabilityPreset_" + modelData.key
                    Layout.fillWidth: true
                    text: modelData.name
                    checkable: true
                    checked: Workbench.readabilityPreset === modelData.key
                    Accessible.description: modelData.detail
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
                    ToolTip.text: modelData.detail
                    onClicked: {
                        if (Workbench.readabilityPreset === modelData.key) return
                        AppSettings.beginPreview()
                        Workbench.applyReadabilityPreset(modelData.key)
                    }
                }
            }
        }
        Label {
            Layout.fillWidth: true
            visible: Workbench.readabilityPreset === "custom"
            text: qsTr("Ajuste personalizado: interfaz %1 %, texto %2 %.").arg(Workbench.uiScale).arg(Workbench.fontScale)
            color: Theme.textDim
            font.pixelSize: Theme.small
            wrapMode: Text.WordWrap
        }
        Label {
            objectName: "legibilityNotice"
            Layout.fillWidth: true
            visible: Theme.legibilityFloor
            text: qsTr("Con esta combinación el texto bajaría de lo legible: se mantiene en %1 px. Tu preferencia se conserva; «Equilibrado» o «Compacto» reducen el espacio sin achicar el texto.").arg(Theme.normal)
            color: Theme.warn
            font.pixelSize: Theme.small
            wrapMode: Text.WordWrap
        }

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

        RowLayout {
            Layout.fillWidth: true
            Button {
                objectName: "moreSettingsButton"
                text: qsTr("Todos los ajustes… (Ctrl+,)")
                onClicked: dialog.moreRequested()
            }
            Item { Layout.fillWidth: true }
            Button {
                text: qsTr("Restablecer valores iniciales")
                enabled: !Workbench.appearanceAtDefaults
                onClicked: Workbench.resetAppearance()
            }
        }
    }
}
