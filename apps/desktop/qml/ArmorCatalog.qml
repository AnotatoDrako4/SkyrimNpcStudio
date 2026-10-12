import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

// Clothing and armor visual gallery (winning ARMO records of the profile).
ColumnLayout {
    id: root
    required property StudioController studio
    signal previewRequested(string formKey)
    spacing: 6

    readonly property var slotOptions: [
        { text: qsTr("Todos los slots"), value: 0 },
        { text: "30 · " + qsTr("Cabeza"), value: 30 },
        { text: "31 · " + qsTr("Pelo"), value: 31 },
        { text: "32 · " + qsTr("Cuerpo"), value: 32 },
        { text: "33 · " + qsTr("Manos"), value: 33 },
        { text: "34 · " + qsTr("Antebrazos"), value: 34 },
        { text: "35 · " + qsTr("Amuleto"), value: 35 },
        { text: "36 · " + qsTr("Anillo"), value: 36 },
        { text: "37 · " + qsTr("Pies"), value: 37 },
        { text: "38 · " + qsTr("Pantorrillas"), value: 38 },
        { text: "39 · " + qsTr("Escudo"), value: 39 },
        { text: "42 · " + qsTr("Diadema"), value: 42 },
        { text: "46 · " + qsTr("Capa (convención)"), value: 46 },
        { text: "52 · " + qsTr("Ropa interior (convención)"), value: 52 }
    ]

    readonly property var typeOptions: [
        { text: qsTr("Todos los tipos"), value: "" },
        { text: qsTr("Ropa / Túnica"), value: "Clothing" },
        { text: qsTr("Armadura Ligera"), value: "LightArmor" },
        { text: qsTr("Armadura Pesada"), value: "HeavyArmor" }
    ]

    function search() {
        studio.searchArmors(searchField.text, slotBox.currentValue, pluginField.text, typeBox.currentValue, smpCheck.checked)
    }
    Timer { id: debounce; interval: 200; onTriggered: root.search() }

    ColumnLayout {
        Layout.fillWidth: true
        Layout.margins: 6
        spacing: 4

        TextField {
            id: searchField
            Layout.fillWidth: true
            placeholderText: qsTr("🔍 Buscar nombre, EditorID o FormKey")
            enabled: root.studio.phase === StudioController.Ready
            onTextChanged: debounce.restart()
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            ComboBox {
                id: slotBox
                Layout.fillWidth: true
                textRole: "text"
                valueRole: "value"
                model: root.slotOptions
                onActivated: root.search()
            }
            ComboBox {
                id: typeBox
                Layout.fillWidth: true
                textRole: "text"
                valueRole: "value"
                model: root.typeOptions
                onActivated: root.search()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            TextField {
                id: pluginField
                Layout.fillWidth: true
                placeholderText: qsTr("Filtrar por plugin/mod (p. ej. Crimson.esp)")
                onTextChanged: debounce.restart()
            }
            CheckBox {
                id: smpCheck
                text: qsTr("✦ SMP")
                checked: false
                onToggled: root.search()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Mostrar únicamente prendas con físicas HDT-SMP / FSMP")
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Text {
                text: qsTr("<b>%1</b> prendas encontradas").arg(root.studio.armorTotal)
                color: Theme.accent
                font.pixelSize: Theme.small
            }
            Item { Layout.fillWidth: true }
            Text {
                text: qsTr("Doble clic o [+] para equipar")
                color: Theme.textDim
                font.pixelSize: Theme.small - 1
            }
        }
    }

    ListView {
        id: armorListView
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        spacing: 4
        model: root.studio.armorResults
        ScrollBar.vertical: ScrollBar {}

        delegate: ItemDelegate {
            id: card
            required property int index
            required property string formKey
            required property string formId
            required property string name
            required property string editorId
            required property string winningPlugin
            required property string winningMod
            required property string slots
            required property string armorType
            required property bool nonPlayable
            property bool nifFound: (typeof model.nifFound !== "undefined") ? Boolean(model.nifFound) : false
            property bool hasSmp: (typeof model.hasSmp !== "undefined") ? Boolean(model.hasSmp) : false

            width: armorListView.width - 6
            x: 3
            padding: 8
            hoverEnabled: true

            background: Rectangle {
                radius: 4
                color: card.hovered ? Theme.panelAlt : Theme.panel
                border.color: card.hovered ? Theme.accent : Theme.border
                border.width: 1
            }

            onClicked: root.previewRequested(card.formKey)
            onDoubleClicked: root.studio.outfit.addPiece(root.studio.armorResults.get(card.index), true)

            contentItem: ColumnLayout {
                spacing: 5

                // Fila 1: Nombre, FormID y Badge SMP
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Text {
                        text: card.name && card.name.length > 0 ? card.name : card.editorId
                        color: Theme.text
                        font.pixelSize: Theme.normal
                        font.bold: true
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    Rectangle {
                        visible: card.hasSmp
                        implicitWidth: smpText.implicitWidth + 8
                        implicitHeight: 18
                        radius: 3
                        color: "#3d1f42"
                        border.color: "#c040d0"
                        Text {
                            id: smpText
                            anchors.centerIn: parent
                            text: qsTr("✦ HDT-SMP / FSMP")
                            color: "#e880f8"
                            font.pixelSize: 10
                            font.bold: true
                        }
                    }

                    Rectangle {
                        implicitWidth: formIdText.implicitWidth + 6
                        implicitHeight: 18
                        radius: 2
                        color: Theme.panelAlt
                        border.color: Theme.border
                        Text {
                            id: formIdText
                            anchors.centerIn: parent
                            text: card.formId && card.formId.length > 0 ? card.formId : (card.formKey ? card.formKey.split(":")[0] : "")
                            color: Theme.textDim
                            font.pixelSize: 10
                            font.family: "Consolas, monospace"
                        }
                    }
                }

                // Fila 2: Badges de Slot, Tipo y Origen MO2
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    Rectangle {
                        implicitWidth: slotTag.implicitWidth + 6
                        implicitHeight: 16
                        radius: 2
                        color: "#1c2a38"
                        border.color: "#305070"
                        Text {
                            id: slotTag
                            anchors.centerIn: parent
                            text: qsTr("Slot: ") + card.slots
                            color: "#80b8e8"
                            font.pixelSize: 10
                        }
                    }

                    Rectangle {
                        visible: card.armorType && card.armorType.length > 0
                        implicitWidth: typeTag.implicitWidth + 6
                        implicitHeight: 16
                        radius: 2
                        color: Theme.panelAlt
                        border.color: Theme.border
                        Text {
                            id: typeTag
                            anchors.centerIn: parent
                            text: card.armorType === "Clothing" ? qsTr("Ropa")
                                  : card.armorType === "LightArmor" ? qsTr("Ligera")
                                  : card.armorType === "HeavyArmor" ? qsTr("Pesada") : card.armorType
                            color: Theme.textDim
                            font.pixelSize: 10
                        }
                    }

                    Text {
                        text: "📦 " + (card.winningMod && card.winningMod.length > 0 ? card.winningMod : card.winningPlugin)
                        color: Theme.textDim
                        font.pixelSize: 10
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }
                }

                // Fila 3: Estado de archivos y botones de acción rápida
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Text {
                        text: card.nifFound ? qsTr("✓ NIF resuelto") : qsTr("⚠ NIF ausente")
                        color: card.nifFound ? Theme.ok : Theme.warn
                        font.pixelSize: 10
                    }

                    Text {
                        visible: card.nonPlayable
                        text: qsTr("· no jugable")
                        color: Theme.textDim
                        font.pixelSize: 10
                    }

                    Item { Layout.fillWidth: true }

                    Button {
                        text: "👁 " + qsTr("3D")
                        implicitHeight: 22
                        font.pixelSize: 11
                        onClicked: root.previewRequested(card.formKey)
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Inspeccionar prenda en el visor 3D")
                    }

                    Button {
                        text: "+ " + qsTr("Equipar")
                        highlighted: true
                        implicitHeight: 22
                        font.pixelSize: 11
                        onClicked: root.studio.outfit.addPiece(root.studio.armorResults.get(card.index), true)
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Añadir esta pieza al outfit del NPC")
                    }
                }
            }
        }
    }
}

