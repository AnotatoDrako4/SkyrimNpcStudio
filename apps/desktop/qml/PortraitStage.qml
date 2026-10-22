import QtQuick
import QtQuick3D
import SkyrimNpcStudio

// Stage of the NPC portraits (PortraitRenderer): only the head parts of the portrait scene, a fixed face
// camera and neutral lights, no interface. The renderer creates it inside its off-screen window
// (QQuickRenderControl) and reads each frame back from a texture. The materials follow CharacterViewport's
// normal render mode.
Item {
    id: stage
    required property var scene
    // Read by the renderer to tell a drawn portrait from an empty one.
    property color background: "#16171a"
    width: 256
    height: 256
    visible: false

    // Frames the face: the largest head part that is not hair (long hair is larger than the head itself),
    // seen from the front at a fixed distance relative to its size. False when the scene has no head part.
    // {center: vector3d, distance: real}: the framing of ANOTHER head of the same NPC (the A/B view of the appearance
    // sources draws several FaceGen files of one NPC and every side must share one camera). null: frame this head.
    property var fixedFrame: null

    function frameHead() {
        const heads = stage.scene.parts.filter(p => p.group === "head" && p.shownByDefault)
        if (heads.length === 0) return false
        if (stage.fixedFrame) {
            pivot.position = stage.fixedFrame.center
            camera.z = stage.fixedFrame.distance
            return true
        }
        const faces = heads.filter(p => !p.hair)
        const pool = faces.length > 0 ? faces : heads
        let head = pool[0]
        for (const p of pool) if (p.radius > head.radius) head = p
        pivot.position = head.center
        camera.z = head.radius * 2.9
        return true
    }

    function tintOf(row) { return row.hair ? (row.hairVertexTint ? "white" : stage.scene.hairTint) : row.tint }
    function alphaModeOf(mode) {
        return mode === "mask" ? PrincipledMaterial.Mask
             : (mode === "blend" || mode === "blendPrepass") ? PrincipledMaterial.Blend : PrincipledMaterial.Opaque
    }

    View3D {
        anchors.fill: parent
        camera: camera
        environment: SceneEnvironment {
            backgroundMode: SceneEnvironment.Color
            clearColor: stage.background
            antialiasingMode: SceneEnvironment.MSAA
            antialiasingQuality: SceneEnvironment.High
        }

        Node {
            id: pivot
            PerspectiveCamera { id: camera; z: 100; clipNear: 0.1; clipFar: 10000; fieldOfView: 30 }
        }

        DirectionalLight { eulerRotation: Qt.vector3d(-20, -20, 0); color: "#fff4e8"; brightness: 1.0; ambientColor: Qt.rgba(0.3, 0.3, 0.3, 1) }
        DirectionalLight { eulerRotation: Qt.vector3d(-10, 140, 0); color: "#dde8ff"; brightness: 0.5 }

        Repeater3D {
            model: stage.scene.parts
            delegate: Node {
                id: part
                required property var modelData
                visible: modelData.group === "head" && (stage.scene.visibility[modelData.id] ?? false)
                property Texture diffuseTexture: Texture { textureData: part.modelData.diffuse; generateMipmaps: false; flipV: false }
                property Texture normalTexture: Texture { textureData: part.modelData.normal; generateMipmaps: false; flipV: false }
                Model {
                    geometry: part.modelData.geometry
                    scale: Qt.vector3d(part.modelData.scale, part.modelData.scale, part.modelData.scale)
                    materials: PrincipledMaterial {
                        baseColor: stage.tintOf(part.modelData)
                        baseColorMap: part.modelData.diffuse ? part.diffuseTexture : null
                        normalMap: part.modelData.normal ? part.normalTexture : null
                        vertexColorsEnabled: part.modelData.vertexColors
                        roughness: 0.8
                        metalness: 0
                        cullMode: part.modelData.doubleSided ? Material.NoCulling : Material.BackFaceCulling
                        alphaMode: stage.alphaModeOf(part.modelData.alphaMode)
                        alphaCutoff: part.modelData.alphaCutoff
                        depthDrawMode: part.modelData.alphaMode === "blendPrepass" ? Material.OpaquePrePassDepthDraw : Material.OpaqueOnlyDepthDraw
                        opacity: part.modelData.alphaMode === "opaque" ? 1 : part.modelData.materialAlpha
                    }
                }
            }
        }
    }
}
