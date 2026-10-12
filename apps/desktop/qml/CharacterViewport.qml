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
    color: Theme.surface

    property bool skeletonVisible: false
    property bool uvMode: false
    property bool partsVisible: false
    property bool inspectorVisible: false
    property real ambient: 0.25

    // Viewer-only diagnostics: none, unlit, baseColor, normals, tangents, alpha, wireframe.
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

    Item {
        anchors.fill: parent

        // ============================================================= VISOR 3D PRINCIPAL
        View3D {
            id: viewport
            objectName: "characterView3D"
            anchors.fill: parent
            camera: camera
            environment: SceneEnvironment {
                backgroundMode: SceneEnvironment.Color
                clearColor: "#16171a"
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
        }

        // Indicador de carga
        BusyIndicator {
            anchors.centerIn: parent
            running: root.scene.loading
            visible: running
        }

        // ============================================================= ESTADO VACÍO ELEGANTE
        Rectangle {
            anchors.centerIn: parent
            visible: !root.scene.loading && root.scene.parts.length === 0
            width: Math.min(360, parent.width - 40)
            height: 190
            radius: Theme.radiusLarge
            color: Qt.rgba(0.09, 0.10, 0.12, 0.88)
            border.color: Theme.borderSubtle
            border.width: 1

            ColumnLayout {
                anchors.centerIn: parent
                spacing: 10
                width: parent.width - 32

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: root.previewMode ? "🥋" : "🎭"
                    font.pixelSize: 36
                }

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: root.previewMode ? qsTr("Visor de Prendas y Outfits") : qsTr("Estudio Visual de Personajes")
                    color: Theme.text
                    font.pixelSize: Theme.large
                    font.bold: true
                }

                Text {
                    Layout.fillWidth: true
                    text: root.scene.status || (root.previewMode ? qsTr("Selecciona una prenda en el catálogo para ver su geometría 3D.") : qsTr("Selecciona un NPC del panel izquierdo para comenzar a personalizar."))
                    color: Theme.textDim
                    font.pixelSize: Theme.small
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                }
            }
        }

        // ============================================================= TOOLBAR FLOTANTE MINIMALISTA
        Rectangle {
            id: floatingToolbar
            anchors.top: parent.top
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.topMargin: 8
            implicitHeight: 38
            implicitWidth: toolRow.implicitWidth + 16
            radius: Theme.radiusLarge
            color: Qt.rgba(0.09, 0.10, 0.12, 0.90)
            border.color: Theme.borderSubtle
            border.width: 1

            RowLayout {
                id: toolRow
                anchors.centerIn: parent
                spacing: 6

                // Selector de Cámara
                ComboBox {
                    id: cameraPreset
                    implicitWidth: 125
                    implicitHeight: 28
                    model: [qsTr("Cuerpo entero"), qsTr("Frontal"), qsTr("Lateral"), qsTr("Posterior"), qsTr("Rostro")]
                    onActivated: root.applyCameraPreset(currentIndex)
                }

                // Modo Preview de Ropa (cuando previewMode = true)
                ComboBox {
                    visible: root.previewMode
                    implicitWidth: 140
                    implicitHeight: 28
                    model: [qsTr("Prenda aislada"), qsTr("Maniquí"), qsTr("Probar en NPC"), qsTr("Outfit completo")]
                    onActivated: root.scene.setPreviewMode(["piece", "mannequin", "npc", "outfit"][currentIndex])
                }

                // Selector de Postura (Pose)
                ComboBox {
                    id: poseBox
                    implicitWidth: 110
                    implicitHeight: 28
                    model: [qsTr("Pose T"), qsTr("A-Pose"), qsTr("Relajada"), qsTr("Combate")]
                    onActivated: root.scene.pose = ["reference", "apose", "relaxed", "combat"][currentIndex]
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Postura de referencia (T-Pose, A-Pose, Relajada, Combate)")
                }

                // Reset Cámara y Enfoque
                ToolButton {
                    text: "↻"
                    implicitHeight: 28
                    onClicked: root.reset()
                    ToolTip.visible: hovered; ToolTip.text: qsTr("Restablecer cámara orbital")
                }

                ToolButton {
                    text: "⌖"
                    implicitHeight: 28
                    enabled: root.scene.selected.length > 0
                    onClicked: { const p = root.scene.selectedBounds(); root.frame(p.center, p.radius, 0) }
                    ToolTip.visible: hovered; ToolTip.text: qsTr("Encuadrar pieza seleccionada")
                }

                // Comparador Original / Modificado
                ToolButton {
                    visible: !root.previewMode
                    implicitHeight: 28
                    checkable: true
                    checked: root.scene.original
                    text: checked ? "👁 " + qsTr("Original") : "✨ " + qsTr("Mod")
                    onToggled: root.scene.original = checked
                    ToolTip.visible: hovered; ToolTip.text: qsTr("Alternar vista original de Skyrim y cambios actuales")
                }

                Rectangle { width: 1; height: 18; color: Theme.borderSubtle }

                // Botón Geometrías / Piezas
                ToolButton {
                    text: "☰"
                    implicitHeight: 28
                    checkable: true
                    checked: root.partsVisible
                    onToggled: root.partsVisible = checked
                    ToolTip.visible: hovered; ToolTip.text: qsTr("Mostrar/ocultar lista de geometrías")
                }

                // Botón Inspector Técnico
                ToolButton {
                    text: "⚙"
                    implicitHeight: 28
                    checkable: true
                    checked: root.inspectorVisible || Theme.advancedMode
                    onToggled: root.inspectorVisible = checked
                    ToolTip.visible: hovered; ToolTip.text: qsTr("Inspector técnico (shaders, UV, SMP, materiales)")
                }

                // Botón cancelar carga
                ToolButton {
                    text: "✕"
                    visible: root.scene.loading
                    implicitHeight: 28
                    onClicked: root.scene.cancel()
                    ToolTip.visible: hovered; ToolTip.text: qsTr("Cancelar carga")
                }
            }
        }

        // ============================================================= PANEL LATERAL: LISTA DE PIEZAS
        Rectangle {
            visible: root.partsVisible
            anchors.right: inspectorPanel.visible ? inspectorPanel.left : parent.right
            anchors.top: parent.top
            anchors.bottom: bottomStatusRow.top
            width: 220
            color: Qt.rgba(0.09, 0.10, 0.12, 0.94)
            border.color: Theme.borderSubtle
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 6
                spacing: 4

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: qsTr("Geometrías")
                        color: Theme.accent
                        font.bold: true
                        font.pixelSize: Theme.small
                        Layout.fillWidth: true
                    }
                    ToolButton {
                        text: "✕"
                        implicitHeight: 20
                        implicitWidth: 20
                        onClicked: root.partsVisible = false
                    }
                }

                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true

                    Column {
                        width: parent.width
                        spacing: 2
                        Repeater {
                            model: root.scene.parts
                            delegate: CheckBox {
                                required property var modelData
                                width: parent.width
                                text: modelData.label
                                checked: root.scene.visibility[modelData.id] ?? false
                                contentItem: Label {
                                    text: parent.text
                                    leftPadding: parent.indicator.width + 4
                                    elide: Text.ElideRight
                                    verticalAlignment: Text.AlignVCenter
                                    color: modelData.shownByDefault ? (modelData.warningCount > 0 ? Theme.warn : Theme.text) : Theme.textDim
                                    font.pixelSize: Theme.small
                                }
                                onToggled: root.scene.setVisible(modelData.id, checked)
                                onPressed: root.scene.select(modelData.id)
                                ToolTip.visible: hovered
                                ToolTip.text: modelData.label + "\n" + modelData.path
                            }
                        }
                    }
                }
            }
        }

        // ============================================================= PANEL LATERAL: INSPECTOR TÉCNICO
        Rectangle {
            id: inspectorPanel
            visible: root.inspectorVisible || (Theme.advancedMode && root.scene.parts.length > 0)
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: bottomStatusRow.top
            width: 320
            color: Qt.rgba(0.09, 0.10, 0.12, 0.95)
            border.color: Theme.borderSubtle
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 4

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: qsTr("Inspector Técnico")
                        color: Theme.accent
                        font.bold: true
                        font.pixelSize: Theme.normal
                        Layout.fillWidth: true
                    }
                    ToolButton {
                        text: "✕"
                        implicitHeight: 20
                        implicitWidth: 20
                        onClicked: { root.inspectorVisible = false }
                    }
                }

                // Modos de Diagnóstico y UV
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    ComboBox {
                        id: diagnosticBox
                        Layout.fillWidth: true
                        textRole: "text"
                        model: root.diagnosticModes
                        onActivated: root.diagnostic = root.diagnosticModes[currentIndex].value
                    }

                    CheckBox {
                        text: "UV"
                        checked: root.uvMode
                        onToggled: { root.uvMode = checked; if (checked) root.frame(Qt.vector3d(0,0,0), 90, 0); else root.reset() }
                    }

                    CheckBox {
                        text: "Huesos"
                        checked: root.skeletonVisible
                        onToggled: root.skeletonVisible = checked
                    }
                }

                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    contentWidth: availableWidth

                    ColumnLayout {
                        width: inspectorPanel.width - 20
                        spacing: 3
                        readonly property var info: root.inspected
                        readonly property bool has: info.shape !== undefined

                        Text {
                            visible: !parent.has
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            text: qsTr("Haz clic en una pieza 3D para inspeccionar sus shaders, rutas NIF, materiales y físicas SMP.")
                            color: Theme.textDim
                            font.pixelSize: Theme.small
                        }

                        ColumnLayout {
                            visible: parent.has
                            Layout.fillWidth: true
                            spacing: 3
                            readonly property var info: parent.info

                            KeyValue { label: qsTr("Geometría"); value: parent.info.shape ?? "" }
                            KeyValue { label: qsTr("Pieza"); value: parent.info.piece ?? "" }
                            KeyValue { label: qsTr("Archivo NIF"); value: parent.info.nif ?? ""; mono: true }
                            KeyValue { label: qsTr("Proveedor MO2"); value: parent.info.nifProvider ?? "" }
                            KeyValue { label: qsTr("Shader"); value: parent.info.shader ?? "" }
                            KeyValue { label: qsTr("Modo alpha"); value: (parent.info.alphaMode ?? "") + " — " + (parent.info.alphaReason ?? "") }
                            KeyValue { label: qsTr("Vértices / Tris"); value: (parent.info.vertices ?? 0) + " / " + (parent.info.triangles ?? 0) }

                            // Estado SMP
                            SectionHeader { title: qsTr("Físicas HDT-SMP / FSMP"); detail: parent.info.physicsDetected ? qsTr("activo") : qsTr("inactivo") }
                            StatusBadge {
                                text: !parent.info.physicsDetected ? qsTr("SIN FÍSICAS")
                                      : (parent.info.physicsXmlFound && parent.info.physicsXmlWellFormed && (parent.info.bonesMissing ?? 0) === 0)
                                        ? qsTr("SMP VÁLIDO") : qsTr("SMP REVISAR")
                                tone: !parent.info.physicsDetected ? Theme.textDim
                                      : (parent.info.physicsXmlFound && parent.info.physicsXmlWellFormed && (parent.info.bonesMissing ?? 0) === 0)
                                        ? Theme.ok : Theme.warn
                            }
                            KeyValue { label: qsTr("XML Físico"); value: parent.info.physicsXml || qsTr("no detectado"); mono: true }
                            KeyValue { label: qsTr("Huesos declarados"); value: (parent.info.physicsBones ?? []).join(", ") || qsTr("ninguno") }
                            KeyValue { label: qsTr("Shapes de colisión"); value: (parent.info.collisionShapes ?? []).join(", ") || qsTr("ninguno") }

                            // Texturas
                            SectionHeader { title: qsTr("Texturas"); detail: String((parent.info.textures ?? []).length) }
                            Repeater {
                                model: parent.info.textures ?? []
                                delegate: ColumnLayout {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    spacing: 0
                                    KeyValue { label: modelData.role; value: modelData.path; mono: true }
                                    KeyValue { visible: !!modelData.size; label: qsTr("  formato"); value: (modelData.size ?? "") + " " + (modelData.format ?? "") }
                                }
                            }
                        }
                    }
                }
            }
        }

        // ============================================================= BARRA INFERIOR DE AMBIENTE
        Rectangle {
            id: bottomStatusRow
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            implicitHeight: 28
            color: Qt.rgba(0.09, 0.10, 0.12, 0.85)
            border.color: Theme.borderSubtle
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 8

                Text {
                    text: qsTr("Luz:")
                    color: Theme.textDim
                    font.pixelSize: Theme.small
                }

                Slider {
                    from: 0
                    to: 0.6
                    value: root.ambient
                    onMoved: root.ambient = value
                    Layout.preferredWidth: 80
                    implicitHeight: 20
                }

                Text {
                    text: root.scene.status
                    color: Theme.textDim
                    font.pixelSize: Theme.micro
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }

                Text {
                    visible: root.diagnostic !== "none" || root.isolated !== ""
                    text: qsTr("Modo: %1").arg(root.diagnosticModes.find(m => m.value === root.diagnostic)?.text ?? "")
                    color: Theme.warn
                    font.pixelSize: Theme.micro
                }
            }
        }
    }
}
