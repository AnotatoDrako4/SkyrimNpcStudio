import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick3D
import QtQuick3D.Helpers
import SkyrimNpcStudio

Rectangle {
    id: root
    objectName: "characterViewport"
    required property StudioController studio
    readonly property var scene: studio.scene
    color: Theme.bg
    property bool skeletonVisible: false
    property bool uvMode: false
    property bool partsVisible: false
    property real ambient: 0.25
    function frame(center, radius, yaw) {
        pivot.position = center
        pivot.eulerRotation = Qt.vector3d(0, yaw, 0)
        camera.z = Math.max(20, radius * 2.8)
    }
    function reset() { frame(scene.center, scene.radius, 0) }
    function applyCameraPreset(index) {
        root.uvMode = false
        cameraPreset.currentIndex = index
        const yaw = index === 2 ? 90 : index === 3 ? 180 : 0
        if (index === 4) {
            const heads = root.scene.parts.filter(p => p.group === "head")
            if (heads.length > 0) root.frame(heads[0].center, Math.min(heads[0].radius, root.scene.radius * 0.22), 0)
            else root.frame(root.scene.center, root.scene.radius, 0)
        } else root.frame(root.scene.center, root.scene.radius, yaw)
    }
    function pickCharacter() {
        for (let y = 0.25; y < 0.8; y += 0.05) {
            for (let x = 0.35; x < 0.7; x += 0.05) {
                const hit = viewport.pick(viewport.width*x, viewport.height*y)
                if (hit.objectHit && hit.objectHit.partId !== undefined) {
                    root.scene.select(hit.objectHit.partId)
                    return hit.objectHit.partId
                }
            }
        }
        return ""
    }
    Connections { target: root.scene; function onReady() { root.reset() } }
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        Flow {
            Layout.fillWidth: true
            spacing: 4
            padding: 6
            ComboBox {
                id: cameraPreset
                width: 135
                model: [qsTr("Cuerpo entero"), qsTr("Frontal"), qsTr("Lateral"), qsTr("Posterior"), qsTr("Rostro")]
                onActivated: root.applyCameraPreset(currentIndex)
            }
            ToolButton {
                text: "\u21bb"
                onClicked: root.reset()
                ToolTip.visible: hovered; ToolTip.text: qsTr("Restablecer cámara")
            }
            ToolButton {
                text: "\u2316"
                enabled: root.scene.selected.length > 0
                onClicked: { const p = root.scene.selectedBounds(); root.frame(p.center, p.radius, 0) }
                ToolTip.visible: hovered; ToolTip.text: qsTr("Encuadrar pieza seleccionada")
            }
            CheckBox { text: qsTr("Original"); checked: root.scene.original; onToggled: root.scene.original = checked }
            CheckBox { text: qsTr("Huesos"); checked: root.skeletonVisible; onToggled: root.skeletonVisible = checked }
            CheckBox {
                text: "UV"; checked: root.uvMode
                onToggled: { root.uvMode = checked; if (checked) root.frame(Qt.vector3d(0,0,0), 90, 0); else root.reset() }
            }
            ToolButton {
                text: "\u2630"; checkable: true; checked: root.partsVisible
                onToggled: root.partsVisible = checked
                ToolTip.visible: hovered; ToolTip.text: qsTr("Geometrías")
            }
            ToolButton {
                text: "\u00d7"; visible: root.scene.loading; onClicked: root.scene.cancel()
                ToolTip.visible: hovered; ToolTip.text: qsTr("Cancelar carga")
            }
        }
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 0
            View3D {
                id: viewport
                objectName: "characterView3D"
                Layout.fillWidth: true; Layout.fillHeight: true
                camera: camera
                environment: SceneEnvironment {
                    backgroundMode: SceneEnvironment.Color
                    clearColor: "#191b20"
                    antialiasingMode: SceneEnvironment.MSAA
                    antialiasingQuality: SceneEnvironment.High
                }
                Node {
                    id: pivot
                    PerspectiveCamera { id: camera; z: 300; clipNear: 0.1; clipFar: 10000; fieldOfView: 45 }
                }
                DirectionalLight { eulerRotation: Qt.vector3d(-30,-25,0); color: "#fff0dc"; brightness: 1.1; castsShadow: true; ambientColor: Qt.rgba(root.ambient,root.ambient,root.ambient,1) }
                DirectionalLight { eulerRotation: Qt.vector3d(-10,125,0); color: "#d6e7ff"; brightness: 0.65 }
                DirectionalLight { eulerRotation: Qt.vector3d(-60,190,0); brightness: 0.4 }
                Repeater3D {
                    model: root.scene.parts
                    delegate: Model {
                        required property var modelData
                        property string partId: modelData.id
                        visible: modelData.visible && (!root.uvMode || root.scene.selected === partId || root.scene.selected === "")
                        geometry: root.uvMode ? modelData.uvGeometry : modelData.geometry
                        scale: root.uvMode ? Qt.vector3d(1,1,1) : Qt.vector3d(modelData.scale,modelData.scale,modelData.scale)
                        pickable: true
                        materials: PrincipledMaterial {
                            baseColor: modelData.tint
                            baseColorMap: modelData.diffuse ? diffuseTexture : null
                            normalMap: !root.uvMode && modelData.normal ? normalTexture : null
                            roughness: 0.8
                            metalness: 0
                            lighting: root.uvMode ? PrincipledMaterial.NoLighting : PrincipledMaterial.FragmentLighting
                            cullMode: root.uvMode || modelData.doubleSided ? Material.NoCulling : Material.BackFaceCulling
                            alphaMode: modelData.alphaTest ? PrincipledMaterial.Mask : modelData.alphaBlend ? PrincipledMaterial.Blend : PrincipledMaterial.Opaque
                            alphaCutoff: modelData.alphaCutoff
                            emissiveFactor: root.scene.selected === partId ? Qt.vector3d(0.08,0.07,0.02) : Qt.vector3d(0,0,0)
                            Texture { id: diffuseTexture; textureData: modelData.diffuse; generateMipmaps: false; flipV: false }
                            Texture { id: normalTexture; textureData: modelData.normal; generateMipmaps: false; flipV: false }
                        }
                    }
                }
                Model {
                    visible: root.skeletonVisible && !root.uvMode
                    geometry: root.scene.skeleton
                    scale: root.scene.parts.length > 0 ? Qt.vector3d(root.scene.parts[0].scale,root.scene.parts[0].scale,root.scene.parts[0].scale) : Qt.vector3d(1,1,1)
                    materials: PrincipledMaterial { baseColor: "#f0c96b"; lighting: PrincipledMaterial.NoLighting }
                }
                OrbitCameraController { anchors.fill: parent; origin: pivot; camera: camera; acceptedButtons: Qt.RightButton | Qt.MiddleButton; panEnabled: true }
                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    onTapped: (eventPoint) => {
                        const hit = viewport.pick(eventPoint.position.x, eventPoint.position.y)
                        root.scene.select(hit.objectHit ? hit.objectHit.partId : "")
                    }
                }
                BusyIndicator { anchors.centerIn: parent; running: root.scene.loading; visible: running }
                Text {
                    anchors.centerIn: parent; width: Math.min(320,parent.width - 30)
                    text: root.scene.status || qsTr("Selecciona un NPC")
                    color: Theme.textDim; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter
                    visible: !root.scene.loading && root.scene.parts.length === 0
                }
            }
            ScrollView {
                visible: root.partsVisible; Layout.preferredWidth: 190; Layout.fillHeight: true
                clip: true
                Column {
                    width: parent.width
                    Repeater {
                        model: root.scene.parts
                        delegate: CheckBox {
                            required property var modelData
                            width: parent.width; text: modelData.label; checked: modelData.visible
                            contentItem: Label { text: parent.text; leftPadding: parent.indicator.width + parent.spacing; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter }
                            onToggled: root.scene.setVisible(modelData.id, checked)
                            onPressed: root.scene.select(modelData.id)
                            ToolTip.visible: hovered; ToolTip.text: modelData.label + "\n" + modelData.path
                        }
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true; Layout.margins: 6
            Text { text: qsTr("Ambiente"); color: Theme.textDim; font.pixelSize: Theme.small }
            Slider { from: 0; to: 0.6; value: root.ambient; onMoved: root.ambient = value; Layout.preferredWidth: 110 }
            Text { text: root.scene.status; color: Theme.textDim; font.pixelSize: Theme.small; Layout.fillWidth: true; elide: Text.ElideRight }
        }
        Text {
            Layout.fillWidth: true; Layout.margins: 6
            text: qsTr("Vista preliminar con recursos reales · pose de referencia · materiales aproximados")
            color: Theme.warn; font.pixelSize: Theme.small; wrapMode: Text.Wrap
        }
        ScrollView {
            Layout.fillWidth: true; Layout.preferredHeight: Math.min(90, issueList.implicitHeight)
            visible: root.scene.issues.length > 0; clip: true
            IssueList { id: issueList; width: parent.width; issues: root.scene.issues }
        }
    }
}
