import QtQuick
import QtQuick.Controls
import SkyrimNpcStudio

// The portrait of one NPC for a card (NPC gallery, donor gallery): a render of the studio from the profile's own
// resources. While it is being verified or redone, the render stored in an earlier session is shown at once,
// marked "verificando" / "rehaciendo" (Configuración › NPC y retratos). Without either it says why there is none:
// never a stand-in face, never presented as a capture made inside Skyrim.
Rectangle {
    id: thumb
    required property PortraitRenderer portraits
    required property string formKey
    readonly property var portrait: portraits.stateOf(formKey, portraits.revision)
    readonly property bool ready: portrait.state === "ready"
    readonly property bool provisional: !ready && (portrait.provisionalUrl ?? "") !== ""
    objectName: "npcCardPortrait"
    color: Theme.surface
    radius: 3

    // The visible cards ask for their portrait; a card scrolled away gives its turn back.
    Component.onCompleted: if (formKey.length > 0) portraits.request(formKey)
    Component.onDestruction: if (formKey.length > 0) portraits.release(formKey)

    Image {
        objectName: "npcCardPortraitImage"
        anchors.fill: parent
        anchors.margins: 1
        visible: thumb.ready || thumb.provisional
        source: thumb.ready ? thumb.portrait.url : thumb.provisional ? thumb.portrait.provisionalUrl : ""
        asynchronous: true
        fillMode: Image.PreserveAspectFit
        sourceSize: Qt.size(256, 256)
        opacity: thumb.provisional ? 0.82 : 1.0
    }
    // Shown from the cache while it is checked against the profile (or redone): said on the card, in words.
    Rectangle {
        objectName: "npcCardProvisionalBadge"
        visible: thumb.provisional
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 2
        width: provisionalLabel.implicitWidth + 6
        height: provisionalLabel.implicitHeight + 2
        radius: 2
        color: Theme.panelHeader
        border.color: Theme.border
        Text {
            id: provisionalLabel
            anchors.centerIn: parent
            text: thumb.portrait.state === "rendering" ? qsTr("rehaciendo…") : qsTr("verificando…")
            color: Theme.textDim
            font.pixelSize: Theme.micro
        }
        ToolTip.visible: provisionalHover.hovered
        ToolTip.text: qsTr("Retrato guardado el %1; se está comprobando contra el perfil actual y se reemplaza si cambió algo.")
                      .arg((thumb.portrait.provisionalCreatedAt || "").replace("T", " ").substring(0, 16))
        HoverHandler { id: provisionalHover }
    }
    // Drawn without some of its files (e.g. a base head texture the profile lacks): said on the card.
    Rectangle {
        objectName: "npcCardMissingBadge"
        readonly property int missing: (thumb.portrait.missing || []).length
        visible: thumb.ready && missing > 0
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 2
        width: missingLabel.implicitWidth + 6
        height: missingLabel.implicitHeight + 2
        radius: 2
        color: Theme.warn
        Text {
            id: missingLabel
            anchors.centerIn: parent
            text: qsTr("%1 ausente(s)").arg(parent.missing)
            color: Theme.bg
            font.pixelSize: Theme.micro
        }
    }
    // No portrait: a short diagnostic code and one line on the card; the renderer's own reason, unedited, in the
    // tooltip. The code only sorts the reason the renderer gave (it never decides anything by itself).
    readonly property string reason: thumb.portrait.reason || ""
    readonly property var diagnosis: reason.indexOf("Sin cabeza") === 0 ? ["NO_HEAD", qsTr("Sin cabeza visible")]
        : (reason.indexOf("Template") >= 0 || reason.indexOf("plantilla") >= 0) ? ["TEMPLATE", qsTr("Hereda de una plantilla")]
        : reason.indexOf("fuera de pantalla") >= 0 ? ["OFFSCREEN", qsTr("Render no disponible")]
        : ["ERROR", qsTr("No se pudo dibujar")]
    Column {
        objectName: "npcCardPortraitPlaceholder"
        anchors.centerIn: parent
        width: parent.width - 12
        spacing: Theme.sp(4)
        visible: !thumb.ready && !thumb.provisional
        Icon {
            anchors.horizontalCenter: parent.horizontalCenter
            name: thumb.portrait.state === "error" ? "warning" : thumb.portrait.state === "rendering" ? "refresh" : "person"
            size: Theme.title
            color: thumb.portrait.state === "error" ? Theme.warn : Theme.textMuted
        }
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            visible: thumb.portrait.state === "error"
            text: thumb.diagnosis[0]
            color: Theme.warn
            font.family: Theme.mono
            font.pixelSize: Theme.small
            font.bold: true
        }
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: thumb.portrait.state === "queued" ? qsTr("Retrato en cola")
                : thumb.portrait.state === "rendering" ? qsTr("Generando retrato…")
                : thumb.portrait.state === "error" ? thumb.diagnosis[1]
                : qsTr("Sin retrato")
            color: Theme.textDim
            font.pixelSize: Theme.small
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }
    }
    ToolTip.visible: placeholderHover.hovered && thumb.portrait.state === "error"
    ToolTip.delay: 500
    ToolTip.text: qsTr("Sin retrato: %1").arg(thumb.reason || "?")
    HoverHandler { id: placeholderHover }
}
