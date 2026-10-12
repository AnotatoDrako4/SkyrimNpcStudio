import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

// What the current outfit draft depends on: plugins, the MO2 mods that provide them, and head parts.
ScrollView {
    id: root
    required property StudioController studio
    clip: true
    contentWidth: availableWidth

    readonly property var plugins: {
        const set = {}
        for (const piece of studio.outfit.pieces) set[piece.plugin] = true
        const winner = studio.npc.summary?.winningPlugin
        if (winner) set[winner] = true
        return Object.keys(set).sort()
    }

    ColumnLayout {
        width: root.availableWidth
        spacing: 6

        SectionHeader { title: qsTr("Plugins requeridos por el atuendo"); detail: qsTr("modo dependiente: no se copian recursos") }
        Repeater {
            model: root.plugins
            delegate: KeyValue {
                required property string modelData
                Layout.leftMargin: 8
                Layout.rightMargin: 8
                label: modelData
                value: root.studio.providerOf(modelData) || qsTr("no está en el load order")
                valueColor: root.studio.providerOf(modelData) ? Theme.text : Theme.error
            }
        }
        Text {
            Layout.margins: 8
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.textDim
            font.pixelSize: Theme.small
            text: qsTr("Los masters definitivos los calcula el worker al exportar: incluyen además los plugins referenciados por el "
                       + "registro ganador del NPC (por ejemplo su pelo o cabeza), para no perder cambios de otros mods.")
        }

        SectionHeader { title: qsTr("Partes de la cabeza (registro ganador)") }
        Repeater {
            model: root.studio.npc.headParts ?? []
            delegate: KeyValue {
                required property var modelData
                Layout.leftMargin: 8
                Layout.rightMargin: 8
                label: modelData.partType ?? "?"
                value: (modelData.record.editorId ?? modelData.record.formKey) + "  [" + (modelData.record.winningPlugin ?? "?") + "]"
                       + (modelData.modelPath ? "\n" + modelData.modelPath : "")
            }
        }

        SectionHeader { title: qsTr("Facciones") }
        Repeater {
            model: root.studio.npc.factions ?? []
            delegate: KeyValue {
                required property var modelData
                Layout.leftMargin: 8
                Layout.rightMargin: 8
                label: qsTr("rango ") + modelData.rank
                value: (modelData.faction.name ? modelData.faction.name + " · " : "") + (modelData.faction.editorId ?? modelData.faction.formKey)
            }
        }
    }
}
