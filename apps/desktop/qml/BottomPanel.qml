import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

// Console, diagnostics (load order + index) and exports of this session.
Rectangle {
    id: root
    required property StudioController studio
    property var exports: []
    color: Theme.panel

    Connections {
        target: root.studio.outfit
        function onExported(result) { root.exports = root.exports.concat([result]); tabs.currentIndex = 2 }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            TabBar {
                id: tabs
                Layout.fillWidth: true
                TabButton { text: qsTr("Consola") + (root.studio.console.errorCount > 0 ? "  (" + root.studio.console.errorCount + " errores)" : "") }
                TabButton { text: qsTr("Diagnósticos (%1)").arg(root.studio.diagnostics.count) }
                TabButton { text: qsTr("Exportaciones (%1)").arg(root.exports.length) }
            }
            ToolButton { text: qsTr("Limpiar"); visible: tabs.currentIndex === 0; onClicked: root.studio.console.clear() }
        }

        StackLayout {
            currentIndex: tabs.currentIndex
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: consoleView
                clip: true
                model: root.studio.console
                ScrollBar.vertical: ScrollBar {}
                onCountChanged: positionViewAtEnd()
                delegate: Text {
                    required property string time
                    required property string level
                    required property string source
                    required property string message
                    width: ListView.view.width
                    leftPadding: 6
                    text: time + "  " + source.padEnd(8) + "  " + message
                    color: Theme.severityColor(level) === Theme.textDim ? Theme.text : Theme.severityColor(level)
                    font.family: Theme.mono
                    font.pixelSize: Theme.small
                    elide: Text.ElideRight
                }
            }

            ListView {
                clip: true
                model: root.studio.diagnostics
                ScrollBar.vertical: ScrollBar {}
                delegate: Text {
                    required property string severity
                    required property string code
                    required property string message
                    required property string subject
                    required property string source
                    width: ListView.view.width
                    leftPadding: 6
                    text: "[" + source + "] " + severity.toUpperCase() + " " + code + (subject ? " · " + subject : "") + " — " + message
                    color: Theme.severityColor(severity) === Theme.textDim ? Theme.text : Theme.severityColor(severity)
                    font.pixelSize: Theme.small
                    elide: Text.ElideRight
                }
            }

            ListView {
                clip: true
                model: root.exports
                delegate: RowLayout {
                    required property var modelData
                    width: ListView.view.width
                    Text {
                        text: modelData.pluginPath + "  ·  masters: " + modelData.masters.join(", ") + (modelData.eslFlagged ? "  ·  ESL" : "")
                        color: Theme.text
                        font.pixelSize: Theme.small
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                        Layout.leftMargin: 6
                    }
                    Button { text: qsTr("Abrir carpeta"); onClicked: root.studio.openFolder(modelData.modDirectory) }
                }
            }
        }
    }
}
