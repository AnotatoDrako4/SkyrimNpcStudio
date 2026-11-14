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

    readonly property bool morphsAllowed: !root.studio.appearance.analysing
        && !(root.studio.appearance.faceCompatibility.blocked || []).some(b => b.op === "face-morphs")
    readonly property bool skinAllowed: !root.studio.appearance.analysing
        && !(root.studio.appearance.faceCompatibility.blocked || []).some(b => b.op === "skin-tone")
    readonly property var caps: root.studio.beautyLab ? root.studio.beautyLab.workshopCapabilities : ({})

    property int editingTintIndex: -1
    property real editingTintAlpha: 1.0

    ColorDialog {
        id: tintColorDialog
        title: qsTr("Elegir color de capa de tinte facial")
        onAccepted: {
            if (root.editingTintIndex >= 0) {
                root.studio.appearance.setTint(root.editingTintIndex, selectedColor, root.editingTintAlpha)
            }
        }
    }

    ColumnLayout {
        width: root.availableWidth
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            Text {
                text: qsTr("Face Beauty Workshop · Estructura y Tintes")
                color: Theme.text
                font.bold: true
                font.pixelSize: Theme.large
            }
            Item { Layout.fillWidth: true }
            Button {
                text: qsTr("Restablecer Morphs")
                enabled: Boolean(root.studio.appearance.hasFaceEdits)
                onClicked: root.studio.appearance.resetMorphs()
            }
            Button {
                text: qsTr("Restablecer Todo")
                enabled: Boolean(root.studio.appearance.hasFaceEdits)
                onClicked: root.studio.appearance.resetFace()
            }
        }

        // Tarjeta de capacidades técnicas: distingue geometría paramétrica vs importada
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: capCol.implicitHeight + 16
            radius: Theme.radius
            color: root.morphsAllowed ? Qt.rgba(Theme.ok.r, Theme.ok.g, Theme.ok.b, 0.08)
                                      : Qt.rgba(Theme.warn.r, Theme.warn.g, Theme.warn.b, 0.12)
            border.color: root.morphsAllowed ? Theme.ok : Theme.warn
            border.width: 1

            ColumnLayout {
                id: capCol
                anchors.fill: parent
                anchors.margins: 8
                spacing: 4

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    StatusBadge {
                        text: root.caps.geometryBadge || (root.morphsAllowed ? qsTr("Morfología Paramétrica") : qsTr("Geometría Importada"))
                        tone: root.morphsAllowed ? Theme.ok : Theme.warn
                    }
                    Text {
                        text: qsTr("Límite técnico NAM9: [-1.00, +1.00]")
                        color: Theme.textDim
                        font.pixelSize: Theme.micro
                        font.family: Theme.mono
                    }
                    Item { Layout.fillWidth: true }
                    Button {
                        visible: Boolean(root.studio.appearance.faceCompatibility.canUnlockForModding)
                        text: qsTr("Desbloquear rostro para modear")
                        highlighted: true
                        onClicked: root.studio.appearance.unlockForModding()
                    }
                }

                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.text
                    font.pixelSize: Theme.small
                    text: root.caps.geometrySummary || qsTr("Modifica los morphs anatómicos reales del NPC reconocidos por el motor de Skyrim.")
                }
            }
        }

        FaceProtectionCard {
            Layout.fillWidth: true
            studio: root.studio
        }

        // Rasgos Raciales (NAMA Face Presets: NoseType, BrowType, EyesType, LipType)
        Rectangle {
            visible: (root.studio.appearance.presetSlots || []).length > 0
            Layout.fillWidth: true
            implicitHeight: presetsCol.implicitHeight + 16
            color: Theme.panelAlt
            radius: Theme.radius
            border.color: Theme.borderSubtle

            ColumnLayout {
                id: presetsCol
                anchors.fill: parent
                anchors.margins: 8
                spacing: 8

                SectionHeader {
                    title: qsTr("Rasgos Raciales Base (Presets NAMA)")
                    detail: qsTr("Verificados contra Chargen TRI")
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 10
                    rowSpacing: 6

                    Repeater {
                        model: root.studio.appearance.presetSlots || []
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 6

                            Text {
                                text: (modelData.label || modelData.displayNameEs || "") + ":"
                                color: Theme.text
                                font.pixelSize: Theme.small
                                Layout.preferredWidth: 110
                                elide: Text.ElideRight
                            }

                            SpinBox {
                                from: -1
                                to: Math.max(0, modelData.maxPreset !== undefined ? modelData.maxPreset : (modelData.max || 0))
                                value: modelData.value !== undefined ? modelData.value : 0
                                enabled: root.morphsAllowed && Boolean(modelData.editable !== undefined ? modelData.editable : true)
                                onValueModified: root.studio.appearance.setFacePreset(modelData.slot, value)
                            }
                        }
                    }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

        // Nose
        FaceSliderGroup {
            Layout.fillWidth: true
            enabled: root.morphsAllowed
            title: qsTr("Nariz")
            morphValues: root.studio.appearance.morphs
            sliders: root.studio.appearance.standardSliders.filter(s => s.category === "Nose")
            onSliderChanged: (key, val) => root.studio.appearance.setSlider(key, val)
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

        // Jaw
        FaceSliderGroup {
            Layout.fillWidth: true
            enabled: root.morphsAllowed
            title: qsTr("Mandíbula")
            morphValues: root.studio.appearance.morphs
            sliders: root.studio.appearance.standardSliders.filter(s => s.category === "Jaw")
            onSliderChanged: (key, val) => root.studio.appearance.setSlider(key, val)
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

        // Cheeks
        FaceSliderGroup {
            Layout.fillWidth: true
            enabled: root.morphsAllowed
            title: qsTr("Mejillas")
            morphValues: root.studio.appearance.morphs
            sliders: root.studio.appearance.standardSliders.filter(s => s.category === "Cheeks")
            onSliderChanged: (key, val) => root.studio.appearance.setSlider(key, val)
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

        // Eyes
        FaceSliderGroup {
            Layout.fillWidth: true
            enabled: root.morphsAllowed
            title: qsTr("Ojos")
            morphValues: root.studio.appearance.morphs
            sliders: root.studio.appearance.standardSliders.filter(s => s.category === "Eyes")
            onSliderChanged: (key, val) => root.studio.appearance.setSlider(key, val)
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

        // Brows
        FaceSliderGroup {
            Layout.fillWidth: true
            enabled: root.morphsAllowed
            title: qsTr("Cejas")
            morphValues: root.studio.appearance.morphs
            sliders: root.studio.appearance.standardSliders.filter(s => s.category === "Brows")
            onSliderChanged: (key, val) => root.studio.appearance.setSlider(key, val)
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

        // Lips
        FaceSliderGroup {
            Layout.fillWidth: true
            enabled: root.morphsAllowed
            title: qsTr("Labios y Boca")
            morphValues: root.studio.appearance.morphs
            sliders: root.studio.appearance.standardSliders.filter(s => s.category === "Lips")
            onSliderChanged: (key, val) => root.studio.appearance.setSlider(key, val)
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

        // Chin
        FaceSliderGroup {
            Layout.fillWidth: true
            enabled: root.morphsAllowed
            title: qsTr("Barbilla")
            morphValues: root.studio.appearance.morphs
            sliders: root.studio.appearance.standardSliders.filter(s => s.category === "Chin")
            onSliderChanged: (key, val) => root.studio.appearance.setSlider(key, val)
        }

        // Capas de Tinte Facial, Maquillaje, Cicatrices y Pintura de Guerra
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

        SectionHeader {
            title: qsTr("Maquillaje, Pintura de Guerra, Cicatrices y Tintes")
            detail: qsTr("%1 máscaras de raza").arg((root.studio.appearance.tintCatalog || []).length)
        }

        Repeater {
            model: root.studio.appearance.tintCatalog || []
            delegate: Rectangle {
                required property var modelData
                Layout.fillWidth: true
                implicitHeight: tintRow.implicitHeight + 12
                color: modelData.active ? Theme.panelAlt : Qt.rgba(Theme.panel.r, Theme.panel.g, Theme.panel.b, 0.5)
                radius: Theme.radius
                border.color: modelData.active ? Theme.accent : Theme.borderSubtle
                border.width: 1

                RowLayout {
                    id: tintRow
                    anchors.fill: parent
                    anchors.margins: 6
                    spacing: 8

                    Rectangle {
                        width: 22
                        height: 22
                        radius: 4
                        color: modelData.color || "#ffffff"
                        border.color: Theme.border
                        MouseArea {
                            anchors.fill: parent
                            enabled: root.skinAllowed
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                root.editingTintIndex = modelData.index
                                root.editingTintAlpha = modelData.value > 0 ? modelData.value : 0.5
                                tintColorDialog.selectedColor = Qt.color(modelData.color || "#ffffff")
                                tintColorDialog.open()
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        RowLayout {
                            spacing: 6
                            Text {
                                text: modelData.label
                                color: Theme.text
                                font.bold: Boolean(modelData.active)
                                font.pixelSize: Theme.small
                                elide: Text.ElideRight
                            }
                            StatusBadge {
                                text: modelData.category || qsTr("Tinte")
                                tone: modelData.textureFound ? Theme.textDim : Theme.error
                            }
                        }
                        Text {
                            text: (modelData.texture || "") + (modelData.provider ? " · " + modelData.provider : "")
                            color: Theme.textDim
                            font.pixelSize: Theme.micro
                            font.family: Theme.mono
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                        }
                    }

                    Slider {
                        Layout.preferredWidth: 100
                        from: 0.0
                        to: 1.0
                        value: modelData.value || 0.0
                        enabled: root.skinAllowed
                        onMoved: root.studio.appearance.setTint(modelData.index, Qt.color(modelData.color || "#ffffff"), value)
                    }

                    Text {
                        text: Math.round((modelData.value || 0) * 100) + "%"
                        color: Theme.text
                        font.pixelSize: Theme.micro
                        Layout.preferredWidth: 34
                    }
                }
            }
        }
    }
}
