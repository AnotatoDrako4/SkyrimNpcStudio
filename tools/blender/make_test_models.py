"""Builds the glTF test models of Character Forge with Blender's own exporter.

    blender --background --factory-startup --python tools/blender/make_test_models.py -- <output folder> [model...]

Everything is made from Blender primitives (no third-party asset):
  suzanne.glb          Suzanne, one material, smooth normals.
  rigged_figure.glb    body + head mesh, 4-bone armature with automatic weights, one shape key,
                       a 12-frame bone animation and an embedded generated texture.
  rigged_figure_split/ the same figure as .gltf + .bin + .png (external resources).
  bridge_sources      optional local-only OBJ, FBX, BLEND fixtures for bridge tests.
  skyrim_prop         optional local-only static prop (.blend + external PNG) for the NIF pipeline.
  bridge_resources    optional local-only heavy .blend with moved/ambiguous textures for the preview loader.
  parts_set.glb        four mesh nodes in a hierarchy (Body > Helmet, Body > Shield, Sword at the
                       root); Body has two materials, so its mesh has two primitives.
Naming models (suzanne, rigged_figure, parts_set) builds only those.
Prints one "SNS_MODEL <path> <bytes>" line per file and exits with 1 on failure.
"""
import os
import sys

import shutil

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


def viewer_stress(folder, parts=172, subdivisions=5):
    """Local-only load test for the 3D preview: the shape of a heavy character export (172 separate meshes, one
    material each, ~3.4 M triangles, ~95 MiB GLB) made from primitives. Not part of the default set."""
    reset()
    columns = 13  # odd and centred on x = 0, so the middle of the view hits a part (the harness clicks there)
    for i in range(parts):
        bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=subdivisions, radius=0.4,
                                              location=((i % columns) - columns // 2, (i // columns) - 6, 0))
        obj = bpy.context.active_object
        obj.name = "Part_%03d" % i
        obj.data.name = "PartMesh_%03d" % i
        hue = (i * 0.618) % 1.0
        obj.data.materials.append(material("PartMat_%03d" % i, (hue, 1.0 - hue, 0.5, 1.0)))
    export(os.path.join(folder, "viewer_stress.glb"), "GLB")


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


def skyrim_prop(folder):
    """Local-only static prop for the Skyrim NIF pipeline: a goblet in game units (about 20 units tall),
    an external PNG outside any 'textures' folder and an unapplied scale, plus a copy whose texture is missing.
    """
    reset()
    bpy.ops.mesh.primitive_cylinder_add(vertices=24, radius=4.0, depth=14.0, location=(0, 0, 7.0))
    cup = bpy.context.active_object
    cup.name = "Goblet"
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.cylinder_project()
    bpy.ops.object.mode_set(mode="OBJECT")
    cup.scale = (1.0, 1.0, 1.4)  # left unapplied on purpose
    image = bpy.data.images.new("goblet_diffuse", width=64, height=64)
    image.generated_type = "COLOR_GRID"
    png = os.path.join(folder, "goblet_diffuse.png")
    image.filepath_raw = png
    image.file_format = "PNG"
    image.save()
    cup.data.materials.append(material("GobletBrass", (1.0, 1.0, 1.0, 1.0), image))
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(folder, "skyrim_prop.blend"))
    bpy.data.images["goblet_diffuse"].filepath = os.path.join(folder, "missing", "goblet_missing.png")
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(folder, "skyrim_prop_missing_texture.blend"), copy=True)
    for name in ("goblet_diffuse.png", "skyrim_prop.blend", "skyrim_prop_missing_texture.blend"):
        path = os.path.join(folder, name)
        print("SNS_MODEL %s %d" % (path, os.path.getsize(path)), flush=True)


