import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

// What the studio checked on the NPC's face and what it protects: the compatibility diagnosis of
// AppearanceEditor (winning FaceGen, head part meshes and TRI files, regeneration from the record)
// and the result of the last FaceGen build. States are written out, never shown by colour alone.
Rectangle {
    id: root
    required property StudioController studio
    readonly property var appearance: studio.appearance
    readonly property var compat: appearance.faceCompatibility
    readonly property var report: appearance.faceGen
    readonly property var provenance: compat.provenance || ({})
    property bool expanded: false

    function blocked(op) {
        return (root.compat.blocked || []).some(b => b.op === op);
    }

    visible: appearance.loaded
    color: Theme.panel
    radius: 4
    border.color: Theme.border
    border.width: 1
    implicitHeight: column.implicitHeight + 16

    ColumnLayout {
        id: column
        anchors.fill: parent
        anchors.margins: 8
        spacing: 5

        // Collapsed by default: one line with the verdict and the details toggle, so the palette and
        // the sliders keep the space they have without this card. Only a provenance conflict (the
        // winning ESP and the FaceGen come from different mods) is always visible.
        RowLayout {
            Layout.fillWidth: true
            Text {
                text: qsTr("Protección facial")
                color: Theme.text
                font.bold: true
                font.pixelSize: Theme.normal
            }
            Text {
                text: root.appearance.analysing ? qsTr("Analizando…") : (root.compat.label || qsTr("Sin diagnóstico"))
                color: Theme.textDim
                font.pixelSize: Theme.small
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Button {
                visible: root.compat.code !== undefined
                text: root.expanded ? qsTr("Ocultar") : qsTr("Detalles")
                flat: true
                onClicked: root.expanded = !root.expanded
            }
        }

        Text {
            Layout.fillWidth: true
            visible: (root.provenance.conflict || "").length > 0
            text: "⚠ " + (root.provenance.conflict || "")
            color: Theme.warn
            font.pixelSize: Theme.small
            wrapMode: Text.WordWrap
        }

        ColumnLayout {
            Layout.fillWidth: true
            visible: root.expanded || root.appearance.analysing
            spacing: 5

            Text {
                Layout.fillWidth: true
                visible: root.appearance.analysing
                text: qsTr("Comparando el FaceGen que carga el perfil con el registro del NPC. Las ediciones se activan al terminar.")
                color: Theme.textDim
                font.pixelSize: Theme.small
                wrapMode: Text.WordWrap
            }

            Repeater {
                model: root.appearance.analysing ? [] : (root.compat.allowed || [])
                delegate: Text {
                    required property var modelData
                    Layout.fillWidth: true
                    text: "✓ " + modelData.label + qsTr(" — permitida")
                    color: Theme.text
                    font.pixelSize: Theme.small
                    wrapMode: Text.WordWrap
                }
            }
            Repeater {
                model: root.appearance.analysing ? [] : (root.compat.blocked || [])
                delegate: Text {
                    required property var modelData
                    Layout.fillWidth: true
                    text: "✕ " + modelData.label + qsTr(" — bloqueada: ") + modelData.reason
                    color: Theme.warn
                    font.pixelSize: Theme.small
                    wrapMode: Text.WordWrap
                }
            }

            Text {
                Layout.fillWidth: true
                visible: root.report.kind === "hair-safe"
                text: root.report.integrity === "hair-tint-only"
                    ? qsTr("Hair Color Safe Mode: %1 pieza(s) recoloreada(s) (%2). El resto del FaceGen ganador es idéntico byte a byte.")
                        .arg((root.report.recoloredShapes || []).length).arg((root.report.recoloredShapes || []).join(", "))
                    : qsTr("Hair Color Safe Mode no aplicó el color: revisa el panel de problemas. El FaceGen original no se modificó.")
                color: root.report.integrity === "hair-tint-only" ? Theme.text : Theme.warn
                font.pixelSize: Theme.small
                wrapMode: Text.WordWrap
            }

            Text {
                Layout.fillWidth: true
                visible: !root.appearance.analysing && root.compat.code !== undefined
                text: qsTr("⚠ Pendiente de verificación en Skyrim: la exportación se comprueba en archivos, no dentro del juego.")
                color: Theme.textDim
                font.pixelSize: Theme.small
                wrapMode: Text.WordWrap
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            visible: root.expanded
            spacing: 3
            Text {
                Layout.fillWidth: true
                text: qsTr("Registro ganador: %1 (mod «%2»)").arg(root.provenance.winningPlugin || "?").arg(root.provenance.pluginProvider || "?")
                color: Theme.textDim
                font.pixelSize: Theme.micro
                wrapMode: Text.WrapAnywhere
            }
            Text {
                Layout.fillWidth: true
                text: root.provenance.faceGen && root.provenance.faceGen.provider
                    ? qsTr("FaceGen: %1 (%2)").arg(root.provenance.faceGen.provider).arg(root.provenance.faceGen.kind)
                    : qsTr("FaceGen: no existe en el perfil")
                color: Theme.textDim
                font.pixelSize: Theme.micro
                wrapMode: Text.WrapAnywhere
            }
            Text {
                Layout.fillWidth: true
                text: root.provenance.faceTint && root.provenance.faceTint.provider
                    ? qsTr("FaceTint: %1 (%2)").arg(root.provenance.faceTint.provider).arg(root.provenance.faceTint.kind)
                    : qsTr("FaceTint: no existe en el perfil")
                color: Theme.textDim
                font.pixelSize: Theme.micro
                wrapMode: Text.WrapAnywhere
            }
            Repeater {
                model: root.compat.evidence || []
                delegate: Text {
                    required property var modelData
                    Layout.fillWidth: true
                    text: "· " + modelData
                    color: Theme.textMuted
                    font.pixelSize: Theme.micro
                    wrapMode: Text.WrapAnywhere
                }
            }
        }
    }
}
