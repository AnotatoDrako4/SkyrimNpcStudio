import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

ScrollView {
    id: root
    required property StudioController studio
    contentWidth: availableWidth
    clip: true

    // Sliders and presets need the analysed FaceGen and TRI files that fit the head's meshes.
    readonly property bool morphsAllowed: !root.studio.appearance.analysing
        && !(root.studio.appearance.faceCompatibility.blocked || []).some(b => b.op === "face-morphs")

    ColumnLayout {
        width: root.availableWidth
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            Text {
                text: qsTr("Estructura Facial")
                color: Theme.text
                font.bold: true
                font.pixelSize: Theme.large
            }
            Item { Layout.fillWidth: true }
            Button {
                text: qsTr("Restablecer Rostro")
                onClicked: root.studio.appearance.resetFace()
            }
        }

        Text {
            text: qsTr("Modifica los morphs anatómicos reales del NPC reconocidos por el motor de Skyrim.")
            color: Theme.textDim
            font.pixelSize: Theme.small
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Theme.border
        }

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

        // Diagnosis of the face protection, collapsed: below the controls so they keep their place.
        FaceProtectionCard {
            Layout.fillWidth: true
            studio: root.studio
        }
    }
}
