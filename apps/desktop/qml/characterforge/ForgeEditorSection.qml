import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

ColumnLayout {
    required property CharacterForgeViewModel forge
    enabled: forge.hasProject && !forge.busy
    function recordLabel(key) {
        return (forge.workspace.recordLabels || {})[key] || qsTr("Referencia de %1").arg(key.split(":").pop());
    }
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
            weight: forge.npc.weight === undefined ? 50 : forge.npc.weight,
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
}
