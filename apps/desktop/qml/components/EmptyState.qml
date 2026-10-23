import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

// What a viewer or list shows when there is nothing to show yet: a glyph, a short title, one line of
// explanation and the real next steps as buttons. It never invents content; every action calls an
// existing flow (`actions: [{ label, glyph, enabled, tip, trigger: function }]`).
Item {
    id: state
    property string glyph: "info"
    property string title: ""
    property string message: ""
    property string note: ""          // small print under the actions (a requirement, a state)
    property var actions: []
    implicitWidth: card.implicitWidth
    implicitHeight: card.implicitHeight

    Rectangle {
        id: card
        anchors.centerIn: parent
        width: Math.min(parent.width - Theme.sp(32), Theme.px(460))
        implicitHeight: column.implicitHeight + Theme.sp(40)
        implicitWidth: Theme.px(460)
        radius: Theme.radiusLarge
        color: Qt.rgba(Theme.panel.r, Theme.panel.g, Theme.panel.b, 0.85)
        border.color: Theme.border

        ColumnLayout {
            id: column
            anchors.centerIn: parent
            width: parent.width - Theme.sp(40)
            spacing: Theme.sp(10)
            Icon {
                name: state.glyph
                size: Theme.heading + Theme.px(10)
                color: Theme.accent
                Layout.alignment: Qt.AlignHCenter
            }
            Text {
                text: state.title
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: Theme.title
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                Accessible.role: Accessible.Heading
                Accessible.name: state.title
            }
            Text {
                visible: state.message.length > 0
                text: state.message
                color: Theme.textDim
                font.family: Theme.fontFamily
                font.pixelSize: Theme.normal
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Flow {
                visible: state.actions.length > 0
                Layout.fillWidth: true
                Layout.topMargin: Theme.sp(6)
                Layout.alignment: Qt.AlignHCenter
                spacing: Theme.sp(8)
                Repeater {
                    model: state.actions
                    delegate: Button {
                        required property var modelData
                        required property int index
                        text: modelData.label
                        enabled: modelData.enabled === undefined ? true : modelData.enabled
                        highlighted: index === 0
                        implicitHeight: Theme.controlHeight
                        Accessible.name: modelData.label
                        ToolTip.visible: hovered && (modelData.tip ?? "").length > 0
                        ToolTip.delay: 450
                        ToolTip.text: modelData.tip ?? ""
                        onClicked: if (modelData.trigger) modelData.trigger()
                    }
                }
            }
            Text {
                visible: state.note.length > 0
                text: state.note
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.small
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
    }
}
