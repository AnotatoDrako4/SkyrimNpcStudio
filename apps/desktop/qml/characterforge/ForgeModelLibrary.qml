import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

ColumnLayout {
    id: root
    required property CharacterForgeViewModel forge
    signal convertPreviewRequested(bool force)

    readonly property var currentModel: forge.workspace.model || ({})

    Text {
        text: qsTr("Biblioteca del proyecto")
        color: Theme.text
        font.bold: true
        font.pixelSize: Theme.title
    }
    Text {
        Layout.fillWidth: true
        text: qsTr("Los modelos externos son enlaces locales. Los modelos copiados o convertidos viven dentro de la carpeta .sncf y conservan sus recursos glTF validados.")
        color: Theme.textDim
        wrapMode: Text.WordWrap
    }
    Text {
        visible: !(root.forge.workspace.library || []).length
        text: qsTr("La biblioteca está vacía.")
        color: Theme.textMuted
    }
    Repeater {
        model: root.forge.workspace.library || []
        delegate: Rectangle {
            required property int index
            required property var modelData
            readonly property bool isSelected: root.currentModel.path === modelData.path && root.currentModel.storage === modelData.storage
            Layout.fillWidth: true
            implicitHeight: libRow.implicitHeight + 8
            radius: Theme.radius
            color: isSelected ? Theme.accentSubtle : Theme.panelAlt
            border.color: isSelected ? Theme.accentBorder : Theme.borderSubtle
            RowLayout {
                id: libRow
                anchors.fill: parent
                anchors.margins: 4
                spacing: 4
                ItemDelegate {
                    Layout.fillWidth: true
                    highlighted: isSelected
                    enabled: !root.forge.busy
                    text: modelData.name + " · " + String(modelData.format || "").toUpperCase() + " · " +
                          (modelData.storage === "managed" ? qsTr("en proyecto") : qsTr("enlace externo")) +
                          (modelData.sourceFormat ? qsTr(" (desde %1)").arg(String(modelData.sourceFormat).toUpperCase()) : "") +
                          (modelData.available !== false ? "" : qsTr(" · falta el archivo"))
                    onClicked: root.forge.selectLibraryModel(index)
                }
                Button {
                    visible: isSelected && (modelData.format === "blend" || modelData.format === "fbx" || modelData.format === "obj" || !!modelData.sourcePath)
                    enabled: !root.forge.busy
                    text: modelData.storage === "managed" ? qsTr("Reconvertir") : qsTr("Vista previa GLB")
                    font.pixelSize: Theme.small
                    onClicked: root.convertPreviewRequested(true)
                }
            }
        }
    }
}
