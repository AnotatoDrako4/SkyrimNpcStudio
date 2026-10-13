import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

ColumnLayout {
    id: root
    required property CharacterForgeViewModel forge
    function set(field, value) {
        forge.setNpcValue(field, value);
    }
    function append(field, value) {
        let entries = (forge.npc[field] || []).slice();
        entries.push(value);
        set(field, entries);
    }
    function remove(field, index) {
        let entries = (forge.npc[field] || []).slice();
        entries.splice(index, 1);
        set(field, entries);
    }
    function face() {
        return forge.npc.face || {
            sliders: Array(18).fill(0),
            presets: [-1, -1, -1, -1],
            weight: forge.npc.weight || 50,
            skinTone: {
                r: 255,
                g: 255,
                b: 255,
                a: 255
            },
            tints: []
        };
    }
    function setMorph(index, value) {
        let f = JSON.parse(JSON.stringify(face()));
        f.sliders[index] = value;
        f.weight = forge.npc.weight;
        set("face", f);
    }
    enabled: forge.hasProject && !forge.busy
    TabBar {
        id: tabs
        Layout.fillWidth: true
        TabButton {
            text: qsTr("Identidad")
        }
        TabButton {
            text: qsTr("Datos")
        }
        TabButton {
            text: qsTr("Rostro")
        }
        TabButton {
            text: qsTr("Equipo")
        }
        TabButton {
            text: qsTr("Lugar")
        }
    }
    ScrollView {
        Layout.fillWidth: true
        Layout.fillHeight: true
        contentWidth: availableWidth
        ColumnLayout {
            width: parent.width
            spacing: 12
            ColumnLayout {
                visible: tabs.currentIndex === 0
                Layout.fillWidth: true
                Text {
                    text: qsTr("Nombre del personaje")
                    color: Theme.textDim
                }
                TextField {
                    Layout.fillWidth: true
                    text: root.forge.characterName
                    onEditingFinished: root.forge.setCharacterName(text)
                }
                Text {
                    text: "EditorID"
                    color: Theme.textDim
                }
                TextField {
                    Layout.fillWidth: true
                    text: root.forge.editorId
                    placeholderText: "SNS_MiPersonaje"
                    onEditingFinished: root.forge.setEditorId(text)
                }
                ForgeRecordSelector {
                    Layout.fillWidth: true
                    forge: root.forge
                    kind: "race"
                    title: qsTr("Raza")
                    value: root.forge.npc.raceFormKey || ""
                    onSelected: record => root.set("raceFormKey", record.formKey)
                }
                Text {
                    text: qsTr("Sexo")
                    color: Theme.textDim
                }
                ComboBox {
                    Layout.fillWidth: true
                    model: [qsTr("Masculino"), qsTr("Femenino")]
                    currentIndex: root.forge.npc.female ? 1 : 0
                    onActivated: root.set("female", currentIndex === 1)
                }
                ForgeRecordSelector {
                    Layout.fillWidth: true
                    forge: root.forge
                    kind: "voice"
                    title: qsTr("Voz")
                    value: root.forge.npc.voiceFormKey || ""
                    onSelected: record => root.set("voiceFormKey", record.formKey)
                }
                Text {
                    text: qsTr("Altura (0.5–2)")
                    color: Theme.textDim
                }
                TextField {
                    Layout.fillWidth: true
                    text: String(root.forge.npc.height || 1)
                    validator: DoubleValidator {
                        bottom: 0.5
                        top: 2
                        locale: "C"
                    }
                    onEditingFinished: root.set("height", Number(text))
                }
                Text {
                    text: qsTr("Peso (0–100)")
                    color: Theme.textDim
                }
                SpinBox {
                    from: 0
                    to: 100
                    value: root.forge.npc.weight === undefined ? 50 : root.forge.npc.weight
                    onValueModified: {
                        root.set("weight", value);
                        if (root.forge.npc.face) {
                            let f = JSON.parse(JSON.stringify(root.forge.npc.face));
                            f.weight = value;
                            root.set("face", f);
                        }
                    }
                }
            }
            ColumnLayout {
                visible: tabs.currentIndex === 1
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
                            text: modelData.formKey
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
                            text: modelData
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
            ColumnLayout {
                visible: tabs.currentIndex === 2
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Configuración del registro facial. FaceGen geométrico pendiente; no hay vista previa certificada.")
                    color: Theme.textDim
                    wrapMode: Text.WordWrap
                }
                ForgeRecordSelector {
                    Layout.fillWidth: true
                    forge: root.forge
                    kind: "headPart"
                    title: qsTr("Agregar cabeza, pelo, ojos o cejas")
                    onSelected: record => root.append("headPartFormKeys", record.formKey)
                }
                Repeater {
                    model: root.forge.npc.headPartFormKeys || []
                    RowLayout {
                        required property var modelData
                        required property int index
                        Text {
                            Layout.fillWidth: true
                            text: modelData
                            color: Theme.textDim
                            elide: Text.ElideRight
                        }
                        Button {
                            text: "×"
                            onClicked: root.remove("headPartFormKeys", index)
                        }
                    }
                }
                ForgeRecordSelector {
                    Layout.fillWidth: true
                    forge: root.forge
                    kind: "hairColor"
                    title: qsTr("Color de pelo")
                    value: root.forge.npc.hairColorFormKey || ""
                    onSelected: record => root.set("hairColorFormKey", record.formKey)
                }
                ForgeRecordSelector {
                    Layout.fillWidth: true
                    forge: root.forge
                    kind: "headTexture"
                    title: qsTr("Textura de cabeza")
                    value: root.forge.npc.headTextureFormKey || ""
                    onSelected: record => root.set("headTextureFormKey", record.formKey)
                }
                Text {
                    text: qsTr("Morphs del registro (-1–1)")
                    color: Theme.textDim
                }
                Repeater {
                    model: [qsTr("Nariz longitud"), qsTr("Nariz altura"), qsTr("Mandíbula altura"), qsTr("Mandíbula anchura"), qsTr("Mandíbula profundidad"), qsTr("Mejillas altura"), qsTr("Mejillas profundidad"), qsTr("Ojos altura"), qsTr("Ojos separación"), qsTr("Cejas altura"), qsTr("Cejas separación"), qsTr("Cejas profundidad"), qsTr("Labios altura"), qsTr("Labios profundidad"), qsTr("Mentón anchura"), qsTr("Mentón altura"), qsTr("Mentón mordida"), qsTr("Ojos profundidad")]
                    ColumnLayout {
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        Text {
                            text: modelData
                            color: Theme.textDim
                        }
                        Slider {
                            Layout.fillWidth: true
                            from: -1
                            to: 1
                            value: root.face().sliders[index]
                            onPressedChanged: if (!pressed)
                                root.setMorph(index, value)
                        }
                    }
                }
                Text {
                    text: qsTr("Presets de nariz, ojos y boca")
                    color: Theme.textDim
                }
                Repeater {
                    model: [0, 2, 3]
                    SpinBox {
                        required property var modelData
                        from: -1
                        to: 65535
                        value: root.face().presets[modelData]
                        onValueModified: {
                            let f = JSON.parse(JSON.stringify(root.face()));
                            f.presets[modelData] = value;
                            root.set("face", f);
                        }
                    }
                }
                Text {
                    text: qsTr("Tono de piel (RGB)")
                    color: Theme.textDim
                }
                RowLayout {
                    Repeater {
                        model: ["r", "g", "b"]
                        SpinBox {
                            required property var modelData
                            from: 0
                            to: 255
                            value: root.face().skinTone[modelData]
                            onValueModified: {
                                let f = JSON.parse(JSON.stringify(root.face()));
                                f.skinTone[modelData] = value;
                                root.set("face", f);
                            }
                        }
                    }
                }
                Text {
                    text: qsTr("Tint mask del registro (índice / preset)")
                    color: Theme.textDim
                }
                RowLayout {
                    SpinBox {
                        id: tintIndex
                        from: 0
                        to: 65535
                        value: 24
                    }
                    SpinBox {
                        id: tintPreset
                        from: -32768
                        to: 32767
                    }
                    Button {
                        text: qsTr("Agregar")
                        onClicked: {
                            let f = JSON.parse(JSON.stringify(root.face()));
                            f.tints.push({
                                index: tintIndex.value,
                                preset: tintPreset.value,
                                value: 1,
                                color: Object.assign({}, f.skinTone)
                            });
                            root.set("face", f);
                        }
                    }
                }
                Repeater {
                    model: root.face().tints
                    RowLayout {
                        required property var modelData
                        required property int index
                        Text {
                            text: qsTr("Máscara %1, preset %2").arg(modelData.index).arg(modelData.preset)
                            color: Theme.textDim
                        }
                        Button {
                            text: "×"
                            onClicked: {
                                let f = JSON.parse(JSON.stringify(root.face()));
                                f.tints.splice(index, 1);
                                root.set("face", f);
                            }
                        }
                    }
                }
            }
            ColumnLayout {
                visible: tabs.currentIndex === 3
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
                            text: modelData
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
                            text: modelData.formKey
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
            ColumnLayout {
                visible: tabs.currentIndex === 4
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Sin ubicación no se genera ACHR. Solo se admite una celda interior seleccionada explícitamente; coordenadas en unidades del juego y giro en radianes.")
                    color: Theme.textDim
                    wrapMode: Text.WordWrap
                }
                ForgeRecordSelector {
                    Layout.fillWidth: true
                    forge: root.forge
                    kind: "cell"
                    title: qsTr("Celda interior (opcional)")
                    value: root.forge.npc.placement ? root.forge.npc.placement.cellFormKey : ""
                    onSelected: record => root.set("placement", {
                            cellFormKey: record.formKey,
                            x: 0,
                            y: 0,
                            z: 0,
                            rotationZ: 0
                        })
                }
                Button {
                    text: qsTr("Sin ubicación")
                    enabled: !!root.forge.npc.placement
                    onClicked: root.set("placement", null)
                }
                Repeater {
                    model: ["x", "y", "z", "rotationZ"]
                    TextField {
                        required property var modelData
                        Layout.fillWidth: true
                        enabled: !!root.forge.npc.placement
                        placeholderText: modelData
                        text: root.forge.npc.placement ? String(root.forge.npc.placement[modelData]) : ""
                        validator: DoubleValidator {
                            locale: "C"
                        }
                        onEditingFinished: {
                            if (!root.forge.npc.placement)
                                return;
                            let p = Object.assign({}, root.forge.npc.placement);
                            p[modelData] = Number(text);
                            root.set("placement", p);
                        }
                    }
                }
            }
        }
    }
}
