import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

ForgeEditorSection {
    id: root
    Layout.fillWidth: true
    Repeater {
        model: [
            {
                field: "level",
                label: qsTr("Nivel"),
                min: 1,
                max: 32767
            },
            {
                field: "health",
                label: qsTr("Salud base"),
                min: 1,
                max: 65535
            },
            {
                field: "magicka",
                label: qsTr("Magia base"),
                min: 0,
                max: 65535
            },
            {
                field: "stamina",
                label: qsTr("Aguante base"),
                min: 0,
                max: 65535
            }
        ]
        RowLayout {
            required property var modelData
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                text: modelData.label
                color: Theme.textDim
            }
            SpinBox {
                from: modelData.min
                to: modelData.max
                value: root.forge.npc[modelData.field] || 0
                editable: true
                onValueModified: root.set(modelData.field, value)
            }
        }
    }
    CheckBox {
        text: qsTr("Escalar con el jugador")
        checked: root.forge.npc.levelMultiplier !== null && root.forge.npc.levelMultiplier !== undefined
        onClicked: root.set("levelMultiplier", checked ? 1 : null)
    }
    RowLayout {
        visible: root.forge.npc.levelMultiplier !== null && root.forge.npc.levelMultiplier !== undefined
        TextField {
            Layout.fillWidth: true
            text: String(root.forge.npc.levelMultiplier || 1)
            placeholderText: qsTr("Multiplicador")
            validator: DoubleValidator {
                bottom: 0.01
                top: 10
                locale: "C"
            }
            onEditingFinished: root.set("levelMultiplier", Number(text))
        }
        SpinBox {
            from: 1
            to: 32767
            value: root.forge.npc.minLevel || 1
            onValueModified: root.set("minLevel", value)
        }
        SpinBox {
            from: 1
            to: 32767
            value: root.forge.npc.maxLevel || 100
            onValueModified: root.set("maxLevel", value)
        }
    }
    ForgeRecordSelector {
        Layout.fillWidth: true
        forge: root.forge
        kind: "class"
        title: qsTr("Clase")
        value: root.forge.npc.classFormKey || ""
        onSelected: record => root.set("classFormKey", record.formKey)
    }
    ForgeRecordSelector {
        Layout.fillWidth: true
        forge: root.forge
        kind: "combatStyle"
        title: qsTr("Estilo de combate (opcional)")
        value: root.forge.npc.combatStyleFormKey || ""
        onSelected: record => root.set("combatStyleFormKey", record.formKey)
    }
    Button {
        text: qsTr("Quitar estilo de combate")
        enabled: !!root.forge.npc.combatStyleFormKey
        onClicked: root.set("combatStyleFormKey", "")
    }
    Text {
        text: qsTr("Habilidades")
        color: Theme.textDim
    }
    RowLayout {
        ComboBox {
            id: skill
            model: ["OneHanded", "TwoHanded", "Archery", "Block", "Smithing", "HeavyArmor", "LightArmor", "Pickpocket", "Lockpicking", "Sneak", "Alchemy", "Speech", "Alteration", "Conjuration", "Destruction", "Illusion", "Restoration", "Enchanting"]
        }
        SpinBox {
            id: skillValue
            from: 0
            to: 100
            value: 15
        }
        Button {
            text: qsTr("Asignar")
            onClicked: {
                let skills = Object.assign({}, root.forge.npc.skills);
                skills[skill.currentText] = skillValue.value;
                root.set("skills", skills);
            }
        }
    }
    Text {
        Layout.fillWidth: true
        text: JSON.stringify(root.forge.npc.skills || {})
        color: Theme.textMuted
        wrapMode: Text.WrapAnywhere
    }
    ForgeRecordSelector {
        Layout.fillWidth: true
        forge: root.forge
        kind: "faction"
        title: qsTr("Agregar facción (rango 0)")
        onSelected: record => root.append("factions", {
                formKey: record.formKey,
                rank: 0
            })
    }
    Repeater {
        model: root.forge.npc.factions || []
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
                from: -1
                to: 127
                value: modelData.rank
                onValueModified: {
                    let list = root.forge.npc.factions.slice();
                    list[index] = {
                        formKey: modelData.formKey,
                        rank: value
                    };
                    root.set("factions", list);
                }
            }
            Button {
                text: "×"
                onClicked: root.remove("factions", index)
            }
        }
    }
    ForgeRecordSelector {
        Layout.fillWidth: true
        forge: root.forge
        kind: "package"
        title: qsTr("Agregar paquete IA existente")
        onSelected: record => root.append("packageFormKeys", record.formKey)
    }
    Repeater {
        model: root.forge.npc.packageFormKeys || []
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
                onClicked: root.remove("packageFormKeys", index)
            }
        }
    }
}
