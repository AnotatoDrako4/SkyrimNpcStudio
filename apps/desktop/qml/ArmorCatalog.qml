import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

// Clothing and armor catalog (winning ARMO records of the profile). Double-click adds to the outfit.
ColumnLayout {
    id: root
    required property StudioController studio
    spacing: 6

    readonly property var slotOptions: [
        { text: qsTr("Todos los slots"), value: 0 }, { text: "30 · " + qsTr("Cabeza"), value: 30 },
        { text: "31 · " + qsTr("Pelo"), value: 31 }, { text: "32 · " + qsTr("Cuerpo"), value: 32 },
        { text: "33 · " + qsTr("Manos"), value: 33 }, { text: "34 · " + qsTr("Antebrazos"), value: 34 },
        { text: "35 · " + qsTr("Amuleto"), value: 35 }, { text: "36 · " + qsTr("Anillo"), value: 36 },
        { text: "37 · " + qsTr("Pies"), value: 37 }, { text: "38 · " + qsTr("Pantorrillas"), value: 38 },
        { text: "39 · " + qsTr("Escudo"), value: 39 }, { text: "42 · " + qsTr("Diadema"), value: 42 },
        { text: "46 · " + qsTr("Capa (convención)"), value: 46 }, { text: "52 · " + qsTr("Ropa interior (convención)"), value: 52 }
    ]

    function search() { studio.searchArmors(searchField.text, slotBox.currentValue, pluginField.text) }
    Timer { id: debounce; interval: 200; onTriggered: root.search() }

    ColumnLayout {
        Layout.fillWidth: true
        Layout.margins: 6
        spacing: 4
        TextField {
            id: searchField
            Layout.fillWidth: true
            placeholderText: qsTr("Nombre, EditorID o FormKey")
            enabled: root.studio.phase === StudioController.Ready
            onTextChanged: debounce.restart()
        }
        RowLayout {
            Layout.fillWidth: true
            ComboBox {
                id: slotBox
                Layout.fillWidth: true
                textRole: "text"
                valueRole: "value"
                model: root.slotOptions
                onActivated: root.search()
            }
        }
        TextField {
            id: pluginField
            Layout.fillWidth: true
            placeholderText: qsTr("Plugin (p. ej. Obi's Crimson Outfit.esp)")
            onTextChanged: debounce.restart()
        }
        Text {
            text: qsTr("%1 piezas · doble clic para añadir al atuendo de %2").arg(root.studio.armorTotal)
                  .arg(root.studio.outfit.npcLabel.length > 0 ? root.studio.outfit.npcLabel.split(" (")[0] : qsTr("(ningún NPC)"))
            color: Theme.textDim
            font.pixelSize: Theme.small
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
    }

    ListView {
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        model: root.studio.armorResults
        ScrollBar.vertical: ScrollBar {}
        delegate: ItemDelegate {
            id: item
            required property int index
            required property string formKey
            required property string name
            required property string editorId
            required property string winningPlugin
            required property string winningMod
            required property string slots
            required property string armorType
            required property bool nonPlayable
            width: ListView.view.width
            onDoubleClicked: root.studio.outfit.addPiece(root.studio.armorResults.get(index), true)
            background: Rectangle {
                color: parent.highlighted ? Theme.accentDim : parent.hovered ? Theme.panelAlt : "transparent"
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border; opacity: 0.5 }
            }
            ToolTip.visible: hovered
            ToolTip.delay: 600
            ToolTip.text: item.formKey + "\n" + qsTr("Proveedor: ") + item.winningMod + "\n" + qsTr("Doble clic: añadir (sustituye piezas del mismo slot)")
            contentItem: ColumnLayout {
                spacing: 1
                Text {
                    text: (item.name.length > 0 ? item.name : item.editorId) + (item.nonPlayable ? qsTr("  (no jugable)") : "")
                    color: Theme.text
                    font.pixelSize: Theme.normal
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Text {
                    text: qsTr("slots ") + item.slots + " · " + item.armorType + " · " + item.winningPlugin
                    color: Theme.textDim
                    font.pixelSize: Theme.small
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                }
            }
        }
    }
}
