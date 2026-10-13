import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

Rectangle {
    id: root
    required property CharacterForgeViewModel forge
    // A future creation-only SceneViewModel can supply real geometry and a renderer
    // component. No fallback to studio.scene (the existing NPC editor) is allowed.
    property var sceneController: null
    property Component sceneComponent: null
    readonly property bool hasGeometry: sceneController !== null && sceneController.ready && sceneComponent !== null
    color: Theme.surface
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        ToolBar {
            Layout.fillWidth: true
            RowLayout {
                anchors.fill: parent
                ComboBox {
                    model: [qsTr("Completo"), qsTr("Rostro"), qsTr("Frontal"), qsTr("Lateral"), qsTr("Posterior")]
                    enabled: root.hasGeometry
                    onActivated: root.sceneController.setCameraPreset(currentIndex)
                }
                ToolButton {
                    text: qsTr("Encuadrar")
                    enabled: root.hasGeometry
                    onClicked: root.sceneController.frameAll()
                }
                CheckBox {
                    text: qsTr("Malla")
                    enabled: root.hasGeometry
                    onClicked: root.sceneController.setWireframe(checked)
                }
                CheckBox {
                    text: qsTr("Huesos")
                    enabled: root.hasGeometry
                    onClicked: root.sceneController.setSkeletonVisible(checked)
                }
                ToolButton {
                    text: qsTr("Original / adaptado")
                    enabled: root.hasGeometry
                    onClicked: root.sceneController.toggleComparison()
                }
            }
        }
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Loader {
                anchors.fill: parent
                active: root.hasGeometry
                sourceComponent: root.sceneComponent
            }
            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(parent.width - 48, 460)
                spacing: 16
                visible: !root.hasGeometry
                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    width: 68
                    height: 68
                    radius: 12
                    color: Theme.accentDim
                    border.color: Theme.accentBorder
                    Text {
                        anchors.centerIn: parent
                        text: "CF"
                        color: Theme.accent
                        font.pixelSize: 24
                        font.bold: true
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: root.forge.workspace.model ? root.forge.workspace.model.name : qsTr("Tu personaje empieza aquí")
                    color: Theme.text
                    font.pixelSize: Theme.heading
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }
                Text {
                    Layout.fillWidth: true
                    text: root.forge.workspace.model ? qsTr("Archivo seleccionado. La inspección de metadatos no carga geometría ni convierte a NIF. La previsualización espera un backend de creación compatible.") : qsTr("Crea un NPC original o selecciona un modelo. El visor se activará cuando existan geometrías reales; no se muestra un personaje de ejemplo.")
                    color: Theme.textDim
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Órbita · zoom · selección · vistas de cámara\nDisponibles cuando se conecte una escena real de Character Forge.")
                    color: Theme.textMuted
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }
            }
        }
        Text {
            Layout.fillWidth: true
            Layout.margins: 12
            text: root.forge.workspace.model && root.forge.workspace.model.available === false ? qsTr("El archivo seleccionado ya no está disponible en esta máquina.") : qsTr("FaceGen pendiente · verificación en Skyrim pendiente")
            color: Theme.textMuted
            elide: Text.ElideRight
        }
    }
}
