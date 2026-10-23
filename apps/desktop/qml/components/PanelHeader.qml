import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

// Title row of a side panel: glyph, name, a short detail (a count, a state) and a chevron that hides the
// panel (the View menu, Ctrl+B / Ctrl+Alt+B and the header toggles bring it back). `trailing` takes one
// extra control, shown before the chevron.
Rectangle {
    id: header
    property string title
    property string detail: ""
    property string glyph: ""
    property bool collapsible: true
    property string side: "left"           // which edge the panel sits on: decides the chevron and the shortcut
    property Item trailing: null
    signal collapseRequested()

    implicitHeight: Theme.controlHeight + Theme.sp(4)
    color: Theme.panelHeader
    onTrailingChanged: if (trailing) { trailing.parent = trailingSlot }

    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.borderSubtle }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.sp(10)
        anchors.rightMargin: Theme.sp(4)
        spacing: Theme.sp(6)
        Icon {
            visible: header.glyph.length > 0
            name: header.glyph
            color: Theme.textDim
            Layout.alignment: Qt.AlignVCenter
        }
        Text {
            text: header.title
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: Theme.normal
            font.bold: true
            elide: Text.ElideRight
            Layout.fillWidth: true
            Accessible.role: Accessible.Heading
            Accessible.name: header.title
        }
        Text {
            visible: header.detail.length > 0
            text: header.detail
            color: Theme.textDim
            font.family: Theme.fontFamily
            font.pixelSize: Theme.small
            elide: Text.ElideRight
            Layout.maximumWidth: header.width * 0.45
        }
        Item {
            id: trailingSlot
            implicitWidth: header.trailing ? header.trailing.implicitWidth : 0
            implicitHeight: header.trailing ? header.trailing.implicitHeight : 0
            visible: header.trailing !== null
        }
        IconToolButton {
            visible: header.collapsible
            glyph: header.side === "left" ? "chevron-left" : "chevron-right"
            label: qsTr("Ocultar panel")
            showLabel: false
            shortcutText: header.side === "left" ? "Ctrl+B" : "Ctrl+Alt+B"
            onClicked: header.collapseRequested()
        }
    }
}
