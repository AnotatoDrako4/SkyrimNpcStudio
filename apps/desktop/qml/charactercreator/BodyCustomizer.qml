import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import SkyrimNpcStudio

ScrollView {
    id: root
    // Skin tone and tints export a generated FaceTint; a head with its own face texture blocks them.
    readonly property bool skinToneAllowed: !root.studio.appearance.analysing
        && !(root.studio.appearance.faceCompatibility.blocked || []).some(b => b.op === "skin-tone")
    required property StudioController studio
    contentWidth: availableWidth
    clip: true

    readonly property var skinPalette: [
        { name: qsTr("Nórdico Pálido"), color: "#FDEBD0" },
        { name: qsTr("Nórdico Rosado"), color: "#F5CBA7" },
        { name: qsTr("Imperial Bronceado"), color: "#E0A96D" },
        { name: qsTr("Bretón Neutro"), color: "#EDBB99" },
        { name: qsTr("Redguard Trigueño"), color: "#A0522D" },
        { name: qsTr("Redguard Ébano"), color: "#5D4037" },
        { name: qsTr("Altmer Dorado"), color: "#F9E79F" },
        { name: qsTr("Bosmer Cobrizo"), color: "#D35400" },
        { name: qsTr("Dunmer Cenizo"), color: "#7F8C8D" },
        { name: qsTr("Dunmer Azulado"), color: "#5D6D7E" },
        { name: qsTr("Orsimer Verdoso"), color: "#52734D" },
        { name: qsTr("Orsimer Ocre"), color: "#8B7D6B" }
    ]

    ColorDialog {
        id: skinToneDialog
        title: qsTr("Elegir tono de piel personalizado")
        selectedColor: root.studio.appearance.skinTone
        onAccepted: root.studio.appearance.setSkinTone(selectedColor)
    }

    ColumnLayout {
        width: root.availableWidth
        spacing: 14

        // --- SECCIÓN: DIMENSIONES CORPORALES ---
        Text {
            text: qsTr("Proporciones Corporales")
            color: Theme.accent
            font.bold: true
            font.pixelSize: 13
        }

        // Tarjeta de Detección de Malla y Físicas
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: bodyInfoCol.implicitHeight + 14
            color: Theme.panelAlt
            radius: 4
            border.color: Theme.border
            border.width: 1

            ColumnLayout {
                id: bodyInfoCol
                anchors.fill: parent
                anchors.margins: 8
                spacing: 4

                RowLayout {
                    spacing: 6
                    Text {
                        text: qsTr("Malla Corporal:")
                        color: Theme.textDim
                        font.pixelSize: 10
                    }
                    Text {
                        text: (root.studio.outfit.compatibilityReport.body?.name ?? qsTr("Vanilla"))
                        color: Theme.accent
                        font.bold: true
                        font.pixelSize: 11
                    }
                    Item { Layout.fillWidth: true }
                    Rectangle {
                        visible: root.studio.outfit.compatibilityReport.body?.physicsEnabled ?? false
                        implicitWidth: physicsTxt.implicitWidth + 8
                        implicitHeight: 18
                        radius: 3
                        color: "#2C3E50"
                        border.color: "#3498DB"
                        border.width: 1
                        Text {
                            id: physicsTxt
                            anchors.centerIn: parent
                            text: qsTr("FSMP / SMP Activo")
                            color: "#5DADE2"
                            font.pixelSize: 9
                            font.bold: true
                        }
                    }
                }

                Text {
                    text: root.studio.outfit.compatibilityReport.body?.details ?? qsTr("Soporta morphs estándar de peso (0 a 100).")
                    color: Theme.textDim
                    font.pixelSize: 10
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
        }

        // Peso (Weight)
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4

            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: qsTr("Peso (Morfología):")
                    color: Theme.text
                    font.pixelSize: 11
                }
                Item { Layout.fillWidth: true }
                Text {
                    text: root.studio.appearance.weight.toFixed(1)
                    color: Theme.accent
                    font.bold: true
                    font.pixelSize: 11
                }
            }

            Slider {
                Layout.fillWidth: true
                from: 0.0
                to: 100.0
                stepSize: 1.0
                value: root.studio.appearance.weight
                onMoved: root.studio.appearance.setWeight(value)
            }

            RowLayout {
                Layout.fillWidth: true
                Text { text: qsTr("Delgado (0)"); color: Theme.textDim; font.pixelSize: 9 }
                Item { Layout.fillWidth: true }
                Text { text: qsTr("Musculoso / Robusto (100)"); color: Theme.textDim; font.pixelSize: 9 }
            }
        }

        // Altura (Height / Scale)
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4

            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: qsTr("Estatura (Escala):")
                    color: Theme.text
                    font.pixelSize: 11
                }
                Item { Layout.fillWidth: true }
                Text {
                    text: root.studio.appearance.height.toFixed(2) + "x"
                    color: Theme.accent
                    font.bold: true
                    font.pixelSize: 11
                }
            }

            Slider {
                Layout.fillWidth: true
                from: 0.80
                to: 1.25
                stepSize: 0.01
                value: root.studio.appearance.height
                onMoved: root.studio.appearance.setHeight(value)
            }

            RowLayout {
                Layout.fillWidth: true
                Text { text: "0.80x"; color: Theme.textDim; font.pixelSize: 9 }
                Item { Layout.fillWidth: true }
                Text { text: "1.00x (Normal)"; color: Theme.textDim; font.pixelSize: 9 }
                Item { Layout.fillWidth: true }
                Text { text: "1.25x"; color: Theme.textDim; font.pixelSize: 9 }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

        // --- SECCIÓN: TONO DE PIEL ---
        Text {
            text: qsTr("Tono y Pigmentación de Piel")
            color: Theme.accent
            font.bold: true
            font.pixelSize: 13
        }

        RowLayout {
            spacing: 10
            Rectangle {
                width: 32
                height: 32
                radius: 4
                color: root.studio.appearance.skinTone
                border.color: Theme.border
                border.width: 1
            }
            Text {
                text: root.studio.appearance.skinTone.toString().toUpperCase()
                color: Theme.text
                font.bold: true
                font.pixelSize: 12
            }
            Item { Layout.fillWidth: true }
            Button {
                text: qsTr("Selector libre…")
                enabled: root.skinToneAllowed
                onClicked: skinToneDialog.open()
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 3
            rowSpacing: 6
            columnSpacing: 6

            Repeater {
                model: root.skinPalette
                delegate: Button {
                    Layout.fillWidth: true
                    implicitHeight: 32
                    enabled: root.skinToneAllowed
                    ToolTip.text: modelData.name
                    ToolTip.visible: hovered

                    contentItem: RowLayout {
                        spacing: 6
                        Rectangle {
                            width: 14
                            height: 14
                            radius: 3
                            color: modelData.color
                            border.color: Theme.border
                            border.width: 1
                        }
                        Text {
                            text: modelData.name
                            color: Theme.text
                            font.pixelSize: 10
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }

                    onClicked: root.studio.appearance.setSkinTone(Qt.color(modelData.color))
                }
            }
        }
    }
}
