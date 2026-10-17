import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

Rectangle {
    id: root
    property string title: "Grupo"
    property var sliders: []
    property var morphValues: ({})
    signal sliderChanged(string key, real value)
    signal resetClicked(string key)

    color: "transparent"
    implicitHeight: colLayout.implicitHeight

    ColumnLayout {
        id: colLayout
        anchors.fill: parent
        spacing: 6

        Text {
            text: root.title
            color: Theme.accent
            font.bold: true
            font.pixelSize: Theme.normal
            Layout.topMargin: 4
            Layout.bottomMargin: 2
        }

        Repeater {
            model: root.sliders
            delegate: RowLayout {
                id: rowDel
                Layout.fillWidth: true
                spacing: 8

                readonly property string sliderKey: modelData.key
                readonly property real currentVal: root.morphValues[sliderKey] !== undefined ? root.morphValues[sliderKey] : modelData.defaultValue

                Text {
                    text: modelData.displayNameEs || modelData.displayNameEn || modelData.key
                    color: Theme.text
                    font.pixelSize: Theme.small
                    Layout.preferredWidth: 140
                    elide: Text.ElideRight
                    ToolTip.text: modelData.key
                    ToolTip.visible: labelMouse.containsMouse
                    MouseArea {
                        id: labelMouse
                        anchors.fill: parent
                        hoverEnabled: true
                    }
                }

                Slider {
                    id: sld
                    Layout.fillWidth: true
                    from: modelData.min
                    to: modelData.max
                    stepSize: 0.05
                    value: rowDel.currentVal
                    onMoved: root.sliderChanged(rowDel.sliderKey, value)
                }

                Text {
                    text: rowDel.currentVal.toFixed(2)
                    color: Theme.textDim
                    font.pixelSize: Theme.small
                    font.family: "Consolas, monospace"
                    Layout.preferredWidth: 36
                    horizontalAlignment: Text.AlignRight
                }

                Button {
                    text: "↺"
                    implicitWidth: 24
                    implicitHeight: 24
                    padding: 0
                    ToolTip.text: qsTr("Restablecer a 0.0")
                    ToolTip.visible: hovered
                    onClicked: {
                        sld.value = modelData.defaultValue
                        root.sliderChanged(rowDel.sliderKey, modelData.defaultValue)
                    }
                }
            }
        }
    }
}
