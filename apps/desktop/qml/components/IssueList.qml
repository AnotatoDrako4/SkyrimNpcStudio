import QtQuick
import QtQuick.Layouts
import SkyrimNpcStudio

// Shows a list of {severity, code, message, subject} issues.
ColumnLayout {
    id: root
    property var issues: []
    property string emptyText: qsTr("Sin incidencias")
    spacing: 4
    Layout.fillWidth: true

    Text {
        visible: root.issues.length === 0
        text: root.emptyText
        color: Theme.ok
        font.pixelSize: Theme.small
    }

    Repeater {
        model: root.issues
        delegate: Rectangle {
            required property var modelData
            Layout.fillWidth: true
            implicitHeight: row.implicitHeight + 8
            radius: 3
            color: Theme.panelAlt
            border.color: Theme.severityColor(modelData.severity)

            RowLayout {
                id: row
                anchors.fill: parent
                anchors.margins: 4
                spacing: 6
                Text {
                    text: modelData.severity === "error" ? "✖" : modelData.severity === "warning" ? "⚠" : "ℹ"
                    color: Theme.severityColor(modelData.severity)
                    font.pixelSize: Theme.normal
                    Layout.alignment: Qt.AlignTop
                }
                Text {
                    text: "<b>" + modelData.code + "</b> — " + modelData.message
                          + (modelData.subject ? "<br><span style='color:" + Theme.textDim + "'>" + modelData.subject + "</span>" : "")
                    textFormat: Text.RichText
                    color: Theme.text
                    font.pixelSize: Theme.small
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
            }
        }
    }
}
