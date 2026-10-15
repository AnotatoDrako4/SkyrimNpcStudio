"""Builds the glTF test models of Character Forge with Blender's own exporter.

    blender --background --factory-startup --python tools/blender/make_test_models.py -- <output folder> [model...]

Everything is made from Blender primitives (no third-party asset):
  suzanne.glb          Suzanne, one material, smooth normals.
  rigged_figure.glb    body + head mesh, 4-bone armature with automatic weights, one shape key,
                       a 12-frame bone animation and an embedded generated texture.
  rigged_figure_split/ the same figure as .gltf + .bin + .png (external resources).
  bridge_sources      optional local-only OBJ, FBX, BLEND fixtures for bridge tests.
  parts_set.glb        four mesh nodes in a hierarchy (Body > Helmet, Body > Shield, Sword at the
                       root); Body has two materials, so its mesh has two primitives.
Naming models (suzanne, rigged_figure, parts_set) builds only those.
Prints one "SNS_MODEL <path> <bytes>" line per file and exits with 1 on failure.
"""
import os
import sys

import bpy


def arguments():
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if not args:
        raise SystemExit("usage: ... --python make_test_models.py -- <output folder> [model...]")
    path = os.path.abspath(args[0])
    os.makedirs(path, exist_ok=True)
    return path, args[1:]


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def material(name, color, image=None):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get("Principled BSDF")
    bsdf.inputs["Base Color"].default_value = color
    if image is not None:
        tex = mat.node_tree.nodes.new("ShaderNodeTexImage")
        tex.image = image
        mat.node_tree.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    return mat


def export(path, fmt):
    bpy.ops.export_scene.gltf(filepath=path, export_format=fmt, export_skins=True, export_morph=True,
                              export_animations=True, export_yup=True)
    print("SNS_MODEL %s %d" % (path, os.path.getsize(path)), flush=True)


def suzanne(folder):
    reset()
    bpy.ops.mesh.primitive_monkey_add(size=2.0)
    obj = bpy.context.active_object
    obj.name = "Suzanne"
    bpy.ops.object.shade_smooth()
    obj.data.materials.append(material("SuzanneSkin", (0.80, 0.55, 0.40, 1.0)))
    export(os.path.join(folder, "suzanne.glb"), "GLB")


def rigged_figure(folder):
    reset()
    # Body: a cylinder 1.8 m tall standing on the ground, head: a UV sphere on top.
    bpy.ops.mesh.primitive_cylinder_add(vertices=16, radius=0.25, depth=1.4, location=(0, 0, 0.7))
    body = bpy.context.active_object
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.subdivide(number_cuts=6)
    bpy.ops.object.mode_set(mode="OBJECT")
    bpy.ops.mesh.primitive_uv_sphere_add(segments=16, ring_count=8, radius=0.2, location=(0, 0, 1.6))
    head = bpy.context.active_object
    bpy.ops.object.select_all(action="DESELECT")
    body.select_set(True)
    head.select_set(True)
    bpy.context.view_layer.objects.active = body
    bpy.ops.object.join()
    figure = bpy.context.active_object
    figure.name = "Figure"
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project()
    bpy.ops.object.mode_set(mode="OBJECT")

    image = bpy.data.images.new("FigureGrid", width=64, height=64)
    image.generated_type = "UV_GRID"
    image.pack()
    figure.data.materials.append(material("FigureMaterial", (1.0, 1.0, 1.0, 1.0), image))

    # Shape key that pushes the head forward ("Lean").
    figure.shape_key_add(name="Basis")
    lean = figure.shape_key_add(name="Lean")
    for v in lean.data:
        if v.co.z > 1.35:
            v.co.y -= 0.15

    bpy.ops.object.armature_add(location=(0, 0, 0))
    armature = bpy.context.active_object
    armature.name = "FigureRig"
    bpy.ops.object.mode_set(mode="EDIT")
    bones = armature.data.edit_bones
    root = bones[0]
    root.name = "Root"
    root.head, root.tail = (0, 0, 0), (0, 0, 0.4)
    for name, parent, head_z, tail_z in (("Spine", "Root", 0.4, 1.0), ("Chest", "Spine", 1.0, 1.4), ("Head", "Chest", 1.4, 1.8)):
        bone = bones.new(name)
        bone.head, bone.tail = (0, 0, head_z), (0, 0, tail_z)
        bone.parent = bones[parent]
        bone.use_connect = True
    bpy.ops.object.mode_set(mode="OBJECT")

    bpy.ops.object.select_all(action="DESELECT")
    figure.select_set(True)
    armature.select_set(True)
    bpy.context.view_layer.objects.active = armature
    bpy.ops.object.parent_set(type="ARMATURE_AUTO")

    # A short animation on the head bone.
    bpy.context.view_layer.objects.active = armature
    bpy.ops.object.mode_set(mode="POSE")
    head_bone = armature.pose.bones["Head"]
    head_bone.rotation_mode = "XYZ"
    for frame, angle in ((1, 0.0), (6, 0.4), (12, 0.0)):
        head_bone.rotation_euler = (0.0, 0.0, angle)
        head_bone.keyframe_insert(data_path="rotation_euler", frame=frame)
    bpy.ops.object.mode_set(mode="OBJECT")

    export(os.path.join(folder, "rigged_figure.glb"), "GLB")
    split = os.path.join(folder, "rigged_figure_split")
    os.makedirs(split, exist_ok=True)
    export(os.path.join(split, "rigged_figure.gltf"), "GLTF_SEPARATE")
    for name in sorted(os.listdir(split)):
        if not name.endswith(".gltf"):
            print("SNS_MODEL %s %d" % (os.path.join(split, name), os.path.getsize(os.path.join(split, name))), flush=True)


