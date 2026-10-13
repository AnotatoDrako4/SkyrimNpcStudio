import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

ColumnLayout {
    id: root
    required property CharacterForgeViewModel forge
    required property string kind
    required property string title
    property string value: ""
    signal selected(var record)
    Text {
        text: root.title
        color: Theme.textDim
    }
    Button {
        Layout.fillWidth: true
        text: root.value.length ? (root.forge.workspace.recordLabels || {})[root.value] || qsTr("Referencia de %1").arg(root.value.split(":").pop()) : qsTr("Seleccionar…")
        enabled: root.forge.hasProject && root.forge.catalogReady && !root.forge.busy
        onClicked: {
            root.forge.searchRecords(root.kind, "");
            picker.open();
        }
    }
    Dialog {
        id: picker
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 500
        height: 460
        modal: true
        title: root.title
        standardButtons: Dialog.Close
        ColumnLayout {
            anchors.fill: parent
            TextField {
                Layout.fillWidth: true
                placeholderText: qsTr("Buscar por EditorID en el perfil indexado")
                onTextEdited: searchDelay.restart()
                Timer {
                    id: searchDelay
                    interval: 250
                    onTriggered: root.forge.searchRecords(root.kind, parent.text)
                }
            }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: root.forge.recordChoices
                delegate: ItemDelegate {
                    required property var modelData
                    width: ListView.view.width
                    text: modelData.editorId
                    onClicked: {
                        let labels = Object.assign({}, root.forge.workspace.recordLabels || {});
                        labels[modelData.formKey] = modelData.editorId;
                        root.forge.setWorkspaceValue("recordLabels", labels);
                        root.selected(modelData);
                        picker.close();
                    }
                    ToolTip.visible: hovered
                    ToolTip.text: modelData.formKey
                }
            }
            Text {
                text: qsTr("Hasta 100 resultados. Refina la búsqueda si hace falta.")
                color: Theme.textMuted
            }
        }
    }
}
