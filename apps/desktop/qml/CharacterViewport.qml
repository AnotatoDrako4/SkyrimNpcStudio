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
    property var sceneOverride: null
    property bool previewMode: false
    readonly property var scene: sceneOverride ?? studio.scene
    color: Theme.bg
    property bool skeletonVisible: false
    property bool uvMode: false
    property bool partsVisible: false
    property bool inspectorVisible: false
    property real ambient: 0.25
    // Viewer-only diagnostics: none, unlit, baseColor, normals, tangents, alpha, wireframe.
    // They change how parts are drawn here, never the resources, the project or the export.
    property string diagnostic: "none"
    property string isolated: ""
    readonly property var diagnosticModes: [
        { value: "none", text: qsTr("Render normal") },
        { value: "unlit", text: qsTr("Sin iluminación") },
        { value: "baseColor", text: qsTr("Solo difusa") },
        { value: "alpha", text: qsTr("Canal alfa") },
        { value: "normals", text: qsTr("Normales") },
        { value: "tangents", text: qsTr("Tangentes") },
        { value: "wireframe", text: qsTr("Malla de alambre") }
    ]
    // Re-read whenever the selection or the scene changes.
    readonly property var inspected: { root.scene.selected; root.scene.parts; return root.scene.selectedPart() }

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
            const heads = root.scene.parts.filter(p => p.group === "head" && p.shownByDefault)
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
    // One line of viewer state for the automated verification log.
    function viewState() {
        let shown = 0
        for (let i = 0; i < partsRepeater.count; ++i) if (partsRepeater.objectAt(i)?.shown) ++shown
        return "uv=" + root.uvMode + " diag=" + root.diagnostic + " isolated='" + root.isolated + "' selected='" + root.scene.selected
               + "' delegates=" + partsRepeater.count + " shown=" + shown + " cam=" + camera.scenePosition + " pivot=" + pivot.position
    }
    function setDiagnostic(mode) {
        root.diagnostic = mode
        const index = root.diagnosticModes.findIndex(m => m.value === mode)
        diagnosticBox.currentIndex = Math.max(0, index)
    }
    function isolateSelected() { root.isolated = root.isolated === "" ? root.scene.selected : "" }
    function hideSelected() { if (root.scene.selected.length > 0) root.scene.setVisible(root.scene.selected, false) }
    // Undo every temporary diagnostic: mode, isolation, UV layout and part visibility.
    function restore() {
        root.setDiagnostic("none")
        root.isolated = ""
        root.uvMode = false
        root.scene.restoreVisibility()
        root.reset()
    }
    function alphaModeOf(mode) {
        return mode === "mask" ? PrincipledMaterial.Mask
             : (mode === "blend" || mode === "blendPrepass") ? PrincipledMaterial.Blend : PrincipledMaterial.Opaque
    }
    Connections { target: root.scene; function onReady() { root.isolated = ""; root.reset() } }
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
            ComboBox {
                visible: root.previewMode
                width: 170
                model: [qsTr("Prenda aislada"), qsTr("Maniquí"), qsTr("Probar en NPC"), qsTr("Outfit completo")]
                onActivated: root.scene.setPreviewMode(["piece", "mannequin", "npc", "outfit"][currentIndex])
            }
            ComboBox {
                id: poseBox
                width: 130
                model: [qsTr("Pose T (Ref)"), qsTr("A-Pose"), qsTr("Relajada"), qsTr("Combate")]
                onActivated: root.scene.pose = ["reference", "apose", "relaxed", "combat"][currentIndex]
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Postura de prueba (T-Pose, A-Pose, Relajada, Combate)")
            }
            ToolButton {
                text: "↻"
                onClicked: root.reset()
                ToolTip.visible: hovered; ToolTip.text: qsTr("Restablecer cámara")
            }
            ToolButton {
                text: "⌖"
                enabled: root.scene.selected.length > 0
                onClicked: { const p = root.scene.selectedBounds(); root.frame(p.center, p.radius, 0) }
                ToolTip.visible: hovered; ToolTip.text: qsTr("Encuadrar pieza seleccionada")
            }
            CheckBox { visible: !root.previewMode; text: qsTr("Original"); checked: root.scene.original; onToggled: root.scene.original = checked }
            CheckBox { text: qsTr("Huesos"); checked: root.skeletonVisible; onToggled: root.skeletonVisible = checked }
            CheckBox {
                text: "UV"; checked: root.uvMode
                onToggled: { root.uvMode = checked; if (checked) root.frame(Qt.vector3d(0,0,0), 90, 0); else root.reset() }
            }
            ComboBox {
                id: diagnosticBox
                width: 150
                textRole: "text"
                model: root.diagnosticModes
                onActivated: root.diagnostic = root.diagnosticModes[currentIndex].value
                ToolTip.visible: hovered; ToolTip.text: qsTr("Vista de diagnóstico (solo el visor; no modifica recursos ni el mod)")
            }
            ToolButton {
                text: root.isolated === "" ? qsTr("Aislar") : qsTr("Mostrar todo")
                enabled: root.isolated !== "" || root.scene.selected.length > 0
                onClicked: root.isolateSelected()
                ToolTip.visible: hovered; ToolTip.text: qsTr("Mostrar solo la geometría seleccionada")
            }
            ToolButton {
                text: qsTr("Ocultar")
                enabled: root.scene.selected.length > 0
                onClicked: root.hideSelected()
                ToolTip.visible: hovered; ToolTip.text: qsTr("Ocultar la geometría seleccionada")
            }
            ToolButton {
                text: qsTr("Restaurar")
                onClicked: root.restore()
                ToolTip.visible: hovered; ToolTip.text: qsTr("Quitar diagnósticos, aislamiento y ocultaciones temporales")
            }
            ToolButton {
                text: "☰"; checkable: true; checked: root.partsVisible
                onToggled: root.partsVisible = checked
                ToolTip.visible: hovered; ToolTip.text: qsTr("Geometrías")
            }
            ToolButton {
                text: "ⓘ"; checkable: true; checked: root.inspectorVisible
                onToggled: root.inspectorVisible = checked
                ToolTip.visible: hovered; ToolTip.text: qsTr("Inspector de la geometría seleccionada")
            }
            ToolButton {
                text: "×"; visible: root.scene.loading; onClicked: root.scene.cancel()
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
                    debugSettings: DebugSettings {
                        materialOverride: root.diagnostic === "baseColor" ? DebugSettings.BaseColor
                                        : root.diagnostic === "normals" ? DebugSettings.Normals
                                        : root.diagnostic === "tangents" ? DebugSettings.Tangents : DebugSettings.None
                        wireframeEnabled: root.diagnostic === "wireframe"
                    }
                }
                Node {
                    id: pivot
                    PerspectiveCamera { id: camera; z: 300; clipNear: 0.1; clipFar: 10000; fieldOfView: 45 }
                }
                DirectionalLight { eulerRotation: Qt.vector3d(-30,-25,0); color: "#fff0dc"; brightness: 1.1; castsShadow: true; ambientColor: Qt.rgba(root.ambient,root.ambient,root.ambient,1) }
                DirectionalLight { eulerRotation: Qt.vector3d(-10,125,0); color: "#d6e7ff"; brightness: 0.65 }
                DirectionalLight { eulerRotation: Qt.vector3d(-60,190,0); brightness: 0.4 }
                Repeater3D {
                    id: partsRepeater
                    model: root.scene.parts
                    // Every geometry, material and texture stays bound to its own Model for the
                    // delegate's whole life; views only switch `visible`. Qt Quick 3D releases a
                    // resource whose last reference goes away and does not re-upload its data when
                    // it is referenced again (blank geometry, white textures).
                    delegate: Node {
                        id: part
                        required property var modelData
                        readonly property string partId: modelData.id
                        readonly property bool shown: root.isolated !== "" ? partId === root.isolated : (root.scene.visibility[partId] ?? false)
                        readonly property vector3d modelScale: Qt.vector3d(modelData.scale, modelData.scale, modelData.scale)
                        property Texture diffuseTexture: Texture { textureData: part.modelData.diffuse; generateMipmaps: false; flipV: false }
                        property Texture normalTexture: Texture { textureData: part.modelData.normal; generateMipmaps: false; flipV: false }
                        Model {
                            readonly property string partId: part.partId
                            visible: part.shown && !root.uvMode && root.diagnostic !== "alpha"
                            geometry: part.modelData.geometry
                            scale: part.modelScale
                            pickable: true
                            materials: PrincipledMaterial {
                                baseColor: part.modelData.tint
                                baseColorMap: part.modelData.diffuse ? part.diffuseTexture : null
                                normalMap: part.modelData.normal ? part.normalTexture : null
                                vertexColorsEnabled: part.modelData.vertexColors
                                roughness: 0.8
                                metalness: 0
                                lighting: root.diagnostic === "unlit" ? PrincipledMaterial.NoLighting : PrincipledMaterial.FragmentLighting
                                cullMode: part.modelData.doubleSided ? Material.NoCulling : Material.BackFaceCulling
                                // Translated from the NiAlphaProperty by the scene builder (see translate_alpha).
                                alphaMode: root.alphaModeOf(part.modelData.alphaMode)
                                alphaCutoff: part.modelData.alphaCutoff
                                depthDrawMode: part.modelData.alphaMode === "blendPrepass" ? Material.OpaquePrePassDepthDraw : Material.OpaqueOnlyDepthDraw
                                opacity: part.modelData.alphaMode === "opaque" ? 1 : part.modelData.materialAlpha
                                emissiveFactor: root.scene.selected === part.partId ? Qt.vector3d(0.08,0.07,0.02) : Qt.vector3d(0,0,0)
                            }
                        }
                        Model {  // diagnostic: alpha channel of the diffuse as grey
                            readonly property string partId: part.partId
                            visible: part.shown && !root.uvMode && root.diagnostic === "alpha"
                            geometry: part.modelData.geometry
                            scale: part.modelScale
                            pickable: true
                            materials: CustomMaterial {
                                shadingMode: CustomMaterial.Unshaded
                                cullMode: part.modelData.doubleSided ? Material.NoCulling : Material.BackFaceCulling
                                vertexShader: "shaders/alpha_channel.vert"
                                fragmentShader: "shaders/alpha_channel.frag"
                                property real hasMap: part.modelData.diffuse ? 1 : 0
                                property TextureInput diffuseMap: TextureInput { texture: part.diffuseTexture }
                            }
                        }
                        Model {  // UV layout of the diffuse, flat on the XY plane
                            readonly property string partId: part.partId
                            visible: part.shown && root.uvMode && (root.scene.selected === part.partId || root.scene.selected === "")
                            geometry: part.modelData.uvGeometry
                            pickable: true
                            materials: PrincipledMaterial {
                                baseColor: part.modelData.tint
                                baseColorMap: part.modelData.diffuse ? part.diffuseTexture : null
                                lighting: PrincipledMaterial.NoLighting
                                cullMode: Material.NoCulling
                                // The layout is inspected whole: texels are not discarded by alpha.
                                alphaMode: PrincipledMaterial.Opaque
                            }
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
                    text: root.scene.status || (root.previewMode ? qsTr("Selecciona una prenda") : qsTr("Selecciona un NPC"))
                    color: Theme.textDim; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter
                    visible: !root.scene.loading && root.scene.parts.length === 0
                }
                Text {
                    anchors.left: parent.left; anchors.top: parent.top; anchors.margins: 8
                    visible: root.diagnostic !== "none" || root.isolated !== ""
                    text: qsTr("Diagnóstico: %1%2").arg(root.diagnosticModes.find(m => m.value === root.diagnostic)?.text ?? "")
                          .arg(root.isolated !== "" ? qsTr(" · aislada") : "")
                    color: Theme.warn; font.pixelSize: Theme.small
                }
            }
            ScrollView {
                visible: root.partsVisible; Layout.preferredWidth: 210; Layout.fillHeight: true
                clip: true
                Column {
                    width: parent.width
                    Repeater {
                        model: root.scene.parts
                        delegate: CheckBox {
                            required property var modelData
                            width: parent.width; text: modelData.label; checked: root.scene.visibility[modelData.id] ?? false
                            contentItem: Label {
                                text: parent.text; leftPadding: parent.indicator.width + parent.spacing; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter
                                color: modelData.shownByDefault ? (modelData.warningCount > 0 ? Theme.warn : Theme.text) : Theme.textDim
                                font.italic: !modelData.shownByDefault
                            }
                            onToggled: root.scene.setVisible(modelData.id, checked)
                            onPressed: root.scene.select(modelData.id)
                            ToolTip.visible: hovered
                            ToolTip.text: modelData.label + "\n" + modelData.path + (modelData.hiddenReason ? "\n" + qsTr("Oculta por defecto: ") + modelData.hiddenReason : "")
                        }
                    }
                }
            }
            ScrollView {
                id: inspectorPanel
                visible: root.inspectorVisible; Layout.preferredWidth: 340; Layout.fillHeight: true
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: inspectorPanel.availableWidth
                    spacing: 2
                    readonly property var info: root.inspected
                    readonly property bool has: info.shape !== undefined
                    SectionHeader { title: qsTr("Inspector"); detail: parent.has ? parent.info.group : qsTr("sin selección") }
                    Text {
                        visible: !parent.has; Layout.fillWidth: true; Layout.margins: 8; wrapMode: Text.Wrap
                        text: qsTr("Haz clic en una geometría del visor o de la lista para ver su origen, materiales y advertencias.")
                        color: Theme.textDim; font.pixelSize: Theme.small
                    }
                    ColumnLayout {
                        visible: parent.has; Layout.fillWidth: true; Layout.margins: 6; spacing: 2
                        readonly property var info: parent.info
                        KeyValue { label: qsTr("Geometría"); value: parent.info.shape ?? "" }
                        KeyValue { label: qsTr("Pieza"); value: parent.info.piece ?? "" }
                        KeyValue { label: qsTr("Archivo NIF"); value: parent.info.nif ?? ""; mono: true }
                        KeyValue { label: qsTr("Proveedor MO2"); value: parent.info.nifProvider ?? "" }
                        KeyValue { label: qsTr("Nodo"); value: parent.info.node ?? "" }
                        KeyValue {
                            label: qsTr("Registro"); mono: true
                            value: parent.info.group === "head"
                                   ? (root.studio.npc.summary?.formKey ?? "") + qsTr(" (FaceGen de ") + (root.studio.npc.summary?.winningPlugin ?? "") + ")"
                                   : qsTr("ARMO ") + (parent.info.armor ?? "") + (parent.info.addon ? qsTr(" · ARMA ") + parent.info.addon : "")
                        }
                        KeyValue { label: qsTr("Slots"); value: parent.info.slots ?? "" }
                        KeyValue { label: qsTr("Shader"); value: parent.info.shader ?? "" }
                        KeyValue { label: qsTr("Flags shader"); value: parent.info.shaderFlags ?? ""; mono: true }
                        KeyValue { label: qsTr("Alpha (NIF)"); value: parent.info.alphaProperty ?? ""; mono: true }
                        KeyValue { label: qsTr("Modo alpha"); value: (parent.info.alphaMode ?? "") + " — " + (parent.info.alphaReason ?? "") }
                        KeyValue { label: qsTr("Alpha material"); value: String(parent.info.materialAlpha ?? "") }
                        KeyValue { label: qsTr("UV"); value: parent.info.uvTransform ?? "" }
                        KeyValue { label: qsTr("Vértices / tris"); value: (parent.info.vertices ?? 0) + " / " + (parent.info.triangles ?? 0) }
                        KeyValue { label: qsTr("Límites"); value: parent.info.bounds ?? ""; mono: true }
                        KeyValue { label: qsTr("Transformación"); value: parent.info.transform ?? ""; mono: true }
                        KeyValue { label: qsTr("Skinning"); value: parent.info.skinning ?? "" }
                        KeyValue { label: qsTr("Huesos del NIF"); value: parent.info.bonesFromMesh ?? "" }
                        KeyValue {
                            label: qsTr("Huesos ausentes"); value: String(parent.info.bonesMissing ?? 0)
                            valueColor: (parent.info.bonesMissing ?? 0) > 0 ? Theme.warn : Theme.text
                        }
                        KeyValue {
                            label: qsTr("Vértices/tangentes")
                            value: (parent.info.vertexColors ? qsTr("colores de vértice") : qsTr("sin colores de vértice"))
                                   + (parent.info.tangentsSwapped ? qsTr(" · tangente/bitangente intercambiadas") : "")
                                   + (parent.info.modelSpaceNormals ? qsTr(" · normales en espacio de modelo") : "")
                        }
                        SectionHeader { title: qsTr("Inspector SMP"); detail: parent.info.physicsDetected ? qsTr("referencia detectada") : qsTr("sin referencia XML") }
                        RowLayout {
                            Layout.fillWidth: true
                            StatusBadge {
                                text: !parent.parent.info.physicsDetected ? qsTr("SIN FÍSICAS")
                                      : (parent.parent.info.physicsXmlFound && parent.parent.info.physicsXmlWellFormed && (parent.parent.info.bonesMissing ?? 0) === 0)
                                        ? qsTr("SMP VÁLIDO")
                                        : (parent.parent.info.bonesMissing ?? 0) > 0 ? qsTr("SMP INCOMPLETO") : qsTr("SMP ROTO")
                                tone: !parent.parent.info.physicsDetected ? Theme.textDim
                                      : (parent.parent.info.physicsXmlFound && parent.parent.info.physicsXmlWellFormed && (parent.parent.info.bonesMissing ?? 0) === 0)
                                        ? Theme.ok
                                        : (parent.parent.info.bonesMissing ?? 0) > 0 ? Theme.warn : Theme.error
                            }
                        }
                        KeyValue { label: qsTr("Malla cargada"); value: parent.info.vertices > 0 ? qsTr("sí, pose de referencia") : qsTr("no") }
                        KeyValue { label: qsTr("XML físico"); value: parent.info.physicsXml || qsTr("no detectado"); mono: true }
                        KeyValue { label: qsTr("Proveedor XML"); value: parent.info.physicsXmlProvider || qsTr("sin resolver") }
                        KeyValue { label: qsTr("Dependencia XML"); value: !parent.info.physicsDetected ? qsTr("no declarada") : parent.info.physicsXmlWellFormed ? qsTr("XML bien formado; esquema FSMP sin validar") : parent.info.physicsXmlFound ? qsTr("XML inválido") : qsTr("faltante") }
                        KeyValue { visible: !!parent.info.physicsXmlError; label: qsTr("Error XML"); value: parent.info.physicsXmlError ?? ""; valueColor: Theme.warn }
                        KeyValue { label: qsTr("Huesos auxiliares"); value: parent.info.bonesFromMesh || qsTr("ninguno detectado") }
                        KeyValue { label: qsTr("Huesos declarados XML"); value: (parent.info.physicsBones ?? []).join(", ") || qsTr("ninguno detectado") }
                        KeyValue { label: qsTr("Shapes de colisión XML"); value: (parent.info.collisionShapes ?? []).join(", ") || qsTr("ninguno detectado") }
                        KeyValue { label: qsTr("Referencia estática"); value: parent.info.staticCompatible ? qsTr("malla y huesos presentes") : qsTr("incompleta") }
                        KeyValue { label: qsTr("Simulación"); value: qsTr("Estática en visor (FSMP se ejecuta en Skyrim)"); valueColor: Theme.accent }
                        KeyValue {
                            label: qsTr("FSMP en MO2")
                            value: root.studio.outfit.compatibilityReport.smpRuntime?.present
                                   ? qsTr("DLL encontrada: ") + root.studio.outfit.compatibilityReport.smpRuntime.provider
                                   : qsTr("DLL no encontrada en archivos sueltos del perfil")
                        }
                        KeyValue { label: qsTr("Dentro de Skyrim"); value: qsTr("pendiente de verificar") }
                        KeyValue {
                            visible: (parent.info.hiddenReason ?? "") !== ""
                            label: qsTr("Oculta por defecto"); value: parent.info.hiddenReason ?? ""; valueColor: Theme.warn
                        }
                        SectionHeader { title: qsTr("Texturas"); detail: String((parent.info.textures ?? []).length) }
                        Repeater {
                            model: parent.info.textures ?? []
                            delegate: ColumnLayout {
                                required property var modelData
                                Layout.fillWidth: true; spacing: 0
                                KeyValue { label: modelData.role; value: modelData.path; mono: true }
                                KeyValue { label: qsTr("  origen"); value: modelData.location ?? "" }
                                KeyValue { visible: !!modelData.size; label: qsTr("  tamaño"); value: (modelData.size ?? "") + " " + (modelData.format ?? "") }
                                KeyValue { visible: !!modelData.alpha; label: qsTr("  alfa"); value: modelData.alpha ?? "" }
                                KeyValue { visible: !!modelData.derivedFrom; label: qsTr("  compuesta de"); value: modelData.derivedFrom ?? ""; mono: true }
                                KeyValue { visible: !!modelData.error; label: qsTr("  error"); value: modelData.error ?? ""; valueColor: Theme.error }
                            }
                        }
                        SectionHeader { title: qsTr("Advertencias"); detail: String((parent.info.warnings ?? []).length) }
                        Repeater {
                            model: parent.info.warnings ?? []
                            delegate: Text {
                                required property var modelData
                                Layout.fillWidth: true; wrapMode: Text.Wrap
                                text: "• " + modelData; color: Theme.warn; font.pixelSize: Theme.small
                            }
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