def parented(child, parent):
    # Keeps the child's world placement under its new parent.
    child.parent = parent
    child.matrix_parent_inverse = parent.matrix_world.inverted()


def parts_set(folder):
    reset()
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0, 0, 0.9))
    body = bpy.context.active_object
    body.name = "Body"
    body.scale = (0.4, 0.25, 0.9)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    body.data.materials.append(material("BodyCloth", (0.25, 0.35, 0.60, 1.0)))
    body.data.materials.append(material("BodyLeather", (0.45, 0.30, 0.18, 1.0)))
    for polygon in body.data.polygons:
        polygon.material_index = 1 if polygon.center.z > 0 else 0

    bpy.ops.mesh.primitive_uv_sphere_add(segments=16, ring_count=8, radius=0.18, location=(0, 0, 1.55))
    helmet = bpy.context.active_object
    helmet.name = "Helmet"
    helmet.data.materials.append(material("HelmetSteel", (0.70, 0.70, 0.72, 1.0)))
    parented(helmet, body)

    bpy.ops.mesh.primitive_cylinder_add(vertices=24, radius=0.3, depth=0.05, location=(-0.32, 0, 1.0), rotation=(0, 1.5708, 0))
    shield = bpy.context.active_object
    shield.name = "Shield"
    shield.data.materials.append(material("ShieldWood", (0.55, 0.40, 0.20, 1.0)))
    parented(shield, body)

    bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0.45, 0, 0.9))
    sword = bpy.context.active_object
    sword.name = "Sword"
    sword.scale = (0.04, 0.02, 0.9)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    sword.data.materials.append(material("SwordSteel", (0.85, 0.85, 0.90, 1.0)))

    export(os.path.join(folder, "parts_set.glb"), "GLB")


def bridge_sources(folder):
    """Create local-only OBJ, FBX and BLEND from the same mesh to test the bridge.

    These outputs are generated in the test run directory, never committed as
    third-party assets or copied into Skyrim/MO2.
    """
    reset()
    bpy.ops.mesh.primitive_cube_add(size=1.0)
    cube = bpy.context.active_object
    cube.name = "BridgeCube"
    cube.data.materials.append(material("BridgeMaterial", (0.2, 0.6, 0.8, 1.0)))
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(folder, "bridge_sample.blend"))
    bpy.ops.export_scene.fbx(filepath=os.path.join(folder, "bridge_sample.fbx"))
    bpy.ops.wm.obj_export(filepath=os.path.join(folder, "bridge_sample.obj"))
    for extension in ("obj", "fbx", "blend"):
        path = os.path.join(folder, "bridge_sample." + extension)
        if not os.path.isfile(path) or os.path.getsize(path) == 0:
            raise RuntimeError("Missing generated Blender fixture: " + path)
        print("SNS_MODEL %s %d" % (path, os.path.getsize(path)), flush=True)


DEFAULT_MODELS = ("suzanne", "rigged_figure", "parts_set")
MODELS = {"suzanne": suzanne, "rigged_figure": rigged_figure,
          "parts_set": parts_set, "bridge_sources": bridge_sources}


def main():
    folder, names = arguments()
    print("SNS_BLENDER " + bpy.app.version_string, flush=True)
    unknown = [n for n in names if n not in MODELS]
    if unknown:
        raise SystemExit("unknown model(s): " + ", ".join(unknown))
    for name in names or DEFAULT_MODELS:
        MODELS[name](folder)


try:
    main()
except Exception as error:  # report and fail the process, never leave a half-written set silently
    import traceback
    traceback.print_exc()
    print("SNS_FAILED " + str(error), flush=True)
    sys.exit(1)