def bridge_resources(folder):
    """Local-only fixture for the preview resource loading (forge_resources.py): bridge_resources/heavy_resources.blend is
    a 400x400 grid (about 318 000 triangles, over the 300 000 preview budget) whose two textures are missing where the
    .blend expects them (//textures/...). One exists ONCE below the folder (moved/textures/heavy_diffuse.png: relinked);
    the other exists twice in folders that do not match the expected path (a/dup.png, b/dup.png: ambiguous, left missing).
    """
    reset()
    root = os.path.join(folder, "bridge_resources")
    for sub in ("moved/textures", "a", "b"):
        os.makedirs(os.path.join(root, sub), exist_ok=True)

    def png(name, path):
        image = bpy.data.images.new(name, width=16, height=16)
        image.generated_type = "COLOR_GRID"
        image.filepath_raw = path
        image.file_format = "PNG"
        image.save()
        return image

    diffuse = png("heavy_diffuse", os.path.join(root, "moved", "textures", "heavy_diffuse.png"))
    dup = png("dup", os.path.join(root, "a", "dup.png"))
    shutil.copyfile(os.path.join(root, "a", "dup.png"), os.path.join(root, "b", "dup.png"))
    bpy.ops.mesh.primitive_grid_add(x_subdivisions=400, y_subdivisions=400, size=2.0)
    grid = bpy.context.active_object
    grid.name = "HeavyGrid"
    grid.data.materials.append(material("HeavyMaterial", (1.0, 1.0, 1.0, 1.0), diffuse))
    grid.data.materials.append(material("DupMaterial", (1.0, 1.0, 1.0, 1.0), dup))
    path = os.path.join(root, "heavy_resources.blend")
    bpy.ops.wm.save_as_mainfile(filepath=path)
    # Point both images at //textures/<name> next to the .blend, where they do not exist, and save again.
    diffuse.filepath = "//textures/heavy_diffuse.png"
    dup.filepath = "//textures/dup.png"
    bpy.ops.wm.save_as_mainfile(filepath=path)
    print("SNS_MODEL %s %d" % (path, os.path.getsize(path)), flush=True)


SPINE = ("NPC Pelvis [Pelv]", "NPC Spine [Spn0]", "NPC Spine1 [Spn1]", "NPC Spine2 [Spn2]")


