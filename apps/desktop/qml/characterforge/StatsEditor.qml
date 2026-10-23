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

    // --- ARQUETIPOS DE SEGUIDOR (AG04) ---
    // Everything here comes from the indexed profile (creation.getBehaviorCatalog / creation.applyArchetype). Each control is enabled only
    // when it can act, and says why when it cannot.
    readonly property var catalog: forge.behaviorCatalog
    readonly property var preview: forge.archetypePreview
    readonly property var archetypes: catalog.archetypes || []
    readonly property var selectedArchetype: archetypeCombo.currentIndex >= 0 && archetypeCombo.currentIndex < archetypes.length ? archetypes[archetypeCombo.currentIndex] : null
    readonly property bool behaviorReady: forge.hasProject && forge.catalogReady && forge.behaviorSupported
    readonly property var categoryIds: ["class", "combatStyle", "stats", "skills", "equipment", "spells", "perks", "factions", "packages"]
    readonly property var categoryLabels: ({
        "class": qsTr("Clase"), "combatStyle": qsTr("Estilo de combate"), "stats": qsTr("Estadísticas"), "skills": qsTr("Habilidades"),
        "equipment": qsTr("Equipo"), "spells": qsTr("Hechizos"), "perks": qsTr("Perks"), "factions": qsTr("Facciones"), "packages": qsTr("Paquetes IA")
    })
    property var chosenCategories: categoryIds.slice()
    readonly property string archetypeBlockedReason: {
        if (!forge.hasProject) return qsTr("Abre o crea un proyecto de Character Forge para usar arquetipos.");
        if (!forge.behaviorSupported) return qsTr("Este worker no incluye los arquetipos: actualiza la aplicación.");
        if (forge.workerMessage.length) return qsTr("El worker no está disponible: %1").arg(forge.workerMessage);
        if (!forge.catalogReady) return qsTr("Indexa el perfil de MO2 activo para cargar los arquetipos.");
        if (catalog.state === "loading") return qsTr("Cargando los arquetipos del perfil…");
        if (catalog.state === "error") return catalog.reason || qsTr("No se pudo cargar el catálogo de arquetipos.");
        if (archetypes.length === 0) return qsTr("El perfil indexado no ofrece arquetipos.");
        if (!selectedArchetype) return qsTr("Elige un arquetipo.");
        if (chosenCategories.length === 0) return qsTr("Marca al menos una categoría.");
        if (forge.behaviorBusy) return qsTr("Calculando…");
        return "";
    }
    readonly property string applyBlockedReason: {
        if (archetypeBlockedReason.length) return archetypeBlockedReason;
        if (selectedArchetype && selectedArchetype.allLinksResolved === false)
            return qsTr("A este arquetipo le faltan registros en el perfil activo (clase, estilo, hechizos o perks): no se puede aplicar.");
        return "";
    }
    function categoriesArg() { return chosenCategories.length === categoryIds.length ? [] : chosenCategories; }
    function loadCatalogIfNeeded() { if (behaviorReady && (catalog.state === undefined || catalog.state === "none")) forge.loadBehaviorCatalog(); }
    // Deferred: loading the catalog changes forge.behaviorCatalog, and doing that inside the notification that reads it is a binding loop.
    onBehaviorReadyChanged: Qt.callLater(loadCatalogIfNeeded)
    Component.onCompleted: Qt.callLater(loadCatalogIfNeeded)
    Connections {
        target: root.forge
        function onBehaviorChanged() { Qt.callLater(root.loadCatalogIfNeeded); }
    }

    Text {
        text: qsTr("Plantilla de Arquetipo de Seguidor")
        color: Theme.textDim
        font.bold: true
    }

    // The combo takes the full width and the two actions sit under it: side by side they need more than a narrow side panel offers.
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 6

        ComboBox {
            id: archetypeCombo
            objectName: "archetypeCombo"
            Layout.fillWidth: true
            enabled: root.behaviorReady && root.archetypes.length > 0
            model: root.archetypes
            textRole: "displayName"
            valueRole: "id"
            displayText: root.archetypes.length === 0 ? qsTr("(sin arquetipos)") : currentText
            onActivated: root.forge.discardArchetypePreview()
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Button {
                objectName: "archetypePreviewButton"
                Layout.fillWidth: true
                text: qsTr("Vista previa")
                enabled: root.archetypeBlockedReason.length === 0
                onClicked: root.forge.previewArchetype(archetypeCombo.currentValue, root.categoriesArg())
            }

            Button {
                objectName: "archetypeApplyButton"
                Layout.fillWidth: true
                text: qsTr("Aplicar")
                enabled: root.applyBlockedReason.length === 0
                onClicked: root.forge.applyArchetype(archetypeCombo.currentValue, root.categoriesArg())
            }
        }
    }

    Text {
        Layout.fillWidth: true
        visible: !!root.selectedArchetype && root.selectedArchetype.roleSummary
        text: root.selectedArchetype ? root.selectedArchetype.roleSummary : ""
        color: Theme.textDim
        font.pixelSize: 11
        wrapMode: Text.WordWrap
    }

    Flow {
        Layout.fillWidth: true
        spacing: 6
        enabled: root.behaviorReady
        Repeater {
            model: root.categoryIds
            CheckBox {
                required property string modelData
                text: root.categoryLabels[modelData]
                checked: root.chosenCategories.indexOf(modelData) >= 0
                onToggled: {
                    const next = root.chosenCategories.filter(c => c !== modelData);
                    if (checked) next.push(modelData);
                    root.chosenCategories = next;
                    root.forge.discardArchetypePreview();
                }
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        Button {
            objectName: "archetypeUndoButton"
            text: qsTr("Deshacer")
            enabled: root.forge.canUndo && !root.forge.behaviorBusy
            onClicked: root.forge.undo()
        }
        Button {
            objectName: "archetypeRedoButton"
            text: qsTr("Rehacer")
            enabled: root.forge.canRedo && !root.forge.behaviorBusy
            onClicked: root.forge.redo()
        }
        BusyIndicator {
            running: root.forge.behaviorBusy || root.catalog.state === "loading"
            visible: running
            implicitHeight: 24
            implicitWidth: 24
        }
        Text {
            Layout.fillWidth: true
            text: root.applyBlockedReason.length ? root.applyBlockedReason : root.archetypeBlockedReason
            visible: text.length > 0 && !root.forge.behaviorBusy
            color: Theme.textMuted
            font.pixelSize: 11
            wrapMode: Text.WordWrap
            objectName: "archetypeBlockedReason"
        }
    }

    // Vista previa / resultado. Un preview nunca toca el borrador: "Aplicar" vuelve a pedir el cálculo y lo valida antes de editar.
    ColumnLayout {
        objectName: "archetypePreviewPanel"
        Layout.fillWidth: true
        visible: root.preview.state !== undefined && root.preview.state !== "idle"
        spacing: 2
        Text {
            objectName: "archetypePreviewState"
            Layout.fillWidth: true
            text: root.preview.message || ""
            color: root.preview.state === "error" || root.preview.state === "conflict" ? Theme.error
                 : root.preview.state === "stale" || root.preview.state === "notApplicable" || root.preview.state === "cancelled" ? Theme.warn
                 : root.preview.state === "applied" ? Theme.ok : Theme.text
            wrapMode: Text.WordWrap
            font.bold: true
        }
        Repeater {
            model: root.preview.unresolved || []
            Text {
                required property var modelData
                Layout.fillWidth: true
                text: qsTr("Falta %1 (%2): %3").arg(modelData.formKey).arg(modelData.type).arg(modelData.note)
                Layout.preferredWidth: 1
                Layout.maximumWidth: root.width
                color: Theme.error
                font.pixelSize: 11
                wrapMode: Text.WrapAnywhere
            }
        }
        Repeater {
            model: root.preview.changes || []
            Text {
                required property var modelData
                Layout.fillWidth: true
                text: "• " + modelData.category + " / " + modelData.field + ": " + modelData.before + " → " + modelData.after
                // Long values (lists of FormKeys) wrap inside the panel instead of widening it.
                Layout.preferredWidth: 1
                Layout.maximumWidth: root.width
                color: Theme.textDim
                font.pixelSize: 11
                wrapMode: Text.WrapAnywhere
            }
        }
        Text {
            Layout.fillWidth: true
            visible: !!root.preview.loadOrderFingerprint
            text: qsTr("Perfil: %1").arg(String(root.preview.loadOrderFingerprint || "").substring(0, 16))
            color: Theme.textMuted
            font.pixelSize: 10
        }
    }

    Text {
        Layout.fillWidth: true
        text: qsTr("Un arquetipo solo toca clase, estilo de combate, estadísticas, habilidades, equipo, hechizos, perks, facciones y paquetes. Rostro, peinado, texturas, identidad, voz, sexo y ubicación no cambian, y «Deshacer» lo revierte de una sola vez.")
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
            // Compressible: the row (combo + value + button) must not be wider than a narrow side panel.
            Layout.fillWidth: true
            Layout.minimumWidth: 60
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
        onSelected: record => {
            if ((root.forge.npc.spellFormKeys || []).indexOf(record.formKey) < 0) root.append("spellFormKeys", record.formKey);
        }
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
        onSelected: record => {
            if ((root.forge.npc.perkFormKeys || []).indexOf(record.formKey) < 0) root.append("perkFormKeys", record.formKey);
        }
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
