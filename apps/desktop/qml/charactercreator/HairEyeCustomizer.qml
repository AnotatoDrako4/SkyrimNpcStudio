import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import SkyrimNpcStudio

ScrollView {
    id: root
    required property StudioController studio
    contentWidth: availableWidth
    clip: true

    readonly property var hairPalette: [
        { name: qsTr("Negro Azabache"), color: "#111111" },
        { name: qsTr("Castaño Oscuro"), color: "#3B2219" },
        { name: qsTr("Castaño Medio"), color: "#5A3825" },
        { name: qsTr("Castaño Claro"), color: "#8B5A2B" },
        { name: qsTr("Rubio Ceniza"), color: "#C2B280" },
        { name: qsTr("Rubio Dorado"), color: "#E6C280" },
        { name: qsTr("Pelirrojo / Cobrizo"), color: "#A53A1E" },
        { name: qsTr("Rojo Fuego"), color: "#C0392B" },
        { name: qsTr("Gris / Ceniza"), color: "#808080" },
        { name: qsTr("Blanco Platino"), color: "#ECECEC" },
        { name: qsTr("Azul Mágico"), color: "#2980B9" },
        { name: qsTr("Violeta Nocturno"), color: "#8E44AD" }
    ]

    ColorDialog {
        id: customHairColorDialog
        title: qsTr("Elegir color de cabello personalizado")
        selectedColor: root.studio.appearance.hairColor
        onAccepted: root.studio.appearance.setHairColor(selectedColor)
    }

    // Hair color edits need the analysed FaceGen and a HairTint shader (Hair Color Safe Mode).
    readonly property bool hairColorAllowed: !root.studio.appearance.analysing
        && !(root.studio.appearance.faceCompatibility.blocked || []).some(b => b.op === "hair-color")
    readonly property bool headPartsAllowed: !root.studio.appearance.analysing
        && !(root.studio.appearance.faceCompatibility.blocked || []).some(b => b.op === "head-parts")

    ColumnLayout {
        width: root.availableWidth
        spacing: 14

        // --- SECCIÓN: CABELLO ---
        RowLayout {
            Layout.fillWidth: true
            Text {
                text: qsTr("Color de Cabello")
                color: Theme.accent
                font.bold: true
                font.pixelSize: Theme.fs(13)
            }
            // Same line, so nothing moves when the analysis ends: why the palette is disabled.
            Text {
                visible: !root.hairColorAllowed
                Layout.fillWidth: true
                text: root.studio.appearance.analysing ? qsTr("· analizando el FaceGen…") : qsTr("· bloqueado (ver Protección facial)")
                color: Theme.textDim
                font.pixelSize: Theme.small
                elide: Text.ElideRight
            }
        }

        RowLayout {
            spacing: 10
            Rectangle {
                width: 32
                height: 32
                radius: 4
                color: root.studio.appearance.hairColor
                border.color: Theme.border
                border.width: 1
            }
            Text {
                text: root.studio.appearance.hairColor.toString().toUpperCase()
                color: Theme.text
                font.bold: true
                font.pixelSize: Theme.normal
            }
            Item { Layout.fillWidth: true }
            Button {
                text: qsTr("Selector libre…")
                enabled: root.hairColorAllowed
                onClicked: customHairColorDialog.open()
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 4
            rowSpacing: 6
            columnSpacing: 6

            Repeater {
                model: root.hairPalette
                delegate: Button {
                    Layout.fillWidth: true
                    implicitHeight: 34
                    enabled: root.hairColorAllowed
                    ToolTip.text: modelData.name
                    ToolTip.visible: hovered

                    contentItem: RowLayout {
                        spacing: 6
                        Rectangle {
                            width: 16
                            height: 16
                            radius: 3
                            color: modelData.color
                            border.color: Theme.border
                            border.width: 1
                        }
                        Text {
                            text: modelData.name
                            color: Theme.text
                            font.pixelSize: Theme.micro
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }

                    onClicked: root.studio.appearance.setHairColor(Qt.color(modelData.color))
                }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

        // --- SECCIÓN: OJOS Y MIRADA ---
        Text {
            text: qsTr("Ojos y Cejas")
            color: Theme.accent
            font.bold: true
            font.pixelSize: Theme.fs(13)
        }

        Text {
            text: qsTr("Partes de Cabeza (HeadParts) asignadas:")
            color: Theme.textDim
            font.pixelSize: Theme.small
        }

        // Lista de HeadParts actuales
        Repeater {
            model: root.studio.appearance.headParts
            delegate: Rectangle {
                Layout.fillWidth: true
                implicitHeight: 32
                color: Theme.control
                radius: 4
                border.color: Theme.border
                border.width: 1

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 6
                    spacing: 8

                    Text {
                        text: modelData.type || qsTr("Parte")
                        color: Theme.accent
                        font.bold: true
                        font.pixelSize: Theme.small
                        Layout.preferredWidth: 60
                    }

                    Text {
                        text: (modelData.editorId || modelData.formKey) + (modelData.meshPath ? " (" + modelData.meshPath + ")" : "")
                        color: Theme.text
                        font.pixelSize: Theme.small
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    Button {
                        text: "✕"
                        implicitWidth: 24
                        implicitHeight: 24
                        onClicked: root.studio.appearance.removeHeadPart(modelData.type)
                        ToolTip.text: qsTr("Quitar parte de cabeza")
                        ToolTip.visible: hovered
                    }
                }
            }
        }

        // Change a head part: only parts valid for this NPC's race and sex (worker query), applied as
        // a PNAM edit; the head is rebuilt and the export carries it. Blocked for heads whose morphs do
        // not match their mesh (see «Protección facial»).
        Text {
            text: qsTr("Cambiar pieza de cabeza")
            color: Theme.textDim
            font.pixelSize: Theme.small
        }
        RowLayout {
            Layout.fillWidth: true
            enabled: root.headPartsAllowed
            ComboBox {
                id: partType
                implicitWidth: 120
                model: ["Hair", "Eyes", "Eyebrows", "FacialHair", "Scars"]
            }
            TextField {
                id: partFilter
                Layout.fillWidth: true
                placeholderText: qsTr("Filtrar (opcional)")
                onAccepted: root.studio.appearance.searchHeadParts(partType.currentText, partFilter.text)
            }
            Button {
                text: qsTr("Buscar")
                onClicked: root.studio.appearance.searchHeadParts(partType.currentText, partFilter.text)
            }
        }
        Text {
            visible: !root.headPartsAllowed
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.textDim
            font.pixelSize: Theme.small
            text: root.studio.appearance.analysing ? qsTr("Disponible cuando termine el análisis del FaceGen.")
                                                   : qsTr("Bloqueado para esta cabeza: ver «Protección facial».")
        }
        ListView {
            id: partOptions
            Layout.fillWidth: true
            Layout.preferredHeight: count > 0 ? Math.min(contentHeight, 180) : 0
            visible: count > 0
            clip: true
            enabled: root.headPartsAllowed
            model: root.studio.appearance.headPartOptions
            delegate: ItemDelegate {
                required property var modelData
                width: ListView.view.width
                height: 26
                font.pixelSize: Theme.small
                text: (modelData.editorId || modelData.formKey) + "  ·  " + modelData.plugin
                onClicked: root.studio.appearance.setHeadPart(modelData.formKey, modelData.type)
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

        // Eye color: in Skyrim it is the Eyes head part (its texture set), not a tint. Choose "Eyes" in
        // «Cambiar pieza de cabeza» above; there is no separate eye palette.

        // Diagnosis of the face protection, collapsed: below the controls so they keep their place.
        FaceProtectionCard {
            Layout.fillWidth: true
            studio: root.studio
        }
    }
}
