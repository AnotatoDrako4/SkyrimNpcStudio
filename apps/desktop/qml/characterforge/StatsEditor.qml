import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

// StatsEditor.qml
// Character Forge stats and behavior editor with Follower Behavior Studio integration.
// Reuses ForgeRecordSelector, ComboBox, SpinBox, and root.forge without duplicating NPC state.
ForgeEditorSection {
    id: root
    Layout.fillWidth: true

    // --- ARQUETIPOS DE SEGUIDOR (INTEGRACIÓN PROFESIONAL) ---
    Text {
        text: qsTr("Plantilla de Arquetipo de Seguidor")
        color: Theme.textDim
        font.bold: true
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8

        ComboBox {
            id: archetypeCombo
            Layout.fillWidth: true
            model: [
                { id: "defensive-warrior", text: qsTr("Guerrero Defensivo (1H, Escudo, Armadura Pesada)") },
                { id: "archer",            text: qsTr("Arquero (Arco, Evasión, Armadura Ligera)") },
                { id: "assassin",          text: qsTr("Asesino (Dagas dobles, Sigilo, Invisibilidad)") },
                { id: "destruction-mage",  text: qsTr("Mago Destructor (Fuego/Hielo, Túnica, Armadura mágica)") },
                { id: "healer",            text: qsTr("Sanador (Restauración, Curación rápida, Custodia)") },
                { id: "spellsword",        text: qsTr("Guerrero-Mago (1H, Destrucción, Armadura)") }
            ]
            textRole: "text"
            valueRole: "id"
        }

        Button {
            text: qsTr("Vista previa")
            enabled: root.forge.hasProject && !root.forge.busy
            onClicked: {
                if (root.forge.previewArchetype) {
                    root.forge.previewArchetype(archetypeCombo.currentValue);
                }
            }
        }

        Button {
            text: qsTr("Aplicar")
            enabled: root.forge.hasProject && !root.forge.busy
            onClicked: {
                if (root.forge.applyArchetype) {
                    root.forge.applyArchetype(archetypeCombo.currentValue);
                }
            }
        }
    }

    Text {
        Layout.fillWidth: true
        text: qsTr("La aplicación de arquetipos preserva al 100% el rostro, peinado, texturas, identidad y ubicación del personaje.")
        color: Theme.textMuted
        font.pixelSize: 11
        wrapMode: Text.WordWrap
    }

    // --- ESTADÍSTICAS BÁSICAS ---
    Repeater {
        model: [
            { field: "level",   label: qsTr("Nivel"),        min: 1, max: 32767 },
            { field: "health",  label: qsTr("Salud base"),   min: 1, max: 65535 },
            { field: "magicka", label: qsTr("Magia base"),   min: 0, max: 65535 },
            { field: "stamina", label: qsTr("Aguante base"), min: 0, max: 65535 }
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

    // --- CLASE Y ESTILO DE COMBATE ---
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

    // --- HABILIDADES ---
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

    // --- FACCIONES ---
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

    // --- PAQUETES IA ---
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

    // --- HECHIZOS (INTEGRACIÓN ARQUETIPOS) ---
    ForgeRecordSelector {
        Layout.fillWidth: true
        forge: root.forge
        kind: "spell"
        title: qsTr("Agregar hechizo / conjuro existente")
        onSelected: record => root.append("spellFormKeys", record.formKey)
    }

    Repeater {
        model: root.forge.npc.spellFormKeys || []
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
                onClicked: root.remove("spellFormKeys", index)
            }
        }
    }

    // --- PERKS (INTEGRACIÓN ARQUETIPOS) ---
    ForgeRecordSelector {
        Layout.fillWidth: true
        forge: root.forge
        kind: "perk"
        title: qsTr("Agregar perk / beneficio existente")
        onSelected: record => root.append("perkFormKeys", record.formKey)
    }

    Repeater {
        model: root.forge.npc.perkFormKeys || []
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
                onClicked: root.remove("perkFormKeys", index)
            }
        }
    }
}
