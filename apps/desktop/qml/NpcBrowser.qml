import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

// NPC explorer: instant search by name, EditorID, FormKey or xEdit FormID, with filters.
ColumnLayout {
    id: root
    required property StudioController studio
    spacing: 6

    function search() {
        studio.searchNpcs(searchField.text, sexBox.currentValue, kindBox.currentValue, pluginField.text)
    }

    Timer { id: debounce; interval: 200; onTriggered: root.search() }
    Connections {
        target: root.studio
        function onLoaded() { root.search() }
    }

    ColumnLayout {
        Layout.fillWidth: true
        Layout.margins: 6
        spacing: 4
        TextField {
            id: searchField
            Layout.fillWidth: true
            placeholderText: qsTr("Nombre, EditorID, FormKey (0A2C8E:Skyrim.esm) o FormID (000A2C8E)")
            enabled: root.studio.phase === StudioController.Ready
            onTextChanged: debounce.restart()
        }
        RowLayout {
            Layout.fillWidth: true
            ComboBox {
                id: sexBox
                Layout.fillWidth: true
                textRole: "text"
                valueRole: "value"
                model: [{ text: qsTr("Sexo: todos"), value: "" }, { text: qsTr("Mujeres"), value: "female" }, { text: qsTr("Hombres"), value: "male" }]
                onActivated: root.search()
            }
            ComboBox {
                id: kindBox
                Layout.fillWidth: true
                textRole: "text"
                valueRole: "value"
                model: [{ text: qsTr("Tipo: todos"), value: "" }, { text: qsTr("Únicos"), value: "unique" },
                        { text: qsTr("Genéricos"), value: "generic" }, { text: qsTr("Con plantilla"), value: "templated" },
                        { text: qsTr("Presets de creación"), value: "preset" }]
                onActivated: root.search()
            }
        }
        TextField {
            id: pluginField
            Layout.fillWidth: true
            placeholderText: qsTr("Plugin que define o modifica (p. ej. Helen_Lydia_replacer.esp)")
            onTextChanged: debounce.restart()
        }
        Text {
            text: root.studio.npcTotal > list.count ? qsTr("%1 resultados (mostrando %2)").arg(root.studio.npcTotal).arg(list.count)
                                                    : qsTr("%1 resultados").arg(root.studio.npcTotal)
            color: Theme.textDim
            font.pixelSize: Theme.small
        }
    }

    ListView {
        id: list
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        model: root.studio.npcResults
        ScrollBar.vertical: ScrollBar {}
        delegate: ItemDelegate {
            id: item
            required property int index
            required property string formKey
            required property string formId
            required property string name
            required property string editorId
            required property string race
            required property string sex
            required property string winningPlugin
            required property string winningMod
            required property int overrideCount
            width: ListView.view.width
            highlighted: root.studio.npc.summary !== undefined && root.studio.npc.summary.formKey === formKey
            onClicked: root.studio.selectNpc(formKey)
            background: Rectangle {
                color: parent.highlighted ? Theme.accentDim : parent.hovered ? Theme.panelAlt : "transparent"
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border; opacity: 0.5 }
            }
            contentItem: ColumnLayout {
                spacing: 1
                RowLayout {
                    Text {
                        text: (item.name.trim().length > 0 ? item.name : qsTr("(sin nombre)")) + "  "
                        color: Theme.text
                        font.pixelSize: Theme.normal
                        font.bold: true
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    StatusBadge {
                        visible: item.overrideCount > 1
                        text: item.overrideCount + "×"
                        tone: item.overrideCount > 2 ? Theme.warn : Theme.accent
                    }
                }
                Text {
                    text: item.editorId + " · " + item.formId + " · " + (item.race || "?") + " · " + (item.sex === "female" ? "♀" : "♂")
                    color: Theme.textDim
                    font.pixelSize: Theme.small
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Text {
                    text: qsTr("gana: ") + item.winningPlugin + (item.winningMod ? "  [" + item.winningMod + "]" : "")
                    color: Theme.textDim
                    font.pixelSize: Theme.small - 1
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                }
            }
        }
    }
}
