import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

ForgeEditorSection {
    id: root
    Layout.fillWidth: true
    ForgeRecordSelector {
        Layout.fillWidth: true
        forge: root.forge
        kind: "armor"
        title: qsTr("Agregar prenda al atuendo inicial")
        onSelected: record => root.append("outfitArmorFormKeys", record.formKey)
    }
    Repeater {
        model: root.forge.npc.outfitArmorFormKeys || []
        RowLayout {
            required property var modelData
            required property int index
            Text {
                Layout.fillWidth: true
                text: root.recordLabel(modelData)
                color: Theme.textDim
                elide: Text.ElideRight
            }
            Button {
                text: "×"
                onClicked: root.remove("outfitArmorFormKeys", index)
            }
        }
    }
    ForgeRecordSelector {
        Layout.fillWidth: true
        forge: root.forge
        kind: "item"
        title: qsTr("Agregar arma u objeto al inventario")
        onSelected: record => root.append("inventory", {
                formKey: record.formKey,
                count: 1
            })
    }
    Repeater {
        model: root.forge.npc.inventory || []
        RowLayout {
            required property var modelData
            required property int index
            Text {
                Layout.fillWidth: true
                text: root.recordLabel(modelData.formKey)
                color: Theme.textDim
                elide: Text.ElideRight
            }
            SpinBox {
                from: 1
                to: 100000
                value: modelData.count
                editable: true
                onValueModified: {
                    let list = root.forge.npc.inventory.slice();
                    list[index] = {
                        formKey: modelData.formKey,
                        count: value
                    };
                    root.set("inventory", list);
                }
            }
            Button {
                text: "×"
                onClicked: root.remove("inventory", index)
            }
        }
    }
    Text {
        Layout.fillWidth: true
        text: qsTr("El worker comprueba slots y ARMA. Las mallas y texturas seguirán siendo dependencias externas; no se copian al paquete.")
        color: Theme.textDim
        wrapMode: Text.WordWrap
    }
}
