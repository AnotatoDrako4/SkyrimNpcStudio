import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

// Center area: the selected NPC's effective record, override history, conflicts and FaceGen status.
Rectangle {
    id: root
    required property StudioController studio
    readonly property var npc: studio.npc
    readonly property var summary: npc.summary ?? ({})
    readonly property bool hasNpc: summary.formKey !== undefined
    color: Theme.bg

    function refText(ref) {
        if (!ref) return ""
        return (ref.name ? ref.name + " · " : "") + (ref.editorId ?? "") + "  [" + ref.formKey + "]"
               + (ref.resolved === false ? qsTr("  (no resuelto)") : "")
    }

    function locationText(locs) {
        if (!locs || locs.length === 0) return qsTr("Sin referencia en el mundo (no colocado o dinámico)")
        return locs.map(l => {
            const cell = l.cellName ? l.cellName + (l.cellEditorId ? " (" + l.cellEditorId + ")" : "") : (l.cellEditorId ?? l.cellFormKey ?? "")
            return cell + " [" + l.formKey + " · " + l.plugin + "]"
        }).join("; ")
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Text {
            visible: !root.hasNpc
            text: root.studio.npcLoading ? qsTr("Cargando NPC…") : qsTr("Selecciona un NPC en el explorador de la izquierda.")
            color: Theme.textDim
            font.pixelSize: Theme.normal
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 40
        }

        ScrollView {
            visible: root.hasNpc
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth

            ColumnLayout {
                width: parent.width
                spacing: 6

                // ---- identity
                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: 10
                    ColumnLayout {
                        spacing: 2
                        Text { text: root.summary.name || qsTr("(sin nombre)"); color: Theme.text; font.pixelSize: Theme.fs(22); font.bold: true }
                        Text {
                            text: (root.summary.editorId ?? "") + "   " + (root.summary.formKey ?? "") + "   FormID " + (root.summary.formId ?? "")
                            color: Theme.textDim
                            font.pixelSize: Theme.small
                            font.family: Theme.mono
                        }
                    }
                    Item { Layout.fillWidth: true }
                    StatusBadge { visible: root.summary.isUnique === true; text: qsTr("ÚNICO"); tone: Theme.accent }
                    StatusBadge { visible: root.summary.usesTemplate === true; text: qsTr("PLANTILLA"); tone: Theme.warn }
                    StatusBadge {
                        visible: (root.npc.conflicts ?? []).length > 0
                        text: (root.npc.conflicts ?? []).length + qsTr(" CONFLICTOS")
                        tone: Theme.warn
                    }
                }

                GridLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 10
                    Layout.rightMargin: 10
                    columns: 2
                    columnSpacing: 20
                    KeyValue { label: qsTr("Raza"); value: root.refText(root.npc.race) }
                    KeyValue { label: qsTr("Sexo"); value: root.summary.sex === "female" ? qsTr("Mujer") : qsTr("Hombre") }
                    KeyValue { label: qsTr("Plugin de origen"); value: root.summary.originPlugin ?? "" }
                    KeyValue { label: qsTr("Plugin ganador"); value: (root.summary.winningPlugin ?? "") + "  [" + root.studio.providerOf(root.summary.winningPlugin ?? "") + "]"; valueColor: Theme.accent }
                    KeyValue { label: qsTr("Clase"); value: root.refText(root.npc.class) }
                    KeyValue { label: qsTr("Voz"); value: root.refText(root.npc.voice) }
                    KeyValue { label: qsTr("Altura / peso"); value: (root.npc.height ?? 0).toFixed(2) + " / " + (root.npc.weight ?? 0).toFixed(0) }
                    KeyValue { label: qsTr("Piel (WNAM)"); value: root.refText(root.npc.wornArmor) }
                    KeyValue { label: qsTr("Plantilla"); value: root.refText(root.npc.template) }
                    KeyValue { label: qsTr("Flags"); value: (root.npc.flags ?? []).join(", ") }
                    KeyValue {
                        label: qsTr("Ubicación")
                        value: root.locationText(root.npc.locations)
                        valueColor: (root.npc.locations && root.npc.locations.length > 0) ? Theme.text : Theme.textDim
                    }
                }

                // ---- FaceGen
                SectionHeader { title: qsTr("FaceGen"); detail: qsTr("archivos nombrados por el plugin de ORIGEN") }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 10
                    Layout.rightMargin: 10
                    KeyValue {
                        label: qsTr("Geometría")
                        mono: true
                        value: (root.npc.faceGen?.geometryPath ?? "") + "  →  "
                               + (root.npc.faceGenGeometry?.found ? root.npc.faceGenGeometry.provider : qsTr("no encontrada como archivo suelto (¿BSA?)"))
                        valueColor: root.npc.faceGenGeometry?.found ? Theme.text : Theme.warn
                    }
                    KeyValue {
                        label: qsTr("Tinte")
                        mono: true
                        value: (root.npc.faceGen?.tintPath ?? "") + "  →  "
                               + (root.npc.faceGenTint?.found ? root.npc.faceGenTint.provider : qsTr("no encontrado como archivo suelto (¿BSA?)"))
                        valueColor: root.npc.faceGenTint?.found ? Theme.text : Theme.warn
                    }
                    // Record/FaceGen coherence from the appearance control map (Character Creator: «Origen»).
                    Repeater {
                        model: (root.studio.appearanceControl.findings ?? []).filter(f => f.severity === "warning")
                        delegate: Text {
                            required property var modelData
                            text: modelData.message + " " + modelData.suggestion
                            color: Theme.warn
                            wrapMode: Text.Wrap
                            font.pixelSize: Theme.small
                            Layout.fillWidth: true
                        }
                    }
                }

                // ---- HeadParts
                SectionHeader { title: qsTr("Partes de cabeza (HeadParts)"); detail: (root.npc.headParts ?? []).length + qsTr(" partes") }
                Repeater {
                    model: root.npc.headParts ?? []
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.leftMargin: 10
                        Layout.rightMargin: 10
                        Text { text: modelData.type ?? ""; color: Theme.accent; font.pixelSize: Theme.small; Layout.preferredWidth: 80 }
                        Text { text: (modelData.editorId || modelData.formKey) ?? ""; color: Theme.text; font.pixelSize: Theme.small; Layout.fillWidth: true }
                        Text { text: "[" + modelData.formKey + "]"; color: Theme.textDim; font.family: Theme.mono; font.pixelSize: Theme.small }
                    }
                }

                // ---- override history
                SectionHeader { title: qsTr("Historial de overrides"); detail: (root.npc.overrides ?? []).length + qsTr(" versiones") }
                Repeater {
                    model: root.npc.overrides ?? []
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.leftMargin: 10
                        Layout.rightMargin: 10
                        Text { text: modelData.loadIndex; color: Theme.textDim; font.family: Theme.mono; font.pixelSize: Theme.small; Layout.preferredWidth: 30 }
                        Text {
                            text: modelData.plugin + (modelData.mod ? "  [" + modelData.mod + "]" : "")
                            color: modelData.isWinner ? Theme.accent : Theme.text
                            font.bold: modelData.isWinner
                            font.pixelSize: Theme.small
                            Layout.preferredWidth: 360
                            elide: Text.ElideRight
                        }
                        Text {
                            text: modelData.isOrigin ? qsTr("origen") : modelData.changedFields.join(", ")
                            color: Theme.textDim
                            font.pixelSize: Theme.small
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
                    }
                }

                // ---- conflicts
                SectionHeader { title: qsTr("Conflictos (ediciones perdidas)"); detail: qsTr("un mod intermedio cambió el campo y el ganador no conserva su valor") }
                Text {
                    visible: (root.npc.conflicts ?? []).length === 0
                    text: qsTr("Ninguno: el registro ganador conserva todos los cambios de los overrides anteriores.")
                    color: Theme.ok
                    font.pixelSize: Theme.small
                    Layout.leftMargin: 10
                }
                Repeater {
                    model: root.npc.conflicts ?? []
                    delegate: KeyValue {
                        required property var modelData
                        Layout.leftMargin: 10
                        Layout.rightMargin: 10
                        label: modelData.field
                        value: qsTr("perdido de ") + modelData.losingPlugins.join(", ") + qsTr("  · gana ") + modelData.winningPlugin
                        valueColor: Theme.warn
                    }
                }

                SectionHeader { title: qsTr("Notas") }
                IssueList {
                    Layout.leftMargin: 10
                    Layout.rightMargin: 10
                    Layout.bottomMargin: 10
                    issues: root.npc.notes ?? []
                }
            }
        }
    }
}