def skyrim_skinned(folder):
    """Local-only skinned fixture for the SkinnedMesh profile (needs PyNifly). The armature is PyNifly's import of a
    Skyrim reference skeleton (SNS_REFERENCE_SKELETON, default: the skeleton.nif shipped with PyNifly), so the bones
    keep their exact names and bind transforms. Derived from that skeleton: never commit the outputs.

    skyrim_skinned.blend             open tube (10 rings x 12 sides, UV seam) around the spine, weighted along it:
                                     ring 3 sums to 0.8 (PyNifly normalizes), ring 5 has 5 influences (it keeps 4),
                                     SBP_32_BODY partition, external PNG.
    skyrim_skinned_unweighted.blend  the same with one vertex left without weights.
    skyrim_skinned_custom_bone.blend the same with ring 9 weighted to an added bone "Goblin_Tail".
    """
    import math
    import addon_utils
    import bmesh
    from mathutils import Vector

    reset()
    # default_set=True: PyNifly's importer reads its add-on preferences. Under --factory-startup they live only in
    # this session's memory and are never saved.
    module = addon_utils.enable("io_scene_nifly", default_set=True, handle_error=None)
    if module is None:
        raise RuntimeError("PyNifly (io_scene_nifly) is required for skyrim_skinned")
    skeleton = os.environ.get("SNS_REFERENCE_SKELETON") or os.path.join(
        os.path.dirname(module.__file__), "skeletons", "Skyrim", "skeleton.nif")
    bpy.ops.import_scene.pynifly(filepath=skeleton, create_bones=True, rename_bones=False, rotate_bones_pretty=True,
                                 blender_xf=False, import_animations=False, import_collisions=False)
    arma = next(o for o in bpy.context.scene.objects if o.type == "ARMATURE")
    arma.name = "SkyrimReference"
    for other in [o for o in bpy.context.scene.objects if o is not arma]:
        bpy.data.objects.remove(other, do_unlink=True)
    heads = [arma.matrix_world @ arma.data.bones[n].head_local for n in SPINE]
    low, high = heads[0].z - 6.0, heads[-1].z + 10.0
    rings, sides, radius = 10, 12, 8.0
    mesh = bpy.data.meshes.new("SkinnedTube")
    bm = bmesh.new()
    grid = []
    for r in range(rings):
        z = low + (high - low) * r / (rings - 1)
        grid.append([bm.verts.new((heads[0].x + radius * math.cos(2 * math.pi * s / sides),
                                   heads[0].y + radius * math.sin(2 * math.pi * s / sides), z)) for s in range(sides)])
    uv = bm.loops.layers.uv.new("UVMap")
    for r in range(rings - 1):
        for s in range(sides):
            face = bm.faces.new((grid[r][s], grid[r][(s + 1) % sides], grid[r + 1][(s + 1) % sides], grid[r + 1][s]))
            for loop, (du, dv) in zip(face.loops, ((0, 0), (1, 0), (1, 1), (0, 1))):
                loop[uv].uv = ((s + du) / sides, (r + dv) / (rings - 1))
    bm.normal_update()
    bm.to_mesh(mesh)
    bm.free()
    tube = bpy.data.objects.new("SkinnedTube", mesh)
    bpy.context.scene.collection.objects.link(tube)
    groups = {n: tube.vertex_groups.new(name=n) for n in SPINE + ("NPC L Thigh [LThg]", "SBP_32_BODY")}
    for vertex in mesh.vertices:
        z, ring = vertex.co.z, vertex.index // sides
        weights = {}
        if z <= heads[0].z:
            weights[SPINE[0]] = 1.0
        elif z >= heads[-1].z:
            weights[SPINE[-1]] = 1.0
        else:
            for i in range(len(SPINE) - 1):
                if heads[i].z <= z <= heads[i + 1].z:
                    t = (z - heads[i].z) / (heads[i + 1].z - heads[i].z)
                    weights[SPINE[i]], weights[SPINE[i + 1]] = 1.0 - t, t
                    break
        if ring == 3:  # unnormalized on purpose: the exporter normalizes
            weights = {k: 0.8 * v for k, v in weights.items()}
        if ring == 5:  # five influences on purpose: the exporter keeps the four heaviest
            weights = {n: 0.25 for n in SPINE}
            weights["NPC L Thigh [LThg]"] = 0.004
        for name, weight in weights.items():
            groups[name].add([vertex.index], weight, "REPLACE")
        groups["SBP_32_BODY"].add([vertex.index], 1.0, "REPLACE")
    modifier = tube.modifiers.new("Armature", "ARMATURE")
    modifier.object = arma
    image = bpy.data.images.new("tube_diffuse", width=64, height=64)
    image.generated_type = "COLOR_GRID"
    image.filepath_raw = os.path.join(folder, "tube_diffuse.png")
    image.file_format = "PNG"
    image.save()
    tube.data.materials.append(material("TubeSkin", (1.0, 1.0, 1.0, 1.0), image))
    outputs = ["tube_diffuse.png", "skyrim_skinned.blend"]
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(folder, "skyrim_skinned.blend"))
    # Variant: one vertex without any bone weight.
    for name in SPINE + ("NPC L Thigh [LThg]",):
        groups[name].remove([0])
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(folder, "skyrim_skinned_unweighted.blend"), copy=True)
    outputs.append("skyrim_skinned_unweighted.blend")
    groups[SPINE[0]].add([0], 1.0, "REPLACE")
    # Variant: ring 9 weighted to a bone the reference skeleton does not have.
    bpy.context.view_layer.objects.active = arma
    bpy.ops.object.mode_set(mode="EDIT")
    tail = arma.data.edit_bones.new("Goblin_Tail")
    tail.head = arma.data.edit_bones[SPINE[-1]].head + Vector((0.0, -10.0, 0.0))
    tail.tail = tail.head + Vector((0.0, -5.0, 0.0))
    tail.parent = arma.data.edit_bones[SPINE[-1]]
    bpy.ops.object.mode_set(mode="OBJECT")
    custom = tube.vertex_groups.new(name="Goblin_Tail")
    ring9 = [v.index for v in mesh.vertices if v.index // sides == 9]
    for name in SPINE:
        groups[name].remove(ring9)
    custom.add(ring9, 1.0, "REPLACE")
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(folder, "skyrim_skinned_custom_bone.blend"), copy=True)
    outputs.append("skyrim_skinned_custom_bone.blend")
    for name in outputs:
        path = os.path.join(folder, name)
        print("SNS_MODEL %s %d" % (path, os.path.getsize(path)), flush=True)


DEFAULT_MODELS = ("suzanne", "rigged_figure", "parts_set")
MODELS = {"suzanne": suzanne, "rigged_figure": rigged_figure,
          "parts_set": parts_set, "bridge_sources": bridge_sources, "skyrim_prop": skyrim_prop,
          "skyrim_skinned": skyrim_skinned, "bridge_resources": bridge_resources, "viewer_stress": viewer_stress}


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
