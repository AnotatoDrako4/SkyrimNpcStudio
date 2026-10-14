import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick3D
import QtQuick3D.AssetUtils
import QtQuick3D.Helpers
import SkyrimNpcStudio

// Character Forge preview: the validated model staged by ForgeSceneViewModel, imported at run time
// by Qt Quick 3D's RuntimeLoader. Never falls back to the NPC scene of the other workspaces.
Rectangle {
    id: root
    objectName: "forgeViewport"
    required property CharacterForgeViewModel forge
    readonly property ForgeSceneViewModel scene: forge.scene
    readonly property bool hasGeometry: scene.hasModel
    property real yaw: 0
    color: Theme.surface

    // Camera on the rendered bounds (RuntimeLoader.bounds: "may not be available before the first
    // render", so this runs when the viewport reports them).
    // Rendered bounds when the viewport has them; until then the validated POSITION bounds (the
    // accessors' min/max, node transforms not applied) so the model is at least in view.
    function framing() {
        const r = root.scene.rendered;
        if (r.center !== undefined)
            return { center: r.center, radius: r.radius };
        const b = root.scene.inspection.positionBounds || [];
        if (b.length !== 6)
            return null;
        const size = Qt.vector3d(b[3] - b[0], b[4] - b[1], b[5] - b[2]);
        return { center: Qt.vector3d((b[0] + b[3]) / 2, (b[1] + b[4]) / 2, (b[2] + b[5]) / 2), radius: size.length() / 2 };
    }
    function frame(newYaw) {
        const f = root.framing();
        if (!f || root.scene.state !== "loaded")
            return;
        const center = f.center;
        const radius = Math.max(f.radius, 0.001);
        const halfFov = camera.fieldOfView * Math.PI / 360;
        orbitOrigin.position = center;
        root.yaw = newYaw === undefined ? root.yaw : newYaw;
        orbitOrigin.eulerRotation = Qt.vector3d(-8, root.yaw, 0);
        const distance = radius / Math.sin(halfFov) * 1.15;
        camera.position = Qt.vector3d(0, 0, distance);
        camera.eulerRotation = Qt.vector3d(0, 0, 0);
        camera.clipNear = Math.max(distance / 1000, 0.0001);
        camera.clipFar = distance + radius * 100;
    }
    function resetView() {
        wireframe.checked = false;
        frame(0);
    }
    function reportStatus() {
        root.scene.reportLoaderStatus(loader.source, loader.status, loader.errorString);
    }
    // Qt 6.11 computes RuntimeLoader.bounds lazily and marks them clean even when the meshes are
    // not uploaded yet; boundsChanged is only emitted while they are dirty. Reading "bounds" before
    // that signal would store empty bounds and suppress the signal, so they are read only after it.
    property bool boundsReady: false
    function reportBounds() {
        if (root.boundsReady && loader.status === RuntimeLoader.Success)
            root.scene.reportBounds(loader.source, loader.bounds.minimum, loader.bounds.maximum);
    }

    Connections {
        target: root.scene
        function onFrameRequested() { root.frame(0) }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        ToolBar {
            Layout.fillWidth: true
            RowLayout {
                anchors.fill: parent
                spacing: 2
                ToolButton {
                    text: qsTr("Encuadrar")
                    enabled: root.scene.state === "loaded"
                    onClicked: root.frame()
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Centra la cámara en el modelo conservando el ángulo")
                }
                ToolButton {
                    text: qsTr("Frontal")
                    enabled: root.hasGeometry
                    onClicked: root.frame(0)
                }
                ToolButton {
                    text: qsTr("Lateral")
                    enabled: root.hasGeometry
                    onClicked: root.frame(90)
                }
                ToolButton {
                    text: qsTr("Posterior")
                    enabled: root.hasGeometry
                    onClicked: root.frame(180)
                }
                ToolButton {
                    text: qsTr("Restablecer")
                    enabled: root.hasGeometry
                    onClicked: root.resetView()
                }
                CheckBox {
                    id: wireframe
                    text: qsTr("Malla")
                    enabled: root.hasGeometry
                }
                CheckBox {
                    text: qsTr("Huesos")
                    enabled: false
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("RuntimeLoader de Qt 6.11 no expone las articulaciones importadas; la lista de huesos está en el importador.")
                }
                ToolButton {
                    text: qsTr("Original / adaptado")
                    enabled: false
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Disponible cuando exista un modelo adaptado para Skyrim que comparar.")
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: root.hasGeometry ? qsTr("Arrastrar: orbitar · Rueda: zoom · Ctrl+arrastrar: desplazar") : ""
                    color: Theme.textMuted
                    font.pixelSize: Theme.small
                }
            }
        }
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            View3D {
                id: view
                objectName: "forgeView3D"
                anchors.fill: parent
                camera: camera
                environment: SceneEnvironment {
                    backgroundMode: SceneEnvironment.Color
                    clearColor: "#16171a"
                    antialiasingMode: SceneEnvironment.MSAA
                    antialiasingQuality: SceneEnvironment.High
                    debugSettings: DebugSettings { wireframeEnabled: wireframe.checked }
                }
                Node {
                    id: orbitOrigin
                    // Qt Quick 3D's default clipNear is 10 units: glTF models (metres) would be clipped.
                    PerspectiveCamera {
                        id: camera
                        z: 5
                        fieldOfView: 40
                        clipNear: 0.01
                        clipFar: 1000
                    }
                }
                DirectionalLight {
                    eulerRotation: Qt.vector3d(-35, -30, 0)
                    brightness: 1.1
                    ambientColor: Qt.rgba(0.28, 0.28, 0.28, 1)
                }
                DirectionalLight {
                    eulerRotation: Qt.vector3d(-15, 150, 0)
                    brightness: 0.6
                }
                RuntimeLoader {
                    id: loader
                    source: root.scene.source
                    onSourceChanged: {
                        root.boundsReady = false;
                        Qt.callLater(root.reportStatus);
                    }
                    onStatusChanged: Qt.callLater(root.reportStatus)
                    onBoundsChanged: {
                        root.boundsReady = true;
                        Qt.callLater(root.reportBounds);
                    }
                }
                OrbitCameraController {
                    anchors.fill: parent
                    origin: orbitOrigin
                    camera: camera
                    enabled: root.hasGeometry
                }
            }

            BusyIndicator {
                anchors.centerIn: parent
                running: root.scene.busy
                visible: running
            }

            // Empty, unsupported, error and loading states (no sample character is ever shown).
            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(parent.width - 48, 480)
                spacing: 12
                visible: !root.hasGeometry
                Text {
                    Layout.fillWidth: true
                    text: root.scene.fileName || (root.forge.workspace.model ? root.forge.workspace.model.name : qsTr("Tu personaje empieza aquí"))
                    color: Theme.text
                    font.pixelSize: Theme.heading
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }
                Text {
                    objectName: "forgeViewportMessage"
                    Layout.fillWidth: true
                    text: root.scene.message.length ? root.scene.message
                        : qsTr("Crea un proyecto e importa un modelo GLB o glTF para verlo aquí. No se muestra ningún personaje de ejemplo.")
                    color: root.scene.state === "error" ? Theme.warn : Theme.textDim
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }
                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    visible: root.scene.state === "error" || root.scene.state === "cancelled"
                    Button {
                        text: qsTr("Reintentar")
                        onClicked: root.scene.reload()
                    }
                }
                Button {
                    Layout.alignment: Qt.AlignHCenter
                    visible: root.scene.busy
                    text: qsTr("Cancelar")
                    onClicked: root.scene.cancel()
                }
            }
        }
        Text {
            Layout.fillWidth: true
            Layout.margins: 12
            text: root.hasGeometry
                ? qsTr("Vista previa de %1 · %2 vértices · %3 triángulos (validados) · no adaptado a Skyrim · FaceGen pendiente")
                    .arg(root.scene.fileName).arg(root.scene.inspection.validated.vertices).arg(root.scene.inspection.validated.triangles)
                : qsTr("FaceGen pendiente · verificación en Skyrim pendiente")
            color: Theme.textMuted
            elide: Text.ElideRight
        }
    }
}
