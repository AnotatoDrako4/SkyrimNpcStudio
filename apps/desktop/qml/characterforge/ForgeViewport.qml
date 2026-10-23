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
    readonly property bool selectionShown: selectionBox.visible  // 3D nodes are not QObject children of the window
    // The "Empezar" card asks the workspace for these; it owns the dialogs and the tool selection.
    signal newProjectRequested()
    signal openProjectRequested()
    signal toolRequested(string tool)
    // Nothing loaded, loading or failed: the start card replaces the plain message only in the quiet state.
    readonly property bool idle: !hasGeometry && !scene.busy && scene.state !== "error" && scene.state !== "cancelled"
                                 && scene.state !== "unsupported" && !scene.fileName && !(forge.workspace.model)
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
        root.updateClipping();
    }
    // Near/far planes follow the camera distance and the model size, also after zooming
    // (OrbitCameraController's automaticClipping would reset them to fixed 0.01/0.1/1 steps).
    function updateClipping() {
        const f = root.framing();
        const radius = f ? Math.max(f.radius, 0.001) : 1;
        const distance = Math.max(Math.abs(camera.z), 0.0001);
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
        if (!root.boundsReady || loader.status !== RuntimeLoader.Success)
            return;
        root.scene.reportBounds(loader.source, loader.bounds.minimum, loader.bounds.maximum);
        root.reportParts();
    }

    // Models created by RuntimeLoader (owned by it), depth first like the glTF scene. Qt 6.11 gives
    // them no name and makes them not pickable; the forge makes them pickable for selection.
    property var models: []
    function collectModels(node, found) {
        const kids = node.children;
        for (let i = 0; i < kids.length; ++i) {
            const child = kids[i];
            if (child.materials !== undefined && child.bounds !== undefined)
                found.push(child);
            root.collectModels(child, found);
        }
        return found;
    }
    function reportParts() {
        const found = root.collectModels(loader, []);
        const report = [];
        for (let i = 0; i < found.length; ++i) {
            const m = found[i];
            m.pickable = true;
            let parent = -1;
            for (let p = m.parent; p && p !== loader && parent < 0; p = p.parent)
                parent = found.indexOf(p);
            report.push({
                materials: m.materials.length,
                skinned: m.skin !== null,
                morphTargets: m.morphTargets.length,
                minimum: m.bounds.minimum,
                maximum: m.bounds.maximum,
                parent: parent
            });
        }
        root.models = found;
        root.scene.reportParts(loader.source, report);
    }
    function applyParts() {
        const parts = root.scene.parts;
        if (parts.length !== root.models.length) {
            selectionBox.visible = false;
            return;
        }
        for (let i = 0; i < parts.length; ++i)
            root.models[i].visible = parts[i].visible;
        root.updateSelectionBox();
    }
    function pickAt(x, y) {
        const hit = view.pick(x, y);
        root.scene.selectPart(hit.objectHit ? root.models.indexOf(hit.objectHit) : -1);
    }
    // Axis-aligned box, in scene space, around the selected part's bounds.
    function updateSelectionBox() {
        const i = root.scene.selectedPart;
        const parts = root.scene.parts;
        if (i < 0 || i >= root.models.length || i >= parts.length || !parts[i].shown) {
            selectionBox.visible = false;
            return;
        }
        const m = root.models[i];
        const lo = m.bounds.minimum, hi = m.bounds.maximum;
        let min = [Infinity, Infinity, Infinity], max = [-Infinity, -Infinity, -Infinity];
        for (let c = 0; c < 8; ++c) {
            const p = m.mapPositionToScene(Qt.vector3d(c & 1 ? hi.x : lo.x, c & 2 ? hi.y : lo.y, c & 4 ? hi.z : lo.z));
            const v = [p.x, p.y, p.z];
            for (let k = 0; k < 3; ++k) {
                min[k] = Math.min(min[k], v[k]);
                max[k] = Math.max(max[k], v[k]);
            }
        }
        selectionBox.position = Qt.vector3d((min[0] + max[0]) / 2, (min[1] + max[1]) / 2, (min[2] + max[2]) / 2);
        selectionBox.size = Qt.vector3d(max[0] - min[0], max[1] - min[1], max[2] - min[2]);
        selectionBox.visible = true;
    }
    // The 12 edges of the selection box: 4 along each axis, at the corners of the other two.
    function edgePosition(i, size) {
        const axis = Math.floor(i / 4), u = (axis + 1) % 3, v = (axis + 2) % 3;
        const half = [size.x / 2, size.y / 2, size.z / 2];
        const p = [0, 0, 0];
        p[u] = (i & 1 ? 1 : -1) * half[u];
        p[v] = (i & 2 ? 1 : -1) * half[v];
        return Qt.vector3d(p[0], p[1], p[2]);
    }
    function edgeScale(i, size, thickness) {
        const axis = Math.floor(i / 4);
        const s = [thickness, thickness, thickness];
        s[axis] = [size.x, size.y, size.z][axis] + thickness;
        return Qt.vector3d(s[0] / 100, s[1] / 100, s[2] / 100);  // #Cube is 100 units wide
    }

    Connections {
        target: root.scene
        function onFrameRequested() { root.frame(0) }
        function onPartsChanged() { root.applyParts() }
    }
    Connections {
        target: camera
        function onZChanged() { root.updateClipping() }
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
                    text: root.hasGeometry ? qsTr("Clic izq: seleccionar parte · Arrastrar con botón der. o central: orbitar · Ctrl + arrastrar: desplazar · Rueda: zoom") : ""
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
                        root.models = [];  // the previous asset's Models are gone
                        selectionBox.visible = false;
                        Qt.callLater(root.reportStatus);
                    }
                    onStatusChanged: Qt.callLater(root.reportStatus)
                    onBoundsChanged: {
                        root.boundsReady = true;
                        Qt.callLater(root.reportBounds);
                    }
                }
                DefaultMaterial {
                    id: selectionMaterial
                    lighting: DefaultMaterial.NoLighting
                    diffuseColor: "#f0b44c"
                }
                Node {
                    id: selectionBox
                    objectName: "forgeSelectionBox"
                    property vector3d size: Qt.vector3d(0, 0, 0)
                    readonly property real thickness: Math.max(size.x, size.y, size.z) * 0.006
                    visible: false
                    Repeater3D {
                        model: 12
                        delegate: Model {
                            required property int index
                            source: "#Cube"
                            pickable: false
                            castsShadows: false
                            receivesShadows: false
                            position: root.edgePosition(index, selectionBox.size)
                            scale: root.edgeScale(index, selectionBox.size, selectionBox.thickness)
                            materials: [selectionMaterial]
                        }
                    }
                }
                OrbitCameraController {
                    anchors.fill: parent
                    origin: orbitOrigin
                    camera: camera
                    enabled: root.hasGeometry
                    automaticClipping: false  // root.updateClipping() keeps the planes on the model
                    acceptedButtons: Qt.RightButton | Qt.MiddleButton
                    panEnabled: true
                }
                // Click (without dragging) selects the part under the pointer; empty space clears.
                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    enabled: root.hasGeometry
                    onTapped: (eventPoint, button) => root.pickAt(eventPoint.position.x, eventPoint.position.y)
                }
            }

            BusyIndicator {
                anchors.centerIn: parent
                running: root.scene.busy
                visible: running
            }

            // Start card: real next steps only (no sample character, no step without a backend).
            EmptyState {
                objectName: "forgeStartCard"
                anchors.fill: parent
                visible: root.idle
                glyph: root.forge.hasProject ? "folder-open" : "forge"
                title: root.forge.hasProject ? qsTr("Proyecto listo: añade un modelo o define el NPC") : qsTr("Empezar en Character Forge")
                message: root.forge.hasProject
                    ? qsTr("1. Importa un modelo GLB o glTF para verlo aquí.  2. Define identidad, estadísticas y comportamiento en «NPC nativo».  3. Valida antes de exportar.")
                    : qsTr("Crea un proyecto .sncf nuevo o abre uno guardado. El borrador es independiente: nada se escribe en Skyrim ni en MO2.")
                note: qsTr("No se muestra ningún personaje de ejemplo. FaceGen nativo pendiente: la exportación jugable sigue bloqueada.")
                actions: root.forge.hasProject ? [
                    { label: qsTr("Importar modelo GLB/glTF"), trigger: () => root.toolRequested("import") },
                    { label: qsTr("Crear NPC"), trigger: () => root.toolRequested("native") },
                    { label: qsTr("Biblioteca de modelos"), trigger: () => root.toolRequested("library") }
                ] : [
                    { label: qsTr("Nuevo proyecto"), enabled: !root.forge.busy, trigger: () => root.newProjectRequested() },
                    { label: qsTr("Abrir .sncf…"), enabled: !root.forge.busy, trigger: () => root.openProjectRequested() },
                    { label: qsTr("Biblioteca de modelos"), trigger: () => root.toolRequested("library") }
                ]
            }

            // Unsupported, error and loading states (no sample character is ever shown).
            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(parent.width - 48, 480)
                spacing: 12
                visible: !root.hasGeometry && !root.idle
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
            readonly property var selected: root.scene.selectedPart >= 0 && root.scene.selectedPart < root.scene.parts.length
                ? root.scene.parts[root.scene.selectedPart] : null
            text: root.hasGeometry
                ? (selected ? qsTr("Seleccionado: %1 · ").arg(selected.label) : "")
                    + qsTr("Vista previa de %1 · %2 vértices · %3 triángulos (validados) · no adaptado a Skyrim · FaceGen pendiente")
                    .arg(root.scene.fileName).arg(root.scene.inspection.validated.vertices).arg(root.scene.inspection.validated.triangles)
                : qsTr("FaceGen pendiente · verificación en Skyrim pendiente")
            color: Theme.textMuted
            elide: Text.ElideRight
        }
    }
}
