import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

ColumnLayout {
    id: root
    required property CharacterForgeViewModel forge
    property var skeletonData: []
    Text {
        text: qsTr("Esqueleto y rigging")
        color: Theme.text
        font.bold: true
        font.pixelSize: Theme.title
    }
    Text {
        Layout.fillWidth: true
        text: qsTr("No comprobado. La selección de un modelo no aporta todavía una jerarquía NIF validada ni datos de skinning.")
        color: Theme.textDim
        wrapMode: Text.WordWrap
    }
    Text {
        visible: root.skeletonData.length === 0
        text: qsTr("Sin huesos inspeccionados")
        color: Theme.textMuted
    }
    Repeater {
        model: root.skeletonData
        ItemDelegate {
            required property var modelData
            Layout.fillWidth: true
            text: modelData.name
            leftPadding: 12 + (modelData.depth || 0) * 12
            onClicked: details.text = modelData.diagnostic || qsTr("Sin diagnóstico")
        }
    }
    Text {
        id: details
        Layout.fillWidth: true
        text: qsTr("Compatibilidad Skyrim pendiente de validar recursos reales.")
        color: Theme.textDim
        wrapMode: Text.WordWrap
    }
    Button {
        Layout.fillWidth: true
        text: qsTr("Adaptar rig · motor pendiente")
        enabled: false
    }
}
