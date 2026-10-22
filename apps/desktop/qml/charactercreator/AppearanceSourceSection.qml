import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

// «Fuentes de apariencia» in the Origen tab: the Appearance Source Matrix of the selected NPC (studio.sources) as a
// list, with the button that opens the A/B comparison. Read-only, like the rest of the tab.
ColumnLayout {
    id: section
    objectName: "sourceSection"
    required property StudioController studio
    readonly property var matrix: studio.sources.matrix
    readonly property var candidates: matrix.candidates || []
    readonly property bool ready: matrix.state === "ready"
    spacing: Theme.spacingXs

    Text {
        Layout.fillWidth: true
        Layout.leftMargin: 8
        text: qsTr("Fuentes de apariencia")
        color: Theme.accent
        font.bold: true
        font.pixelSize: Theme.normal
    }
    Text {
        objectName: "sourceSectionState"
        Layout.fillWidth: true
        Layout.leftMargin: 8
        Layout.rightMargin: 8
        text: section.matrix.state === "gathering" ? qsTr("Reuniendo las fuentes del NPC…")
            : !section.ready ? qsTr("Selecciona un NPC para ver qué fuentes de apariencia ofrece el perfil.")
            : section.candidates.length <= 1 ? qsTr("Este NPC solo ofrece una fuente de apariencia: la que carga el juego.")
            : qsTr("%1 fuentes para el mismo NPC (%2 se pueden dibujar). Se leyeron %3 KB en %4 ms.")
                  .arg(section.candidates.length).arg(section.matrix.renderable).arg(Math.round((section.matrix.readBytes || 0) / 1024)).arg(section.matrix.gatherMs || 0)
        color: Theme.textDim
        font.pixelSize: Theme.micro
        wrapMode: Text.WordWrap
    }
    Repeater {
        model: section.ready ? section.candidates : []
        delegate: Rectangle {
            id: row
            required property var modelData
            objectName: "sourceRow_" + modelData.id
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            implicitHeight: rowColumn.implicitHeight + 10
            color: Theme.panel
            radius: Theme.radiusSmall
            border.color: Theme.border
            ColumnLayout {
                id: rowColumn
                anchors.fill: parent
                anchors.margins: 5
                spacing: 2
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: row.modelData.label
                        color: Theme.text
                        font.bold: true
                        font.pixelSize: Theme.small
                        elide: Text.ElideRight
                    }
                    SourceStatusBadge { status: row.modelData.status }
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Registro %1 (%2) · FaceGen «%3»").arg(row.modelData.recordPlugin || "?").arg(row.modelData.recordMod || "?").arg(row.modelData.facegenMod || qsTr("ninguno"))
                    color: Theme.textDim
                    font.pixelSize: Theme.micro
                    wrapMode: Text.WrapAnywhere
                }
                Text {
                    Layout.fillWidth: true
                    visible: (row.modelData.codes || []).length > 0
                    text: (row.modelData.codes || []).join(", ")
                    color: Theme.textMuted
                    font.pixelSize: Theme.micro
                    wrapMode: Text.WrapAnywhere
                }
                Text {
                    Layout.fillWidth: true
                    visible: !row.modelData.renderable
                    text: row.modelData.renderNote
                    color: Theme.warn
                    font.pixelSize: Theme.micro
                    wrapMode: Text.WordWrap
                }
            }
        }
    }
    Text {
        Layout.fillWidth: true
        Layout.leftMargin: 8
        Layout.rightMargin: 8
        visible: section.ready && (section.matrix.skipped || []).length > 0
        text: qsTr("No se leyeron: ") + (section.matrix.skipped || []).join("; ")
        color: Theme.warn
        font.pixelSize: Theme.micro
        wrapMode: Text.WordWrap
    }
    Button {
        objectName: "sourceCompareOpen"
        Layout.leftMargin: 8
        text: qsTr("Comparar A/B…")
        enabled: section.ready && section.candidates.length >= 2
        Accessible.name: qsTr("Abrir la comparación A/B de las fuentes de apariencia")
        onClicked: compare.open()
    }
    AppearanceSourceCompareDialog {
        id: compare
        studio: section.studio
    }
}
