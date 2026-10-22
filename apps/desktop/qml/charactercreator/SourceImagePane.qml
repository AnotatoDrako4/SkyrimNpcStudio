import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

// One side of the A/B view: the picture of one appearance source, drawn by the portrait stage with that source's
// FaceGen files (the same camera, lights and size for every source, so two pictures differ only by what they were made
// from), plus where it came from. Zoom and pan are owned by the dialog and shared by both sides. The picture is the
// editor's approximation; it is never a capture made inside Skyrim, and it says so.
Rectangle {
    id: pane
    required property AppearanceSources sources
    required property PortraitRenderer portraits
    required property string tag            // "A" or "B"
    required property string candidateId
    property real zoom: 1.0
    property real panX: 0
    property real panY: 0
    // The grid of several sources (3-4 at once): no origin line and no details block, only the picture and what the source is.
    property bool compact: false
    signal zoomRequested(real factor)
    signal panRequested(real dx, real dy)

    readonly property var candidate: {
        const list = sources.matrix.candidates || []
        for (let i = 0; i < list.length; ++i) if (list[i].id === candidateId) return list[i]
        return null
    }
    readonly property var image: sources.imageOf(candidateId, sources.imageRevision + portraits.revision)
    readonly property string state: image.state ?? "none"
    readonly property bool shown: (state === "ready" || ((image.origin ?? "") === "provisional" && (image.url ?? "") !== "")) && (image.url ?? "") !== ""
    readonly property real panLimit: Math.max(0, (zoom - 1) * Math.min(view.width, view.height) / 2)

    color: Theme.panel
    radius: Theme.radius
    border.color: Theme.border
    border.width: 1
    objectName: "sourcePane_" + tag
    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Fuente %1: %2").arg(tag).arg(candidate ? candidate.label : qsTr("ninguna"))

    function originText() {
        const origin = image.origin ?? ""
        return origin === "cache" ? qsTr("Render del estudio · caché verificada contra el perfil")
             : origin === "provisional" ? qsTr("Provisional: caché anterior, se está comprobando")
             : origin === "studio-render" ? qsTr("Render del estudio hecho ahora")
             : ""
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingSm
        spacing: Theme.spacingXs

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm
            Rectangle {
                implicitWidth: Theme.px(22)
                implicitHeight: Theme.px(22)
                radius: width / 2
                color: Theme.accent
                Text {
                    anchors.centerIn: parent
                    text: pane.tag
                    color: Theme.onAccent
                    font.bold: true
                    font.pixelSize: Theme.normal
                }
            }
            Text {
                objectName: "sourcePaneTitle_" + pane.tag
                Layout.fillWidth: true
                text: pane.candidate ? pane.candidate.label : qsTr("Sin fuente elegida")
                color: Theme.text
                font.bold: true
                font.pixelSize: Theme.normal
                elide: Text.ElideRight
            }
            SourceStatusBadge {
                visible: pane.candidate !== null
                status: pane.candidate ? pane.candidate.status : "MISSING"
            }
        }

        // The picture. Both sides zoom and pan together (wheel, drag; double click resets).
        Rectangle {
            id: view
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: Theme.px(120)
            color: Theme.surface
            radius: Theme.radiusSmall
            clip: true

            Image {
                id: picture
                objectName: "sourceImage_" + pane.tag
                visible: pane.shown
                width: view.width
                height: view.height
                x: Math.max(-pane.panLimit, Math.min(pane.panLimit, pane.panX))
                y: Math.max(-pane.panLimit, Math.min(pane.panLimit, pane.panY))
                scale: pane.zoom
                transformOrigin: Item.Center
                source: pane.shown ? pane.image.url : ""
                asynchronous: true
                fillMode: Image.PreserveAspectFit
                smooth: true
                mipmap: true
                opacity: (pane.image.origin ?? "") === "provisional" ? 0.82 : 1.0
            }
            Text {
                objectName: "sourceImageState_" + pane.tag
                anchors.centerIn: parent
                width: parent.width - Theme.px(16)
                visible: !pane.shown
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: pane.state === "error" ? Theme.warn : Theme.textMuted
                font.pixelSize: Theme.small
                text: pane.candidate === null ? qsTr("Elige una fuente para este lado")
                    : pane.state === "preparing" ? qsTr("Comprobando los archivos de la fuente…")
                    : pane.state === "queued" || pane.state === "rendering" ? qsTr("Generando la cabeza con los archivos de esta fuente…")
                    : pane.state === "unavailable" ? qsTr("No se puede dibujar: %1").arg(pane.image.reason || pane.candidate.renderNote)
                    : pane.state === "error" ? qsTr("Sin imagen: %1").arg(pane.image.reason || "?")
                    : qsTr("Sin imagen todavía")
            }
            MouseArea {
                id: drag
                anchors.fill: parent
                property point last
                hoverEnabled: false
                cursorShape: pane.zoom > 1 ? Qt.OpenHandCursor : Qt.ArrowCursor
                onPressed: mouse => last = Qt.point(mouse.x, mouse.y)
                onPositionChanged: mouse => {
                    if (!pressed) return
                    pane.panRequested(mouse.x - last.x, mouse.y - last.y)
                    last = Qt.point(mouse.x, mouse.y)
                }
                onWheel: wheel => pane.zoomRequested(wheel.angleDelta.y > 0 ? 1.15 : 1 / 1.15)
                onDoubleClicked: pane.zoomRequested(0)
            }
            Rectangle {
                visible: pane.shown && (pane.image.missing || []).length > 0
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.margins: 2
                width: missingText.implicitWidth + 8
                height: missingText.implicitHeight + 3
                radius: 2
                color: Theme.warn
                Text {
                    id: missingText
                    anchors.centerIn: parent
                    text: qsTr("%1 ausente(s)").arg((pane.image.missing || []).length)
                    color: Theme.bg
                    font.pixelSize: Theme.micro
                }
            }
        }

        Text {
            objectName: "sourceOrigin_" + pane.tag
            Layout.fillWidth: true
            visible: pane.shown && !pane.compact
            text: pane.originText() + ((pane.image.createdAt ?? "") !== "" ? " · " + (pane.image.createdAt || "").replace("T", " ").substring(0, 16) : "")
                  + (pane.image.renderMs > 0 ? qsTr(" · %1 ms").arg(pane.image.renderMs) : "")
            color: Theme.textDim
            font.pixelSize: Theme.micro
            wrapMode: Text.WordWrap
        }

        // Where each part comes from, and why the verdict is what it is.
        ScrollView {
            id: details
            Layout.fillWidth: true
            visible: !pane.compact
            Layout.preferredHeight: pane.compact ? 0 : Theme.px(92)
            clip: true
            contentWidth: availableWidth
            Text {
                width: details.availableWidth
                objectName: "sourceDetails_" + pane.tag
                color: Theme.textDim
                font.pixelSize: Theme.micro
                wrapMode: Text.WrapAnywhere
                text: {
                    const c = pane.candidate
                    if (!c) return ""
                    let t = qsTr("Registro: %1 (%2)").arg(c.recordPlugin || "?").arg(c.recordMod || "?")
                    t += "\n" + qsTr("FaceGen: «%1» · tinte: «%2»").arg(c.facegenMod || qsTr("ninguno")).arg(c.facetintMod || qsTr("ninguno"))
                    if (c.geometrySha256) t += "\n" + qsTr("SHA-256 geometría %1… (%2 KB)").arg(c.geometrySha256.substring(0, 12)).arg(Math.round(c.geometryBytes / 1024))
                    if (c.tintSha256) t += "\n" + qsTr("SHA-256 tinte %1… (%2 KB)").arg(c.tintSha256.substring(0, 12)).arg(Math.round(c.tintBytes / 1024))
                    for (let i = 0; i < Math.min((c.messages || []).length, 3); ++i) t += "\n· " + c.codes[i] + ": " + c.messages[i]
                    t += "\n" + c.renderNote
                    return t
                }
            }
        }
    }
}
