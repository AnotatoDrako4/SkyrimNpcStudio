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
        ToolTip.visible: hovered && !enabled
        ToolTip.text: !root.forge.hasProject ? qsTr("Abre o crea un proyecto primero.")
                    : root.forge.workerMessage.length ? qsTr("El worker no está disponible: %1").arg(root.forge.workerMessage)
                    : !root.forge.catalogReady ? qsTr("Indexa el perfil de MO2 activo para buscar registros.")
                    : qsTr("Hay una operación en curso.")
    }
    Dialog {
        id: picker
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Theme.px(500)
        height: 460
        modal: true
        title: root.title
        standardButtons: Dialog.Close
        ColumnLayout {
            anchors.fill: parent
            TextField {
                id: searchField
                objectName: "recordSearchField"
                Layout.fillWidth: true
                placeholderText: qsTr("Buscar por EditorID en el perfil indexado")
                onTextEdited: searchDelay.restart()
                Timer {
                    id: searchDelay
                    interval: 250
                    onTriggered: root.forge.searchRecords(root.kind, searchField.text)
                }
            }
            // Loading, 0 results, unindexed profile and a refused kind are told apart (forge.recordSearch).
            Text {
                objectName: "recordSearchStatus"
                Layout.fillWidth: true
                readonly property var s: root.forge.recordSearch
                text: s.state === "loading" ? qsTr("Buscando…")
                    : s.state === "error" ? (s.error || qsTr("No se pudo buscar."))
                    : s.state === "ready" && s.count === 0 ? (searchField.text.length ? qsTr("Sin resultados para «%1» en el perfil indexado.").arg(searchField.text) : qsTr("El perfil indexado no tiene registros de este tipo."))
                    : s.state === "ready" ? qsTr("%1 resultados").arg(s.count) : ""
                color: s.state === "error" ? Theme.error : Theme.textDim
                wrapMode: Text.WordWrap
                visible: text.length > 0
            }
            ListView {
                objectName: "recordChoicesList"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: root.forge.recordChoices
                delegate: ItemDelegate {
                    required property var modelData
                    width: ListView.view.width
                    // Different records can share an EditorID (the same spell from two mods): the FormKey tells them apart.
                    text: modelData.editorId + "    " + modelData.formKey
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
