import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

// Head <-> body skin match of the NPC on view (studio.scene.skinMatch): the median color of the head's skin (FaceGen tint over the
// base head) against the body, hands and feet that the viewer draws, with the difference in delta E. It is an ESTIMATE of the seam
// at the neck made from the textures the studio composited, not what Skyrim draws; it never blocks anything and says so.
Rectangle {
    id: card
    objectName: "skinMatchCard"
    required property StudioController studio
    readonly property var match: studio.scene.skinMatch
    readonly property string verdict: match.verdict ?? ""
    readonly property bool ready: (match.state ?? "none") === "ready"

    implicitHeight: column.implicitHeight + 2 * Theme.spacingSm
    color: Theme.panel
    radius: Theme.radiusSmall
    border.color: Theme.border
    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Piel de la cabeza y del cuerpo")

    function toneOf(v) {
        return v === "CONSISTENT" ? Theme.ok : v === "NOTICEABLE" ? Theme.warn : v === "LIKELY_SEAM" ? Theme.error : Theme.textDim
    }
    function textOf(v) {
        return v === "CONSISTENT" ? qsTr("CONSISTENT · mismo color")
             : v === "NOTICEABLE" ? qsTr("NOTICEABLE · se nota")
             : v === "LIKELY_SEAM" ? qsTr("LIKELY_SEAM · costura probable")
             : qsTr("UNKNOWN · sin medir")
    }
    function roleText(r) {
        return r === "body" ? qsTr("Cuerpo") : r === "hands" ? qsTr("Manos / brazos") : r === "feet" ? qsTr("Pies") : r
    }

    ColumnLayout {
        id: column
        anchors.fill: parent
        anchors.margins: Theme.spacingSm
        spacing: Theme.spacingXs

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm
            Text {
                Layout.fillWidth: true
                text: qsTr("Piel de la cabeza y del cuerpo (estimación)")
                color: Theme.text
                font.bold: true
                font.pixelSize: Theme.small
                elide: Text.ElideRight
            }
            StatusBadge {
                objectName: "skinMatchVerdict"
                visible: card.ready
                text: card.textOf(card.verdict)
                tone: card.toneOf(card.verdict)
                Accessible.name: qsTr("Veredicto: %1").arg(text)
            }
        }

        Text {
            objectName: "skinMatchMessage"
            Layout.fillWidth: true
            text: card.ready ? (card.match.message ?? "") : qsTr("Sin escena cargada: no hay piel que medir.")
            color: Theme.text
            font.pixelSize: Theme.small
            wrapMode: Text.WordWrap
        }

        // Swatches: the head and each body part measured, with its difference from the head.
        Flow {
            Layout.fillWidth: true
            visible: card.ready && (card.match.head ?? {}).color !== undefined
            spacing: Theme.spacingSm
            Repeater {
                model: card.ready ? [{ role: "head", color: (card.match.head ?? {}).color ?? "#000000", deltaE: -1 }].concat(card.match.pairs ?? []) : []
                delegate: Row {
                    id: swatchRow
                    required property var modelData
                    spacing: 4
                    Rectangle {
                        width: Theme.px(26); height: Theme.px(18); radius: 3
                        color: swatchRow.modelData.color
                        border.color: Theme.border
                        Accessible.ignored: true
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        color: Theme.textDim
                        font.pixelSize: Theme.micro
                        text: swatchRow.modelData.role === "head" ? qsTr("Cabeza")
                              : card.roleText(swatchRow.modelData.role) + qsTr(" · ΔE %1").arg(swatchRow.modelData.deltaE.toFixed(1))
                    }
                }
            }
        }

        Text {
            Layout.fillWidth: true
            text: qsTr("Estimación a partir del color mediano de las texturas que dibuja el estudio (ΔE CIE76: ≤ %1 mismo color, ≤ %2 se nota, más: costura probable). "
                     + "No es lo que dibuja Skyrim y no se ha visto dentro del juego: no bloquea nada. Si la ropa cubre el cuerpo, no se mide.")
                      .arg(card.match.consistentMax ?? 6).arg(card.match.noticeableMax ?? 12)
            color: Theme.textMuted
            font.pixelSize: Theme.micro
            wrapMode: Text.WordWrap
        }
    }
}
