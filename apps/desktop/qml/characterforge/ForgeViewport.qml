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
    property real pitch: -8
    readonly property bool selectionShown: selectionBox.visible  // 3D nodes are not QObject children of the window
    property alias wireframeEnabled: wireframe.checked
    property alias boundsEnabled: boundsToggle.checked
    property alias issuesEnabled: issuesToggle.checked
    property alias inspectorOpen: inspectorToggle.checked
    property alias compareOpen: compareToggle.checked
    property alias inspectorTab: visualInspectorDrawer.currentTab

    // The "Empezar" card and recovery controls ask the workspace for these; it owns the dialogs and tool selection.
    signal newProjectRequested()
    signal openProjectRequested()
    signal toolRequested(string tool)
    signal saveAndConvertRequested()
    signal convertPreviewRequested(bool force)

    // Nothing loaded, loading or failed: the start card replaces the plain message only in the quiet state.
    readonly property bool idle: !hasGeometry && !scene.busy && !forge.blenderBusy
                                 && scene.state !== "error" && scene.state !== "cancelled"
                                 && scene.state !== "unsupported" && !scene.fileName && !(forge.workspace.model)
    readonly property var modelInfo: forge.workspace.model || ({})
    readonly property var skyrimConv: forge.skyrimConversion || ({ state: "none" })
    readonly property bool skyrimAnalyzed: ["analyzed", "exported", "verificationFailed"].indexOf(skyrimConv.state) >= 0
    readonly property bool canCompare: hasGeometry && (skyrimAnalyzed || !!modelInfo.sourcePath)
    readonly property bool anyPartShown: {
        const p = scene.parts || [];
        for (let i = 0; i < p.length; ++i) {
            if (p[i].shown)
                return true;
        }
        return false;
    }

    // Track elapsed time during background Blender operations without blocking the UI thread.
    property real blenderElapsedSec: 0
    Timer {
        id: blenderElapsedTimer
        interval: 200
        repeat: true
        running: root.forge.blenderBusy
        onRunningChanged: {
            if (running)
                root.blenderElapsedSec = 0;
        }
        onTriggered: root.blenderElapsedSec += 0.2
    }

    // Match localized issues (with object name) from SkyrimConversionWizard / glTF inspection to drawable 3D parts.
    readonly property var localizedIssues: {
        const out = [];
        const clsIssues = ((root.skyrimConv.classification || {}).issues) || [];
        const parts = root.scene.parts || [];
        for (let i = 0; i < clsIssues.length; ++i) {
            const iss = clsIssues[i];
            if (!iss || iss.severity === "compatible")
                continue;
            let matchedPart = -1;
            const obj = String(iss.object || "").trim();
            if (obj.length > 0) {
                const objLower = obj.toLowerCase();
                for (let p = 0; p < parts.length; ++p) {
                    const part = parts[p];
                    const lbl = String(part.label || "").toLowerCase();
                    const nName = String(part.nodeName || "").toLowerCase();
                    const mName = String(part.meshName || "").toLowerCase();
                    if (lbl === objLower || nName === objLower || mName === objLower
                            || lbl.indexOf(objLower) >= 0 || objLower.indexOf(lbl) >= 0) {
                        matchedPart = p;
                        break;
                    }
                }
            }
            out.push({
                severity: iss.severity || "warning",
                code: iss.code || "",
                object: obj,
                problem: iss.problem || "",
                solution: iss.solution || "",
                fixAvailable: !!iss.fixAvailable,
                partIndex: matchedPart
            });
        }
        return out;
    }

    property var localizedIssueBoxes: []
    function updateIssueBoxes() {
        if (!root.hasGeometry || !root.anyPartShown || root.models.length === 0) {
            root.localizedIssueBoxes = [];
            return;
        }
        const parts = root.scene.parts || [];
        const issues = root.localizedIssues || [];
        const byPart = {};
        for (let i = 0; i < issues.length; ++i) {
            const iss = issues[i];
            const idx = iss.partIndex;
            if (idx >= 0 && idx < root.models.length && idx < parts.length && parts[idx].shown) {
                if (!byPart[idx] || iss.severity === "blocking") {
                    byPart[idx] = iss;
                }
            }
        }
        const boxes = [];
        const keys = Object.keys(byPart);
        for (let k = 0; k < keys.length; ++k) {
            const idx = Number(keys[k]);
            const box = root.sceneBoxForModel(root.models[idx]);
            if (box) {
                boxes.push({
                    partIndex: idx,
                    position: box.position,
                    size: box.size,
                    severity: byPart[idx].severity,
                    label: byPart[idx].object || parts[idx].label
                });
            }
        }
        root.localizedIssueBoxes = boxes;
    }

    color: Theme.surface

    // Camera on the rendered bounds (RuntimeLoader.bounds: "may not be available before the first
    // render", so this runs when the viewport reports them).
    // Rendered bounds when the viewport has them; until then the validated POSITION bounds (the
    // accessors' min/max, node transforms not applied) so the model is at least in view.
    function framing() {
        const r = root.scene.rendered;
        if (r.center !== undefined)
            return { center: r.center, radius: r.radius, size: r.size || Qt.vector3d(r.radius, r.radius, r.radius) };
        const b = root.scene.inspection.positionBounds || [];
        if (b.length !== 6)
            return null;
        const size = Qt.vector3d(b[3] - b[0], b[4] - b[1], b[5] - b[2]);
        return {
            center: Qt.vector3d((b[0] + b[3]) / 2, (b[1] + b[4]) / 2, (b[2] + b[5]) / 2),
            radius: size.length() / 2,
            size: size
        };
    }
    function frame(newYaw, newPitch) {
        const f = root.framing();
        if (!f || root.scene.state !== "loaded")
            return;
        const center = f.center;
        const radius = Math.max(f.radius, 0.001);
        const halfFov = camera.fieldOfView * Math.PI / 360;
        orbitOrigin.position = center;
        root.yaw = newYaw === undefined ? root.yaw : newYaw;
        root.pitch = newPitch === undefined ? root.pitch : newPitch;
        orbitOrigin.eulerRotation = Qt.vector3d(root.pitch, root.yaw, 0);
        const distance = radius / Math.sin(halfFov) * 1.15;
        camera.position = Qt.vector3d(0, 0, distance);
        camera.eulerRotation = Qt.vector3d(0, 0, 0);
        root.updateClipping();
    }
    function framePart(partIndex) {
        if (partIndex < 0 || partIndex >= root.models.length || root.scene.state !== "loaded") {
            root.frame();
            return;
        }
        const box = root.sceneBoxForModel(root.models[partIndex]);
        if (!box) {
            root.frame();
            return;
        }
        const radius = Math.max(box.size.length() / 2, 0.001);
        const halfFov = camera.fieldOfView * Math.PI / 360;
        orbitOrigin.position = box.position;
        orbitOrigin.eulerRotation = Qt.vector3d(root.pitch, root.yaw, 0);
        const distance = radius / Math.sin(halfFov) * 1.25;
        camera.position = Qt.vector3d(0, 0, distance);
        camera.eulerRotation = Qt.vector3d(0, 0, 0);
        root.updateClipping();
    }
    function focusPartByName(objectName) {
        if (!objectName)
            return;
        const target = String(objectName).trim().toLowerCase();
        const parts = root.scene.parts || [];
        for (let i = 0; i < parts.length; ++i) {
            const p = parts[i];
            const lbl = String(p.label || "").toLowerCase();
            const nName = String(p.nodeName || "").toLowerCase();
            const mName = String(p.meshName || "").toLowerCase();
            if (lbl === target || nName === target || mName === target
                    || lbl.indexOf(target) >= 0 || target.indexOf(lbl) >= 0) {
                if (!p.visible)
                    root.scene.setPartVisible(i, true);
                root.scene.selectPart(i);
                issuesToggle.checked = true;
                root.framePart(i);
                return;
            }
        }
    }
    function isPartIsolated(partIndex) {
        const parts = root.scene.parts || [];
        if (partIndex < 0 || partIndex >= parts.length || parts.length <= 1)
            return false;
        if (!parts[partIndex].visible)
            return false;
        // Build ancestor set so parents required to keep partIndex visible are allowed
        const keep = {};
        let cur = partIndex;
        while (cur >= 0 && cur < parts.length) {
            keep[cur] = true;
            cur = parts[cur].parent !== undefined ? parts[cur].parent : -1;
        }
        let anyHidden = false;
        for (let i = 0; i < parts.length; ++i) {
            if (!keep[i]) {
                if (parts[i].visible)
                    return false;
                anyHidden = true;
            }
        }
        return anyHidden;
    }
    function isolatePart(partIndex) {
        const parts = root.scene.parts || [];
        if (partIndex < 0 || partIndex >= parts.length)
            return;
        if (root.isPartIsolated(partIndex)) {
            root.scene.showAllParts();
            return;
        }
        const keep = {};
        let cur = partIndex;
        while (cur >= 0 && cur < parts.length) {
            keep[cur] = true;
            cur = parts[cur].parent !== undefined ? parts[cur].parent : -1;
        }
        for (let i = 0; i < parts.length; ++i) {
            root.scene.setPartVisible(i, !!keep[i]);
        }
        root.scene.selectPart(partIndex);
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
        boundsToggle.checked = false;
        issuesToggle.checked = false;
        root.pitch = -8;
        root.frame(0, -8);
    }
    function reportStatus() {
        if (!root.scene)
            return;
        root.scene.reportLoaderStatus(loader.source, loader.status, loader.errorString);
    }
    // Qt 6.11 computes RuntimeLoader.bounds lazily and marks them clean even when the meshes are
    // not uploaded yet; boundsChanged is only emitted while they are dirty. Reading "bounds" before
    // that signal would store empty bounds and suppress the signal, so they are read only after it.
    property bool boundsReady: false
    function reportBounds() {
        if (!root.scene || !root.boundsReady || loader.status !== RuntimeLoader.Success)
            return;
        root.scene.reportBounds(loader.source, loader.bounds.minimum, loader.bounds.maximum);
        root.reportParts();
    }

    // Models created by RuntimeLoader (owned by it), depth first like the glTF scene. Qt 6.11 gives
    // them no name and makes them not pickable; the forge makes them pickable for selection.
    property var models: []
    function collectModels(node, found) {
        if (!node || !node.children)
            return found;
        const kids = node.children;
        for (let i = 0; i < kids.length; ++i) {
            const child = kids[i];
            if (!child)
                continue;
            if (child.materials !== undefined && child.bounds !== undefined)
                found.push(child);
            root.collectModels(child, found);
        }
        return found;
    }
    function reportParts() {
        if (!root.scene || loader.status !== RuntimeLoader.Success)
            return;
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
        if (!root.scene)
            return;
        const parts = root.scene.parts;
        if (parts.length !== root.models.length) {
            selectionBox.visible = false;
            root.localizedIssueBoxes = [];
            return;
        }
        for (let i = 0; i < parts.length; ++i) {
            if (root.models[i])
                root.models[i].visible = parts[i].visible;
        }
        root.updateSelectionBox();
        root.updateIssueBoxes();
    }
    function pickAt(x, y) {
        const hit = view.pick(x, y);
        root.scene.selectPart(hit.objectHit ? root.models.indexOf(hit.objectHit) : -1);
    }
    function sceneBoxForModel(m) {
        if (!m || !m.bounds)
            return null;
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
        return {
            position: Qt.vector3d((min[0] + max[0]) / 2, (min[1] + max[1]) / 2, (min[2] + max[2]) / 2),
            size: Qt.vector3d(max[0] - min[0], max[1] - min[1], max[2] - min[2])
        };
    }
    // Axis-aligned box, in scene space, around the selected part's bounds.
    function updateSelectionBox() {
        const i = root.scene.selectedPart;
        const parts = root.scene.parts;
        if (i < 0 || i >= root.models.length || i >= parts.length || !parts[i].shown) {
            selectionBox.visible = false;
            return;
        }
        const box = root.sceneBoxForModel(root.models[i]);
        if (!box) {
            selectionBox.visible = false;
            return;
        }
        selectionBox.position = box.position;
        selectionBox.size = box.size;
        selectionBox.visible = true;
    }
    // The 12 edges of a 3D bounding box: 4 along each axis, at the corners of the other two.
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
        function onFrameRequested() { root.frame(0, -8) }
        function onPartsChanged() { root.applyParts() }
    }
    Connections {
        target: root.forge
        function onChanged() { root.updateIssueBoxes() }
    }
    Connections {
        target: camera
        function onZChanged() { root.updateClipping() }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---- Top Viewport Toolbar: Camera views, overlays, Visual Inspector, Comparison mode ----
        ToolBar {
            Layout.fillWidth: true
            RowLayout {
                anchors.fill: parent
                spacing: 2
                ToolButton {
                    text: qsTr("Encuadrar")
                    enabled: root.scene.state === "loaded"
                    onClicked: root.scene.selectedPart >= 0 ? root.framePart(root.scene.selectedPart) : root.frame()
                    ToolTip.visible: hovered
                    ToolTip.text: root.scene.selectedPart >= 0
                        ? qsTr("Centra la cámara en la pieza seleccionada conservando el ángulo")
                        : qsTr("Centra la cámara en el modelo conservando el ángulo")
                }
                ToolButton {
                    text: qsTr("Frontal")
                    enabled: root.hasGeometry
                    onClicked: root.frame(0, 0)
                }
                ToolButton {
                    text: qsTr("Lateral")
                    enabled: root.hasGeometry
                    onClicked: root.frame(90, 0)
                }
                ToolButton {
                    text: qsTr("Posterior")
                    enabled: root.hasGeometry
                    onClicked: root.frame(180, 0)
                }
                ToolButton {
                    text: qsTr("Perspectiva")
                    enabled: root.hasGeometry
                    onClicked: root.frame(35, -18)
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Vista isométrica / perspectiva tres cuartos (35°, -18°)")
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
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Modo alámbrico (wireframe) sobre las mallas en el visor 3D")
                }
                CheckBox {
                    id: boundsToggle
                    text: qsTr("Caja")
                    enabled: root.hasGeometry
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Muestra la caja delimitadora (bounding box) completa del modelo y sus dimensiones")
                }
                CheckBox {
                    id: issuesToggle
                    text: root.localizedIssueBoxes.length > 0
                        ? qsTr("Problemas (%1)").arg(root.localizedIssueBoxes.length)
                        : qsTr("Problemas")
                    enabled: root.hasGeometry && root.localizedIssueBoxes.length > 0
                    ToolTip.visible: hovered
                    ToolTip.text: root.localizedIssueBoxes.length > 0
                        ? qsTr("Resalta en 3D las mallas con problemas diagnosticados para Skyrim")
                        : qsTr("Ejecuta «Analizar compatibilidad con Skyrim» para localizar problemas por malla")
                }
                CheckBox {
                    text: qsTr("Huesos")
                    enabled: false
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("RuntimeLoader de Qt 6.11 no expone transformaciones mundiales de Joint en el grafo 3D; consulta la jerarquía real de huesos en «Inspector visual → Esqueleto» o en la herramienta «Esqueleto».")
                }
                ToolButton {
                    id: inspectorToggle
                    text: qsTr("Inspector visual")
                    checkable: true
                    enabled: root.hasGeometry
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Aislar mallas, mostrar/ocultar piezas, materiales, huesos y problemas localizados")
                }
                ToolButton {
                    id: compareToggle
                    text: qsTr("Original / adaptado")
                    checkable: true
                    enabled: root.canCompare
                    ToolTip.visible: hovered
                    ToolTip.text: root.canCompare
                        ? qsTr("Compara geometría, escala y materiales entre el modelo original y la preparación para Skyrim SE")
                        : qsTr("Disponible cuando exista un análisis/exportación para Skyrim o un modelo convertido desde Blender.")
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: root.hasGeometry ? qsTr("Clic izq: seleccionar parte · Arrastrar der./central: orbitar · Ctrl+arrastrar: desplazar · Rueda: zoom") : ""
                    color: Theme.textMuted
                    font.pixelSize: Theme.small
                    elide: Text.ElideRight
                    Layout.maximumWidth: 420
                }
            }
        }

        // ---- Non-blocking background Blender activity bar when 3D geometry is already visible ----
        Rectangle {
            Layout.fillWidth: true
            visible: root.hasGeometry && root.forge.blenderBusy
            implicitHeight: busyStripRow.implicitHeight + Theme.spacingSm * 2
            color: Theme.panelAlt
            border.color: Theme.accentBorder
            border.width: 1
            RowLayout {
                id: busyStripRow
                anchors.fill: parent
                anchors.margins: Theme.spacingSm
                spacing: Theme.spacingMd
                BusyIndicator {
                    Layout.preferredWidth: 20
                    Layout.preferredHeight: 20
                    running: root.forge.blenderBusy
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        Layout.fillWidth: true
                        text: root.forge.statusMessage || qsTr("Ejecutando operación en Blender en segundo plano…")
                        color: Theme.text
                        font.pixelSize: Theme.small
                        font.bold: true
                        elide: Text.ElideRight
                    }
                    ProgressBar {
                        Layout.fillWidth: true
                        indeterminate: !(root.skyrimConv.fraction > 0)
                        from: 0
                        to: 1
                        value: root.skyrimConv.fraction || 0
                    }
                }
                Text {
                    text: root.blenderElapsedSec.toFixed(1) + " s"
                    color: Theme.textDim
                    font.family: Theme.mono
                    font.pixelSize: Theme.small
                }
                Button {
                    text: qsTr("Cancelar")
                    onClicked: root.forge.cancelOperation()
                }
            }
        }

        // ---- Comparison Mode Panel: Original vs Preparado para Skyrim (outside View3D) ----
        Rectangle {
            id: comparisonBanner
            objectName: "forgeComparisonPanel"
            Layout.fillWidth: true
            visible: compareToggle.checked && root.canCompare
            implicitHeight: compareCol.implicitHeight + Theme.spacingMd * 2
            color: Theme.panel
            border.color: Theme.border
            border.width: 1
            ColumnLayout {
                id: compareCol
                anchors.fill: parent
                anchors.margins: Theme.spacingMd
                spacing: Theme.spacingSm
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: qsTr("Comparación técnica: Original vs Preparado para Skyrim SE")
                        color: Theme.text
                        font.bold: true
                    }
                    Item { Layout.fillWidth: true }
                    Button {
                        visible: !!root.modelInfo.sourcePath && !root.forge.busy
                        text: qsTr("Reconvertir GLB desde .blend")
                        font.pixelSize: Theme.small
                        onClicked: root.convertPreviewRequested(true)
                    }
                    Button {
                        visible: !root.skyrimAnalyzed
                        text: qsTr("Abrir asistente Skyrim…")
                        font.pixelSize: Theme.small
                        onClicked: root.toolRequested("skyrim")
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingMd
                    // Card 1: Original
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        implicitHeight: origCol.implicitHeight + Theme.spacingMd * 2
                        radius: Theme.radius
                        color: Theme.panelAlt
                        border.color: Theme.borderSubtle
                        ColumnLayout {
                            id: origCol
                            anchors.fill: parent
                            anchors.margins: Theme.spacingMd
                            spacing: 3
                            readonly property var tot: root.skyrimConv.totals || ({})
                            readonly property var val: root.scene.inspection.validated || ({})
                            readonly property var dec: root.scene.inspection.declared || ({})
                            readonly property var dim: root.skyrimConv.dimensions || []
                            readonly property var rSize: root.scene.rendered.size || Qt.vector3d(0, 0, 0)
                            Text {
                                text: qsTr("1. ESCENA ORIGINAL (FUENTE)")
                                color: Theme.accent
                                font.bold: true
                                font.pixelSize: Theme.small
                            }
                            Text {
                                Layout.fillWidth: true
                                elide: Text.ElideMiddle
                                color: Theme.text
                                font.pixelSize: Theme.small
                                text: qsTr("Origen: %1 (%2)")
                                    .arg(root.modelInfo.sourcePath || root.modelInfo.path || root.scene.fileName)
                                    .arg(String(root.modelInfo.sourceFormat || root.modelInfo.format || "glb").toUpperCase())
                            }
                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                color: Theme.textDim
                                font.pixelSize: Theme.small
                                text: qsTr("Geometría: %1 malla(s) · %2 vértices · %3 triángulos · %4 shape keys")
                                    .arg(origCol.tot.meshes !== undefined ? origCol.tot.meshes : (origCol.dec.meshes || root.scene.parts.length))
                                    .arg(origCol.tot.vertices !== undefined ? origCol.tot.vertices : (origCol.val.vertices || 0))
                                    .arg(origCol.tot.triangles !== undefined ? origCol.tot.triangles : (origCol.val.triangles || 0))
                                    .arg(origCol.tot.shapeKeys || origCol.dec.morphTargets || 0)
                            }
                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                color: Theme.textDim
                                font.pixelSize: Theme.small
                                text: origCol.dim.length === 3
                                    ? qsTr("Escala / Dimensiones (Blender): %1 × %2 × %3 u (metros)")
                                        .arg(Number(origCol.dim[0]).toFixed(3)).arg(Number(origCol.dim[1]).toFixed(3)).arg(Number(origCol.dim[2]).toFixed(3))
                                    : qsTr("Dimensiones visor GLB: %1 × %2 × %3 u")
                                        .arg(origCol.rSize.x.toFixed(3)).arg(origCol.rSize.y.toFixed(3)).arg(origCol.rSize.z.toFixed(3))
                            }
                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                color: Theme.textDim
                                font.pixelSize: Theme.small
                                text: qsTr("Materiales: %1 material(es) PBR/BSDF · %2 textura(s) · %3 armature(s)")
                                    .arg(origCol.tot.materials !== undefined ? origCol.tot.materials : (origCol.dec.materials || 0))
                                    .arg(origCol.tot.images !== undefined ? origCol.tot.images : (origCol.dec.textures || 0))
                                    .arg(origCol.tot.armatures !== undefined ? origCol.tot.armatures : (origCol.dec.skins || 0))
                            }
                        }
                    }
                    // Card 2: Skyrim Prepared
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        implicitHeight: skyCol.implicitHeight + Theme.spacingMd * 2
                        radius: Theme.radius
                        color: Theme.panelAlt
                        border.color: root.skyrimConv.state === "exported" ? Theme.ok : Theme.accentBorder
                        ColumnLayout {
                            id: skyCol
                            anchors.fill: parent
                            anchors.margins: Theme.spacingMd
                            spacing: 3
                            readonly property var ver: root.skyrimConv.verification || ({})
                            readonly property var prep: root.skyrimConv.prepare || ({})
                            readonly property var cls: root.skyrimConv.classification || ({})
                            readonly property var cnt: cls.counts || ({})
                            readonly property var bMin: ver.boundsMin || []
                            readonly property var bMax: ver.boundsMax || []
                            Text {
                                text: qsTr("2. PREPARADO PARA SKYRIM SE (TIPO A / NIF)")
                                color: root.skyrimConv.state === "exported" ? Theme.ok : Theme.warn
                                font.bold: true
                                font.pixelSize: Theme.small
                            }
                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                color: Theme.text
                                font.pixelSize: Theme.small
                                text: root.skyrimConv.state === "exported"
                                    ? qsTr("Estado: NIF exportado y releído con nifly (%1)")
                                        .arg(root.skyrimConv.modState === "done" ? "PAQUETE_MO2_VERIFICADO" : "INTEGRACION_WINDOWS_OK")
                                    : root.skyrimAnalyzed
                                    ? qsTr("Estado: Diagnosticado (%1 bloqueantes · %2 auto-corregibles · %3 advertencias)")
                                        .arg(skyCol.cnt.blocking || 0).arg(skyCol.cnt.autoFixable || 0).arg(skyCol.cnt.warning || 0)
                                    : qsTr("Estado: Pendiente de ejecutar «Analizar compatibilidad con Skyrim»")
                            }
                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                color: Theme.textDim
                                font.pixelSize: Theme.small
                                text: skyCol.ver.vertices !== undefined
                                    ? qsTr("Geometría NIF: %1 BSTriShape(s) · %2 vértices · %3 triángulos · %4 malla(s) reducidas (Decimate)")
                                        .arg((skyCol.ver.shapes || []).length)
                                        .arg(skyCol.ver.vertices || 0)
                                        .arg(skyCol.ver.triangles || 0)
                                        .arg((skyCol.prep.decimated || []).length)
                                    : qsTr("Geometría tras preparación: triangulación + Decimate automático si supera 65 535 vértices/malla")
                            }
                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                color: Theme.textDim
                                font.pixelSize: Theme.small
                                text: skyCol.bMin.length === 3 && skyCol.bMax.length === 3
                                    ? qsTr("Escala NIF: %1 × %2 × %3 unidades Skyrim (%4 malla(s) escaladas ×100)")
                                        .arg((Number(skyCol.bMax[0]) - Number(skyCol.bMin[0])).toFixed(1))
                                        .arg((Number(skyCol.bMax[1]) - Number(skyCol.bMin[1])).toFixed(1))
                                        .arg((Number(skyCol.bMax[2]) - Number(skyCol.bMin[2])).toFixed(1))
                                        .arg((skyCol.prep.scaledMeshes || []).length)
                                    : qsTr("Escala objetivo: rotación/escala aplicadas + conversión automática metros → unidades Skyrim (×100)")
                            }
                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                color: Theme.textDim
                                font.pixelSize: Theme.small
                                text: (skyCol.prep.textures || []).length > 0
                                    ? qsTr("Materiales NIF: BSLightingShaderProperty · %1 textura(s) DDS en textures\\CharacterForge\\")
                                        .arg((skyCol.prep.textures || []).length)
                                    : qsTr("Materiales NIF: conversión de Principled BSDF a BSLightingShaderProperty + texturas DDS BC1/BC3")
                            }
                        }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.warn
                    font.pixelSize: Theme.small
                    text: qsTr("Aviso: La vista previa 3D utiliza Qt Quick 3D sobre el GLB intermedio y NO coincide exactamente con lo que dibujará Skyrim SE (BSLightingShaderProperty, espacio tangente e iluminación del Creation Engine siguen PENDIENTE_SKYRIM).")
                }
            }
        }

        // ---- Main Center Area: Pure 3D Viewport + Collapsible Visual Inspector Drawer ----
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

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
                            root.localizedIssueBoxes = [];
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
                    DefaultMaterial {
                        id: boundsMaterial
                        lighting: DefaultMaterial.NoLighting
                        diffuseColor: "#38bdf8"
                    }
                    DefaultMaterial {
                        id: blockingIssueMaterial
                        lighting: DefaultMaterial.NoLighting
                        diffuseColor: "#ef4444"
                    }
                    DefaultMaterial {
                        id: warningIssueMaterial
                        lighting: DefaultMaterial.NoLighting
                        diffuseColor: "#f59e0b"
                    }
                    // Selected Part Bounding Box
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
                    // Full Model Bounding Box (toggled by "Caja")
                    Node {
                        id: modelBoundsBox
                        objectName: "forgeModelBoundsBox"
                        readonly property var f: root.framing()
                        readonly property vector3d size: f && f.size ? f.size : Qt.vector3d(0, 0, 0)
                        readonly property real thickness: Math.max(size.x, size.y, size.z) * 0.005
                        position: f && f.center ? f.center : Qt.vector3d(0, 0, 0)
                        visible: boundsToggle.checked && root.hasGeometry && root.anyPartShown && size.length() > 0
                        Repeater3D {
                            model: 12
                            delegate: Model {
                                required property int index
                                source: "#Cube"
                                pickable: false
                                castsShadows: false
                                receivesShadows: false
                                position: root.edgePosition(index, modelBoundsBox.size)
                                scale: root.edgeScale(index, modelBoundsBox.size, modelBoundsBox.thickness)
                                materials: [boundsMaterial]
                            }
                        }
                    }
                    // Localized Geometry Issue Bounding Boxes (toggled by "Problemas")
                    Node {
                        id: issueBoxesRoot
                        visible: issuesToggle.checked && root.hasGeometry && root.anyPartShown
                        Repeater3D {
                            model: root.localizedIssueBoxes.length
                            delegate: Node {
                                id: issueBoxNode
                                required property int index
                                readonly property var boxData: root.localizedIssueBoxes[index] || ({
                                    position: Qt.vector3d(0, 0, 0),
                                    size: Qt.vector3d(0, 0, 0),
                                    severity: "warning"
                                })
                                readonly property vector3d boxSize: boxData.size || Qt.vector3d(0, 0, 0)
                                readonly property real thickness: Math.max(boxSize.x, boxSize.y, boxSize.z) * 0.008
                                position: boxData.position || Qt.vector3d(0, 0, 0)
                                Repeater3D {
                                    model: 12
                                    delegate: Model {
                                        required property int index
                                        source: "#Cube"
                                        pickable: false
                                        castsShadows: false
                                        receivesShadows: false
                                        position: root.edgePosition(index, issueBoxNode.boxSize)
                                        scale: root.edgeScale(index, issueBoxNode.boxSize, issueBoxNode.thickness)
                                        materials: [issueBoxNode.boxData.severity === "blocking" ? blockingIssueMaterial : warningIssueMaterial]
                                    }
                                }
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
                    running: root.scene.busy || (!root.hasGeometry && root.forge.blenderBusy)
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
                        ? qsTr("1. Importa un modelo (.blend, .glb, .gltf, .fbx, .obj) para verlo aquí.  2. Define identidad, estadísticas y comportamiento en «NPC nativo».  3. Valida antes de exportar.")
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

                // Unsupported, converting, error and loading states (no sample character is ever shown).
                ColumnLayout {
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 48, 520)
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
                        visible: !!root.modelInfo.format
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        color: Theme.textMuted
                        font.pixelSize: Theme.small
                        text: root.modelInfo.format
                            ? qsTr("Formato %1 · %2 MiB")
                                .arg(String(root.modelInfo.format).toUpperCase())
                                .arg(((root.modelInfo.sizeBytes || 0) / 1048576).toFixed(2))
                            : ""
                    }
                    Text {
                        objectName: "forgeViewportMessage"
                        Layout.fillWidth: true
                        text: root.forge.blenderBusy
                            ? (root.forge.statusMessage || qsTr("Convirtiendo %1 a GLB mediante el puente de Blender…").arg(root.modelInfo.name || ""))
                            : root.scene.message.length ? root.scene.message
                            : qsTr("Crea un proyecto e importa un modelo GLB o glTF para verlo aquí. No se muestra ningún personaje de ejemplo.")
                        color: (root.scene.state === "error" || root.forge.lastError.length > 0) ? Theme.warn : Theme.textDim
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                    }
                    // Recoverable error / status detail from CharacterForgeViewModel (e.g. Blender stderr or missing .sncf)
                    Text {
                        Layout.fillWidth: true
                        visible: !root.forge.blenderBusy && root.forge.lastError.length > 0 && root.forge.lastError !== root.scene.message
                        text: root.forge.lastError
                        color: Theme.warn
                        font.pixelSize: Theme.small
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                    }
                    // Live progress bar & timer during Blender conversion
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: root.forge.blenderBusy
                        spacing: 6
                        ProgressBar {
                            Layout.fillWidth: true
                            indeterminate: true
                        }
                        Text {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            color: Theme.textDim
                            font.family: Theme.mono
                            font.pixelSize: Theme.small
                            text: qsTr("Tiempo transcurrido: %1 s · Conversión en segundo plano (interfaz libre)")
                                .arg(root.blenderElapsedSec.toFixed(1))
                        }
                    }
                    // Actionable buttons for BLEND/FBX/OBJ preview conversion & error recovery
                    Flow {
                        Layout.alignment: Qt.AlignHCenter
                        spacing: 8
                        visible: !root.forge.blenderBusy && !root.scene.busy
                                 && (root.scene.state === "unsupported" || root.scene.state === "error" || root.scene.state === "cancelled")
                        Button {
                            visible: root.scene.state === "unsupported" && root.forge.projectPath.length === 0
                            highlighted: true
                            text: qsTr("Guardar proyecto .sncf y convertir vista previa…")
                            onClicked: root.saveAndConvertRequested()
                        }
                        Button {
                            visible: root.scene.state === "unsupported" && root.forge.projectPath.length > 0
                                     && (!root.forge.workspace.blender || !root.forge.workspace.blender.executable)
                            highlighted: true
                            text: qsTr("Detectar Blender y convertir a GLB")
                            onClicked: {
                                root.forge.detectBlender();
                                root.convertPreviewRequested(true);
                            }
                        }
                        Button {
                            visible: root.scene.state === "unsupported" && root.forge.projectPath.length > 0
                                     && !!root.forge.workspace.blender && !!root.forge.workspace.blender.executable
                            highlighted: true
                            text: root.forge.lastError.length > 0
                                ? qsTr("Reintentar conversión a GLB")
                                : qsTr("Convertir a GLB y mostrar en el visor")
                            onClicked: root.convertPreviewRequested(true)
                        }
                        Button {
                            visible: root.scene.state === "unsupported"
                            text: qsTr("Configurar Blender…")
                            onClicked: root.toolRequested("blender")
                        }
                        Button {
                            visible: root.scene.state === "error" || root.scene.state === "cancelled"
                            text: qsTr("Reintentar")
                            onClicked: root.scene.reload()
                        }
                    }
                    RowLayout {
                        Layout.alignment: Qt.AlignHCenter
                        spacing: 8
                        visible: root.scene.busy || root.forge.blenderBusy
                        Button {
                            visible: root.scene.busy
                            text: qsTr("Cancelar")
                            onClicked: root.scene.cancel()
                        }
                        Button {
                            visible: root.forge.blenderBusy
                            text: qsTr("Cancelar conversión")
                            onClicked: root.forge.cancelOperation()
                        }
                    }
                }
            }

            // ---- Collapsible Visual Inspector Drawer (sibling of View3D, never pollutes 3D canvas) ----
            Rectangle {
                id: visualInspectorDrawer
                objectName: "forgeVisualInspector"
                visible: inspectorToggle.checked && root.hasGeometry
                Layout.preferredWidth: 310
                Layout.fillHeight: true
                color: Theme.panel
                border.color: Theme.border
                border.width: 1

                property int currentTab: 0  // 0: Mallas, 1: Materiales, 2: Esqueleto, 3: Problemas

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingSm
                    spacing: Theme.spacingSm

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: qsTr("Inspector visual 3D")
                            color: Theme.text
                            font.bold: true
                        }
                        Item { Layout.fillWidth: true }
                        ToolButton {
                            text: "✕"
                            onClicked: inspectorToggle.checked = false
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Cerrar inspector visual")
                        }
                    }

                    // Tab selector
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Repeater {
                            model: [
                                qsTr("Mallas (%1)").arg(root.scene.parts.length),
                                qsTr("Mat. (%1)").arg(((root.scene.inspection.materials || []).length)),
                                qsTr("Huesos (%1)").arg(((root.scene.inspection.declared || {}).joints || 0)),
                                qsTr("Alertas (%1)").arg(root.localizedIssues.length)
                            ]
                            Button {
                                required property var modelData
                                required property int index
                                Layout.fillWidth: true
                                text: modelData
                                highlighted: visualInspectorDrawer.currentTab === index
                                font.pixelSize: Theme.small
                                onClicked: visualInspectorDrawer.currentTab = index
                            }
                        }
                    }

                    // Tab 0: Meshes / Parts (Isolate, Show/Hide, Select, Frame)
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: visualInspectorDrawer.currentTab === 0
                        spacing: Theme.spacingXs

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Button {
                                Layout.fillWidth: true
                                text: qsTr("Mostrar todas")
                                enabled: root.scene.parts.some(p => !p.visible)
                                font.pixelSize: Theme.small
                                onClicked: root.scene.showAllParts()
                            }
                            Button {
                                Layout.fillWidth: true
                                text: root.isPartIsolated(root.scene.selectedPart)
                                    ? qsTr("Salir de aislar")
                                    : qsTr("Aislar pieza")
                                enabled: root.scene.selectedPart >= 0 && root.scene.parts.length > 1
                                font.pixelSize: Theme.small
                                onClicked: root.isolatePart(root.scene.selectedPart)
                            }
                        }

                        ScrollView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            contentWidth: availableWidth
                            ColumnLayout {
                                width: parent.width
                                spacing: 2
                                Repeater {
                                    model: root.scene.parts
                                    delegate: Rectangle {
                                        id: vPartCard
                                        required property var modelData
                                        Layout.fillWidth: true
                                        implicitHeight: vPartRow.implicitHeight + 6
                                        radius: Theme.radius
                                        color: root.scene.selectedPart === modelData.index ? Theme.accentSubtle : Theme.panelAlt
                                        border.color: root.scene.selectedPart === modelData.index ? Theme.accentBorder : Theme.borderSubtle
                                        RowLayout {
                                            id: vPartRow
                                            anchors.fill: parent
                                            anchors.margins: 3
                                            spacing: 4
                                            CheckBox {
                                                checked: vPartCard.modelData.visible
                                                onToggled: root.scene.setPartVisible(vPartCard.modelData.index, checked)
                                                ToolTip.visible: hovered
                                                ToolTip.text: qsTr("Mostrar u ocultar esta malla")
                                            }
                                            ColumnLayout {
                                                Layout.fillWidth: true
                                                spacing: 0
                                                Text {
                                                    Layout.fillWidth: true
                                                    elide: Text.ElideRight
                                                    color: Theme.text
                                                    font.pixelSize: Theme.small
                                                    font.bold: root.scene.selectedPart === vPartCard.modelData.index
                                                    text: vPartCard.modelData.label
                                                }
                                                Text {
                                                    Layout.fillWidth: true
                                                    elide: Text.ElideRight
                                                    color: Theme.textDim
                                                    font.pixelSize: Theme.small
                                                    text: (vPartCard.modelData.triangles !== undefined
                                                            ? qsTr("%1 tri · %2 vért").arg(vPartCard.modelData.triangles).arg(vPartCard.modelData.vertices || 0)
                                                            : qsTr("%1 mat").arg(vPartCard.modelData.materials))
                                                          + (vPartCard.modelData.skinned ? qsTr(" · skin") : "")
                                                }
                                            }
                                            ToolButton {
                                                text: root.isPartIsolated(vPartCard.modelData.index) ? qsTr("Todo") : qsTr("Solo")
                                                visible: root.scene.parts.length > 1
                                                font.pixelSize: Theme.small
                                                onClicked: root.isolatePart(vPartCard.modelData.index)
                                                ToolTip.visible: hovered
                                                ToolTip.text: qsTr("Ver esta malla por separado (aislar)")
                                            }
                                            ToolButton {
                                                text: "⌖"
                                                font.pixelSize: Theme.small
                                                onClicked: {
                                                    root.scene.selectPart(vPartCard.modelData.index);
                                                    root.framePart(vPartCard.modelData.index);
                                                }
                                                ToolTip.visible: hovered
                                                ToolTip.text: qsTr("Seleccionar y encuadrar esta malla")
                                            }
                                        }
                                        TapHandler {
                                            onTapped: root.scene.selectPart(
                                                root.scene.selectedPart === vPartCard.modelData.index ? -1 : vPartCard.modelData.index)
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // Tab 1: Materials & Textures list
                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: visualInspectorDrawer.currentTab === 1
                        contentWidth: availableWidth
                        ColumnLayout {
                            width: parent.width
                            spacing: Theme.spacingXs
                            Text {
                                visible: !(root.scene.inspection.materials || []).length
                                text: qsTr("El modelo no declara materiales glTF.")
                                color: Theme.textMuted
                                font.pixelSize: Theme.small
                            }
                            Repeater {
                                model: root.scene.inspection.materials || []
                                delegate: Rectangle {
                                    required property var modelData
                                    required property int index
                                    Layout.fillWidth: true
                                    implicitHeight: matCol.implicitHeight + 10
                                    radius: Theme.radius
                                    color: Theme.panelAlt
                                    border.color: Theme.borderSubtle
                                    ColumnLayout {
                                        id: matCol
                                        anchors.fill: parent
                                        anchors.margins: 5
                                        spacing: 2
                                        Text {
                                            Layout.fillWidth: true
                                            elide: Text.ElideRight
                                            color: Theme.text
                                            font.bold: true
                                            font.pixelSize: Theme.small
                                            text: (index + 1) + ". " + (modelData.name || qsTr("Material sin nombre"))
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            wrapMode: Text.WordWrap
                                            color: Theme.textDim
                                            font.pixelSize: Theme.small
                                            text: qsTr("Alfa: %1 · Doble cara: %2")
                                                .arg(modelData.alphaMode || "OPAQUE")
                                                .arg(modelData.doubleSided ? qsTr("sí") : qsTr("no"))
                                        }
                                    }
                                }
                            }
                            Text {
                                visible: (root.scene.inspection.images || []).length > 0
                                text: qsTr("Imágenes / Texturas (%1)").arg((root.scene.inspection.images || []).length)
                                color: Theme.text
                                font.bold: true
                                font.pixelSize: Theme.small
                            }
                            Repeater {
                                model: root.scene.inspection.images || []
                                delegate: Text {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    elide: Text.ElideMiddle
                                    color: Theme.textDim
                                    font.pixelSize: Theme.small
                                    text: "• " + (modelData.name || modelData.uri || modelData.mimeType || qsTr("Textura embebida"))
                                }
                            }
                        }
                    }

                    // Tab 2: Skeleton / Joints
                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: visualInspectorDrawer.currentTab === 2
                        contentWidth: availableWidth
                        ColumnLayout {
                            width: parent.width
                            spacing: Theme.spacingXs
                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                color: Theme.textMuted
                                font.pixelSize: Theme.small
                                text: qsTr("Nota de contrato: RuntimeLoader (Qt 6.11) no expone los nodos Joint como QQuick3DNode con transformaciones en vivo. Se muestra la jerarquía real extraída del activo.")
                            }
                            Text {
                                visible: !(root.scene.inspection.skins || []).length
                                text: qsTr("Este modelo no contiene armatures ni huesos (recurso estático).")
                                color: Theme.textDim
                                font.pixelSize: Theme.small
                                wrapMode: Text.WordWrap
                            }
                            Repeater {
                                model: root.scene.inspection.skins || []
                                delegate: Rectangle {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    implicitHeight: skCol.implicitHeight + 10
                                    radius: Theme.radius
                                    color: Theme.panelAlt
                                    border.color: Theme.borderSubtle
                                    ColumnLayout {
                                        id: skCol
                                        anchors.fill: parent
                                        anchors.margins: 5
                                        spacing: 3
                                        Text {
                                            Layout.fillWidth: true
                                            color: Theme.text
                                            font.bold: true
                                            font.pixelSize: Theme.small
                                            text: qsTr("Armature: %1 (%2 huesos)")
                                                .arg(modelData.name || qsTr("sin nombre"))
                                                .arg((modelData.joints || []).length)
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            wrapMode: Text.WordWrap
                                            color: Theme.textDim
                                            font.family: Theme.mono
                                            font.pixelSize: Theme.small
                                            text: (modelData.joints || []).join(", ")
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // Tab 3: Localized Geometry Issues
                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: visualInspectorDrawer.currentTab === 3
                        contentWidth: availableWidth
                        ColumnLayout {
                            width: parent.width
                            spacing: Theme.spacingXs
                            Text {
                                visible: root.localizedIssues.length === 0
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                color: Theme.textDim
                                font.pixelSize: Theme.small
                                text: root.skyrimAnalyzed
                                    ? qsTr("No se detectaron problemas de geometría incompatibles.")
                                    : qsTr("Ejecuta «Analizar compatibilidad con Skyrim» para inspeccionar problemas por objeto/malla.")
                            }
                            Repeater {
                                model: root.localizedIssues
                                delegate: Rectangle {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    implicitHeight: issCol.implicitHeight + 10
                                    radius: Theme.radius
                                    color: Theme.panelAlt
                                    border.color: modelData.severity === "blocking" ? Theme.error : Theme.warn
                                    ColumnLayout {
                                        id: issCol
                                        anchors.fill: parent
                                        anchors.margins: 5
                                        spacing: 2
                                        RowLayout {
                                            Layout.fillWidth: true
                                            Text {
                                                Layout.fillWidth: true
                                                elide: Text.ElideRight
                                                color: modelData.severity === "blocking" ? Theme.error : Theme.warn
                                                font.bold: true
                                                font.pixelSize: Theme.small
                                                text: (modelData.object ? modelData.object + " · " : "") + modelData.code
                                            }
                                            Button {
                                                visible: modelData.partIndex >= 0
                                                text: qsTr("Ver en 3D")
                                                font.pixelSize: Theme.small
                                                onClicked: {
                                                    issuesToggle.checked = true;
                                                    root.scene.selectPart(modelData.partIndex);
                                                    root.framePart(modelData.partIndex);
                                                }
                                            }
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            wrapMode: Text.WordWrap
                                            color: Theme.text
                                            font.pixelSize: Theme.small
                                            text: modelData.problem
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // ---- Bottom Viewport Status Bar ----
        Text {
            Layout.fillWidth: true
            Layout.margins: 12
            readonly property var selected: root.scene.selectedPart >= 0 && root.scene.selectedPart < root.scene.parts.length
                ? root.scene.parts[root.scene.selectedPart] : null
            readonly property var f: root.framing()
            readonly property string boundsText: boundsToggle.checked && f && f.size
                ? qsTr(" · Caja %1 × %2 × %3 u").arg(f.size.x.toFixed(2)).arg(f.size.y.toFixed(2)).arg(f.size.z.toFixed(2))
                : ""
            text: root.hasGeometry
                ? (selected ? qsTr("Seleccionado: %1 · ").arg(selected.label) : "")
                    + qsTr("Vista previa de %1 · %2 vértices · %3 triángulos (validados)%4 · no adaptado a Skyrim · FaceGen pendiente")
                    .arg(root.scene.fileName).arg(root.scene.inspection.validated.vertices).arg(root.scene.inspection.validated.triangles)
                    .arg(boundsText)
                : qsTr("FaceGen pendiente · verificación en Skyrim pendiente")
            color: Theme.textMuted
            elide: Text.ElideRight
        }
    }
}
