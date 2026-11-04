"""Character Forge -> Skyrim jobs run inside Blender (contract sns.forge.skyrim/1).

Run only through the application (ForgeBlenderBridge::runSkyrimJob):
  blender --background --factory-startup --disable-autoexec --python forge_skyrim.py -- JOB.json

JOB.json (written by the application inside its own staging folder):
  {"schema": "sns.forge.skyrim.job/1", "operation": "inspect" | "exportNif",
   "source": "<model: .blend .fbx .obj .glb .gltf>", "stage": "<staging folder>",
   "report": "<stage>/report.json", "texturePrefix": "CharacterForge\\<slug>",
   "nifName": "<slug>.nif", "targetGame": "SKYRIMSE", "applySafeTransforms": true}

The source is never saved: the scene is re-saved as <stage>/work.blend (relative paths remapped) and every
preparation step changes only that copy. Facts are collected here; the application classifies them
(engines/creation skyrim_conversion) and re-reads the NIF with its own nifly build.
stdout carries "SNS_FORGE_PROGRESS {json}" lines; the report is always written, also on failure.
"""

import hashlib
import json
import os
import re
import sys
import traceback

import bpy

SCHEMA = "sns.forge.skyrim/1"
JOB_SCHEMA = "sns.forge.skyrim.job/1"
SOURCE_EXTENSIONS = {".blend", ".fbx", ".obj", ".glb", ".gltf"}
PYNIFLY_MODULE = "io_scene_nifly"
PYNIFLY_TESTED_VERSIONS = {(29, 1, 0)}
MAX_IMAGE_BYTES = 256 * 1024 * 1024
# Bones whose presence marks a Skyrim humanoid skeleton (skeleton.nif / XPMSSE share them).
SKYRIM_HUMANOID_BONES = {"NPC Root [Root]", "NPC COM [COM ]", "NPC Pelvis [Pelv]", "NPC Spine [Spn0]", "NPC Head [Head]"}
# "static": Type A resource (no skin). "skinned": meshes bound to an armature, exported with their skin.
PROFILES = {"static", "skinned"}
MAX_BONES = 1024           # the PyNifly Skyrim reference skeleton has 513 bones
WEIGHT_SAMPLES = 256         # vertices per mesh whose exporter-side weights are reported for the readback check
PYNIFLY_MIN_WEIGHT = 0.00005  # pynifly.get_weights_by_bone ignores lighter weights
PARTITION_GROUP = re.compile(r"^SBP_\d+_")  # Skyrim dismember partition vertex groups (PyNifly is_partition)


def progress(step, fraction):
    print("SNS_FORGE_PROGRESS " + json.dumps({"step": step, "fraction": round(fraction, 3)}), flush=True)


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def inside(folder, path):
    folder, path = os.path.normcase(os.path.abspath(folder)), os.path.normcase(os.path.abspath(path))
    try:
        return os.path.commonpath([folder, path]) == folder
    except ValueError:  # different drives (model on G:, stage in %TEMP% on C:): never inside
        return False


def read_job():
    values = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if len(values) != 1:
        raise ValueError("Expected exactly one job file")
    with open(values[0], "r", encoding="utf-8") as stream:
        job = json.load(stream)
    if job.get("schema") != JOB_SCHEMA or job.get("operation") not in {"inspect", "exportNif"}:
        raise ValueError("Unsupported job schema or operation")
    stage = os.path.abspath(job["stage"])
    source = os.path.abspath(job["source"])
    if not os.path.isdir(stage) or not os.path.isfile(source):
        raise ValueError("Missing stage folder or source model")
    if os.path.splitext(source)[1].lower() not in SOURCE_EXTENSIONS:
        raise ValueError("Unsupported source format")
    if inside(stage, source):
        raise ValueError("The source must not live inside the staging folder")
    report = os.path.abspath(job["report"])
    if not inside(stage, report):
        raise ValueError("The report must be inside the staging folder")
    prefix = job.get("texturePrefix", "CharacterForge")
    if not re.fullmatch(r"[A-Za-z0-9_\- ]{1,64}(\\[A-Za-z0-9_\- ]{1,64}){0,3}", prefix):
        raise ValueError("Invalid texture prefix")
    nif_name = job.get("nifName", "model.nif")
    if not re.fullmatch(r"[A-Za-z0-9_\-]{1,64}\.nif", nif_name):
        raise ValueError("Invalid NIF file name")
    if job.get("targetGame", "SKYRIMSE") != "SKYRIMSE":
        raise ValueError("Only Skyrim Special Edition is supported")
    profile = job.get("profile", "static")
    if profile not in PROFILES:
        raise ValueError("Unsupported conversion profile")
    job.update(stage=stage, source=source, report=report, texturePrefix=prefix, nifName=nif_name, profile=profile)
    return job


def write_report(path, payload):
    temporary = path + ".tmp"
    with open(temporary, "w", encoding="utf-8") as stream:
        json.dump(payload, stream, ensure_ascii=False, sort_keys=True, indent=1)
    os.replace(temporary, path)


# ---------------------------------------------------------------------------------------------------------
# Loading: the original is opened or imported, then the session continues on a copy in the stage.

def load_source(source, stage):
    extension = os.path.splitext(source)[1].lower()
    if extension == ".blend":
        result = bpy.ops.wm.open_mainfile(filepath=source, load_ui=False)
    else:
        bpy.ops.wm.read_factory_settings(use_empty=True)
        if extension == ".fbx":
            result = bpy.ops.import_scene.fbx(filepath=source)
        elif extension == ".obj":
            result = bpy.ops.wm.obj_import(filepath=source)
        else:
            result = bpy.ops.import_scene.gltf(filepath=source)
    if "FINISHED" not in result:
        raise RuntimeError("Blender did not finish loading the model")
    work = os.path.join(stage, "work.blend")
    # copy=False keeps the session on the copy; relative_remap rewrites //relative paths for the new folder.
    bpy.ops.wm.save_as_mainfile(filepath=work, copy=False, relative_remap=True, compress=False)
    if os.path.normcase(os.path.abspath(bpy.data.filepath)) != os.path.normcase(work):
        raise RuntimeError("The session did not move to the working copy")
    return work


# ---------------------------------------------------------------------------------------------------------
# Inspection: facts only.

def image_facts(image):
    path = bpy.path.abspath(image.filepath, library=image.library) if image.filepath else ""
    exists = bool(path) and os.path.isfile(path)
    facts = {"name": image.name, "source": image.source, "packed": image.packed_file is not None,
             "filepath": path, "exists": exists, "fileFormat": image.file_format,
             "colorspace": image.colorspace_settings.name,
             "width": int(image.size[0]), "height": int(image.size[1])}
    if exists:
        facts["bytes"] = os.path.getsize(path)
    return facts


def linked_images(socket, seen=None):
    """Image nodes upstream of a socket, through any intermediate node (Normal Map, Mix, groups excluded)."""
    seen = seen if seen is not None else set()
    found = []
    for link in socket.links:
        node = link.from_node
        if node.name in seen:
            continue
        seen.add(node.name)
        if node.type == "TEX_IMAGE":
            found.append(node)
        else:
            for upstream in node.inputs:
                found.extend(linked_images(upstream, seen))
    return found


ROLE_SOCKETS = (("diffuse", "Base Color"), ("normal", "Normal"), ("alpha", "Alpha"), ("roughness", "Roughness"),
                ("metallic", "Metallic"), ("emission", "Emission Color"), ("specular", "Specular IOR Level"))


def material_facts(material):
    facts = {"name": material.name, "useNodes": bool(material.use_nodes), "shader": None, "images": [],
             "unsupportedChannels": [], "alpha": False, "pynifly": "PYN_shader" in material.keys() or None}
    if not material.use_nodes or material.node_tree is None:
        return facts
    output = next((n for n in material.node_tree.nodes if n.type == "OUTPUT_MATERIAL" and n.is_active_output), None)
    shader = None
    if output and output.inputs["Surface"].links:
        shader = output.inputs["Surface"].links[0].from_node
    facts["shader"] = shader.bl_idname if shader else None
    if shader is None or shader.type != "BSDF_PRINCIPLED":
        return facts
    for role, socket_name in ROLE_SOCKETS:
        socket = shader.inputs.get(socket_name)
        if socket is None:
            continue
        nodes = linked_images(socket)
        for node in nodes:
            if node.image is not None:
                facts["images"].append({"role": role, "image": node.image.name})
        if nodes and role in {"roughness", "metallic", "emission", "specular"}:
            facts["unsupportedChannels"].append(role)
        if role == "alpha" and (nodes or socket.default_value < 0.999):
            facts["alpha"] = True
    return facts


def mesh_facts(obj):
    import bmesh

    mesh = obj.data
    mesh.calc_loop_triangles()
    bm = bmesh.new()
    bm.from_mesh(mesh)
    non_manifold = sum(1 for e in bm.edges if not e.is_manifold and not e.is_boundary)
    boundary = sum(1 for e in bm.edges if e.is_boundary)
    loose_verts = sum(1 for v in bm.verts if not v.link_edges)
    zero_faces = sum(1 for f in bm.faces if f.calc_area() < 1e-10)
    bm.free()
    groups = {g.index: g.name for g in obj.vertex_groups}
    weighted = over_four = unnormalized = 0
    for vertex in mesh.vertices:
        weights = [g.weight for g in vertex.groups if g.group in groups and g.weight > 0.0]
        if weights:
            weighted += 1
            if len(weights) > 4:
                over_four += 1
            if abs(sum(weights) - 1.0) > 0.01:
                unnormalized += 1
    arma_object = next((m.object for m in obj.modifiers if m.type == "ARMATURE" and m.object), None)
    armature = arma_object.name if arma_object else None
    corners = [obj.matrix_world @ v.co for v in mesh.vertices] or []
    low = [min(c[i] for c in corners) for i in range(3)] if corners else [0, 0, 0]
    high = [max(c[i] for c in corners) for i in range(3)] if corners else [0, 0, 0]
    rotation = obj.matrix_world.to_euler()
    scale = obj.matrix_world.to_scale()
    facts = {
        "name": obj.name, "data": mesh.name, "parent": obj.parent.name if obj.parent else None,
        "vertices": len(mesh.vertices), "edges": len(mesh.edges), "faces": len(mesh.polygons),
        "triangles": len(mesh.loop_triangles), "ngons": sum(1 for p in mesh.polygons if p.loop_total > 4),
        "uvLayers": [uv.name for uv in mesh.uv_layers], "materials": [m.name if m else None for m in mesh.materials],
        "vertexGroups": len(groups), "weightedVertices": weighted, "verticesOver4Influences": over_four,
        "unnormalizedVertices": unnormalized, "armature": armature,
        "shapeKeys": [k.name for k in mesh.shape_keys.key_blocks] if mesh.shape_keys else [],
        "modifiers": [m.type for m in obj.modifiers],
        "nonManifoldEdges": non_manifold, "boundaryEdges": boundary, "looseVertices": loose_verts,
        "zeroAreaFaces": zero_faces, "customNormals": bool(getattr(mesh, "has_custom_normals", False)),
        "location": [round(v, 5) for v in obj.matrix_world.translation],
        "rotationEuler": [round(v, 5) for v in rotation], "scale": [round(v, 5) for v in scale],
        "negativeScale": obj.matrix_world.determinant() < 0,
        "boundsMin": [round(v, 5) for v in low], "boundsMax": [round(v, 5) for v in high],
    }
    if arma_object is not None:
        facts["skin"] = skin_facts(obj, arma_object)
    return facts


def skin_facts(obj, arma):
    """Influences exactly as PyNifly 29.1 will write them (trim_weights + get_weights_by_bone): only groups named
    after a bone of the armature, weights above PYNIFLY_MIN_WEIGHT, the four heaviest, normalized to 1. Positions are
    in armature space, the frame of the NIF skin (export_skin: skin_xf = arma^-1 @ obj)."""
    mesh = obj.data
    bones = set(arma.data.bones.keys())
    names = {g.index: g.name for g in obj.vertex_groups}
    to_arma = arma.matrix_world.inverted() @ obj.matrix_world
    stride = max(1, -(-len(mesh.vertices) // WEIGHT_SAMPLES))
    per_bone, partitions, others, samples = {}, set(), set(), []
    unweighted = trimmed = 0
    low, high = [1e30] * 3, [-1e30] * 3
    for vertex in mesh.vertices:
        pairs = []
        for g in vertex.groups:
            name = names.get(g.group)
            if name is None or g.weight <= 0.0:
                continue
            if name in bones:
                if g.weight > PYNIFLY_MIN_WEIGHT:
                    pairs.append((g.weight, name))
            elif PARTITION_GROUP.match(name):
                partitions.add(name)
            else:
                others.add(name)
        position = to_arma @ vertex.co
        for i in range(3):
            low[i], high[i] = min(low[i], position[i]), max(high[i], position[i])
        if not pairs:
            unweighted += 1
            continue
        pairs.sort(reverse=True)
        trimmed += len(pairs) > 4
        top = pairs[:4]
        total = sum(w for w, _ in top)
        for _, name in top:
            per_bone[name] = per_bone.get(name, 0) + 1
        if vertex.index % stride == 0:
            samples.append({"p": [round(c, 4) for c in position], "w": {n: round(w / total, 4) for w, n in top}})
    return {"armature": arma.name, "weightedBones": dict(sorted(per_bone.items())), "unweightedVertices": unweighted,
            "verticesOver4Influences": trimmed, "partitionGroups": sorted(partitions), "nonBoneGroups": sorted(others),
            "boundsMin": [round(v, 5) for v in low], "boundsMax": [round(v, 5) for v in high],
            "weightSamples": samples, "weightSampleStride": stride}


PRETTY_BONES_PROP = "PYN_ROTATE_BONES_PRETTY"  # io_scene_nifly util/settings.py PYN_ROTATE_BONES_PRETTY_PROP


def rotate_bones_pretty(arma):
    """The bone orientation convention of this armature, as PyNifly's export dialog seeds it: the flag its importer
    stamps on the armature (ExportSettings default False). A background export must pass it explicitly: without it
    PyNifly 29.1 writes every skin-to-bone rotated 90 degrees about X from the bones of a 'pretty' armature."""
    return bool(arma.get(PRETTY_BONES_PROP, False))


def nif_bone_matrix(arma, name, pretty):
    """The bone's bind transform as PyNifly writes it for SKYRIMSE (armature space) with the same bone convention the
    export uses: PyNifly's own get_bone_global_xf under the rotation table its exporter selects (export_nif.execute)."""
    try:
        from io_scene_nifly import blender_defs
        saved = blender_defs.game_rotations
        blender_defs.game_rotations = blender_defs.game_rotations_pretty if pretty else blender_defs.game_rotations_none
        try:
            return blender_defs.get_bone_global_xf(arma, name, "SKYRIMSE", False), "pynifly"
        finally:
            blender_defs.game_rotations = saved
    except ImportError:
        import math
        from mathutils import Matrix
        local = arma.data.bones[name].matrix_local
        return (local @ Matrix.Rotation(math.radians(-90.0), 4, "X") if pretty else local.copy()), "formula"


def armature_facts(obj):
    bones = [b.name for b in obj.data.bones]
    detail, source = [], None
    pretty = rotate_bones_pretty(obj)
    for bone in list(obj.data.bones)[:MAX_BONES]:
        matrix, source = nif_bone_matrix(obj, bone.name, pretty)
        translation, rotation, scale = matrix.decompose()
        detail.append({"name": bone.name, "parent": bone.parent.name if bone.parent else None, "deform": bone.use_deform,
                       "translation": [round(v, 5) for v in translation],
                       "rotation": [[round(v, 6) for v in row] for row in rotation.to_matrix()],
                       "scale": round(max(scale), 6), "uniformScale": max(scale) - min(scale) < 1e-4})
    pose_delta = 0.0
    for pose_bone in obj.pose.bones:
        a, b = pose_bone.matrix, pose_bone.bone.matrix_local
        pose_delta = max(pose_delta, max(abs(a[i][j] - b[i][j]) for i in range(4) for j in range(4)))
    world = obj.matrix_world
    identity = all(abs(world[i][j] - (1.0 if i == j else 0.0)) < 1e-6 for i in range(4) for j in range(4))
    return {"name": obj.name, "bones": len(bones), "boneNames": bones[:MAX_BONES],
            "skyrimHumanoidBones": sorted(SKYRIM_HUMANOID_BONES.intersection(bones)),
            "pyniflyGame": obj.get("PYN_GAME", None),
            "scale": [round(v, 5) for v in obj.matrix_world.to_scale()],
            "bonesDetail": detail, "bonesTruncated": len(bones) > MAX_BONES, "bindSource": source, "rotateBonesPretty": pretty,
            "worldIdentity": identity, "poseDelta": round(pose_delta, 6)}


def pynifly_facts():
    import addon_utils

    facts = {"module": PYNIFLY_MODULE, "available": False}
    try:
        module = addon_utils.enable(PYNIFLY_MODULE, default_set=False, handle_error=None)
    except Exception as error:  # an add-on that fails to register is reported, never fatal for inspection
        facts["error"] = str(error)[:500]
        return facts
    if module is None:
        facts["error"] = "PyNifly add-on is not installed"
        return facts
    version = tuple(getattr(module, "bl_info", {}).get("version", ()))
    facts.update(version=list(version), tested=version in PYNIFLY_TESTED_VERSIONS,
                 path=os.path.dirname(getattr(module, "__file__", "") or ""))
    if not hasattr(bpy.ops.export_scene, "pynifly"):
        facts["error"] = "export_scene.pynifly is not registered"
        return facts
    properties = bpy.ops.export_scene.pynifly.get_rna_type().properties
    facts["exportOperator"] = "export_scene.pynifly"
    facts["targetGames"] = sorted({item.identifier for item in properties["target_game"].enum_items})
    facts["exportProperties"] = sorted(k for k in properties.keys() if k != "rna_type")
    required = {"filepath", "target_game", "intuit_defaults", "blender_xf", "export_modifiers", "write_tris"}
    missing = sorted(required.difference(facts["exportProperties"]))
    if missing or "SKYRIMSE" not in facts["targetGames"]:
        facts["error"] = "Installed PyNifly lacks: " + ", ".join(missing or ["SKYRIMSE target"])
        return facts
    facts["available"] = True
    return facts


def inspect_scene():
    objects = list(bpy.context.scene.objects)
    meshes = [mesh_facts(o) for o in objects if o.type == "MESH"]
    armatures = [armature_facts(o) for o in objects if o.type == "ARMATURE"]
    used_materials = {name for m in meshes for name in m["materials"] if name}
    materials = [material_facts(m) for m in bpy.data.materials if m.name in used_materials]
    used_images = {i["image"] for m in materials for i in m["images"]}
    images = [image_facts(i) for i in bpy.data.images if i.name in used_images]
    # Same mesh data, same bounds and same placement: a duplicated object exported twice.
    signatures = {}
    for m in meshes:
        key = (m["vertices"], m["triangles"], tuple(m["boundsMin"]), tuple(m["boundsMax"]))
        signatures.setdefault(key, []).append(m["name"])
    duplicates = [names for names in signatures.values() if len(names) > 1]
    low = [min((m["boundsMin"][i] for m in meshes), default=0.0) for i in range(3)]
    high = [max((m["boundsMax"][i] for m in meshes), default=0.0) for i in range(3)]
    return {
        "objects": len(objects), "objectTypes": sorted({o.type for o in objects}),
        "unitScale": bpy.context.scene.unit_settings.scale_length,
        "meshes": meshes, "armatures": armatures, "materials": materials, "images": images,
        "duplicates": duplicates,
        "totals": {"vertices": sum(m["vertices"] for m in meshes), "triangles": sum(m["triangles"] for m in meshes),
                   "meshes": len(meshes), "armatures": len(armatures), "materials": len(materials),
                   "images": len(images), "shapeKeys": sum(len(m["shapeKeys"]) for m in meshes)},
        "boundsMin": low, "boundsMax": high, "dimensions": [round(high[i] - low[i], 5) for i in range(3)],
    }


# ---------------------------------------------------------------------------------------------------------
# Preparation on the working copy.

def slug(text):
    value = re.sub(r"[^a-z0-9_]+", "_", os.path.splitext(text)[0].lower()).strip("_")
    return (value or "texture")[:48]


def stage_textures(stage, prefix):
    """Writes every used image as PNG under <stage>/Data/textures/<prefix>/ and points the image there, so the
    exporter derives a Data-relative 'textures\\...' path instead of an absolute one. Missing images are
    detached from their node (an absolute path to a missing file would be written otherwise)."""
    folder = os.path.join(stage, "Data", "textures", *prefix.split("\\"))
    os.makedirs(folder, exist_ok=True)
    staged, missing, names = [], [], set()
    used = set()
    for obj in bpy.context.scene.objects:
        if obj.type == "MESH":
            for material in obj.data.materials:
                if material and material.use_nodes and material.node_tree:
                    for node in material.node_tree.nodes:
                        if node.type == "TEX_IMAGE" and node.image is not None:
                            used.add((material.name, node.name))
    for material_name, node_name in sorted(used):
        node = bpy.data.materials[material_name].node_tree.nodes[node_name]
        image = node.image
        facts = image_facts(image)
        if not facts["exists"] and not facts["packed"] and image.source != "GENERATED":
            missing.append({"image": image.name, "filepath": facts["filepath"], "material": material_name})
            node.image = None
            continue
        if image.get("sns_staged"):
            continue
        name = slug(image.name)
        while name in names:
            name += "_x"
        names.add(name)
        target = os.path.join(folder, name + ".png")
        if facts["exists"] and facts.get("bytes", 0) > MAX_IMAGE_BYTES:
            raise ValueError("Texture larger than 256 MiB: " + image.name)
        image.file_format = "PNG"
        image.save(filepath=target)
        image.filepath = target
        image["sns_staged"] = True
        staged.append({"image": image.name, "source": facts["filepath"], "packed": facts["packed"],
                       "png": os.path.relpath(target, stage).replace("/", "\\"),
                       "nifPath": "textures\\" + prefix.lower() + "\\" + name + ".dds",
                       "width": int(image.size[0]), "height": int(image.size[1]),
                       "colorspace": facts["colorspace"]})
    return {"folder": os.path.relpath(folder, stage), "staged": staged, "missing": missing}


def apply_safe_transforms():
    """Applies rotation and scale only to meshes without shape keys, armature or parent: their vertex order and
    morph data cannot be invalidated. Everything else is reported, never changed."""
    applied, skipped = [], []
    for obj in [o for o in bpy.context.scene.objects if o.type == "MESH"]:
        rotation = obj.rotation_euler
        identity = all(abs(v - 1.0) < 1e-5 for v in obj.scale) and all(abs(v) < 1e-6 for v in rotation)
        if identity:
            continue
        reasons = []
        if obj.data.shape_keys:
            reasons.append("shapeKeys")
        if any(m.type == "ARMATURE" for m in obj.modifiers) or obj.parent is not None:
            reasons.append("armatureOrParent")
        if obj.data.users > 1:
            reasons.append("sharedMeshData")
        if reasons:
            skipped.append({"object": obj.name, "reasons": reasons})
            continue
        bpy.ops.object.select_all(action="DESELECT")
        obj.select_set(True)
        bpy.context.view_layer.objects.active = obj
        bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
        applied.append(obj.name)
    return {"applied": applied, "skipped": skipped}


SHAPE_LIMIT = 65535          # BSTriShape: 16-bit indices
DECIMATE_TARGET = 60000      # triangles left after the automatic reduction (margin for UV-seam vertex splits)
UNITS_PER_METER = 100.0 / 1.428  # Skyrim: 1 unit = 1.428 cm


def rigid(obj):
    """A mesh the job may change: no shape keys, armature, parent or shared data (vertex order cannot matter)."""
    return (not obj.data.shape_keys and not any(m.type == "ARMATURE" for m in obj.modifiers)
            and obj.parent is None and obj.data.users == 1)


def reduce_polycount():
    """Decimate (collapse) every rigid mesh over the BSTriShape limit down to about DECIMATE_TARGET triangles."""
    reduced, skipped = [], []
    for obj in [o for o in bpy.context.scene.objects if o.type == "MESH"]:
        obj.data.calc_loop_triangles()
        before_tris, before_verts = len(obj.data.loop_triangles), len(obj.data.vertices)
        if before_tris <= SHAPE_LIMIT and before_verts <= SHAPE_LIMIT:
            continue
        if not rigid(obj):
            skipped.append({"object": obj.name, "reason": "shapeKeysArmatureParentOrShared"})
            continue
        modifier = obj.modifiers.new("SNS_Decimate", "DECIMATE")
        modifier.decimate_type = "COLLAPSE"
        modifier.ratio = max(0.0001, DECIMATE_TARGET / before_tris)
        bpy.ops.object.select_all(action="DESELECT")
        obj.select_set(True)
        bpy.context.view_layer.objects.active = obj
        bpy.ops.object.modifier_apply(modifier=modifier.name)
        obj.data.calc_loop_triangles()
        reduced.append({"object": obj.name, "trianglesBefore": before_tris, "verticesBefore": before_verts,
                        "trianglesAfter": len(obj.data.loop_triangles), "verticesAfter": len(obj.data.vertices)})
    return {"reduced": reduced, "skipped": skipped}


def meters_to_units():
    """A scene under 2 units tall was modelled in meters: scale every mesh by 1 m = 70.03 units about the world origin
    and apply it. Only when every mesh is rigid (a rig would need its armature scaled too)."""
    meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    if not meshes:
        return {"applied": False, "reason": "noMeshes"}
    corners = [o.matrix_world @ v.co for o in meshes for v in o.data.vertices]
    extent = max(max(c[i] for c in corners) - min(c[i] for c in corners) for i in range(3)) if corners else 0.0
    if extent >= 2.0:
        return {"applied": False, "reason": "notMeters", "extent": round(extent, 5)}
    if any(o.type == "ARMATURE" for o in bpy.context.scene.objects) or not all(rigid(o) for o in meshes):
        return {"applied": False, "reason": "rigOrMorphs", "extent": round(extent, 5)}
    for obj in meshes:
        obj.location = obj.location * UNITS_PER_METER
        obj.scale = obj.scale * UNITS_PER_METER
        bpy.ops.object.select_all(action="DESELECT")
        obj.select_set(True)
        bpy.context.view_layer.objects.active = obj
        bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
    return {"applied": True, "factor": round(UNITS_PER_METER, 5), "extentBefore": round(extent, 5),
            "extentAfter": round(extent * UNITS_PER_METER, 3)}


def export_nif(job, report):
    meshes = [o for o in bpy.context.scene.objects if o.type == "MESH" and o.visible_get()]
    if not meshes:
        raise ValueError("No visible mesh object to export")
    output = os.path.join(job["stage"], "Data", "meshes", *job["texturePrefix"].split("\\"), job["nifName"])
    os.makedirs(os.path.dirname(output), exist_ok=True)
    if bpy.context.object and bpy.context.object.mode != "OBJECT":
        bpy.ops.object.mode_set(mode="OBJECT")
    bpy.ops.object.select_all(action="DESELECT")
    for obj in meshes:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    settings = {"target_game": "SKYRIMSE", "intuit_defaults": False, "blender_xf": False,
                "export_modifiers": False, "write_tris": False, "export_animations": False,
                "export_pose": False, "rename_bones": True, "preserve_hierarchy": False}
    rigs = [m.object for o in meshes for m in o.modifiers if m.type == "ARMATURE" and m.object]
    if rigs:  # the same convention armature_facts reported, or the bind is written 90 degrees off (see rotate_bones_pretty)
        settings["rotate_bones_pretty"] = rotate_bones_pretty(rigs[0])
    report["export"] = {"operator": "export_scene.pynifly", "settings": settings,
                        "objects": [o.name for o in meshes], "output": os.path.relpath(output, job["stage"])}
    # PyNifly 29.1 returns an empty set on success and {'CANCELLED'} on failure; its findings (non-uniform
    # scale, unweighted vertices, missing partitions...) go to Python logging, so they are captured here.
    import logging

    class Collector(logging.Handler):
        def __init__(self):
            super().__init__(logging.WARNING)
            self.records = []

        def emit(self, record):
            if len(self.records) < 200:
                self.records.append({"level": record.levelname, "logger": record.name,
                                     "message": record.getMessage()[:500]})

    collector = Collector()
    logging.getLogger().addHandler(collector)
    try:
        result = bpy.ops.export_scene.pynifly(filepath=output, **settings)
    finally:
        logging.getLogger().removeHandler(collector)
    report["export"]["result"] = sorted(result)
    report["export"]["log"] = collector.records
    if "CANCELLED" in result or not os.path.isfile(output):
        raise RuntimeError("PyNifly did not write the NIF")
    if any(r["level"] in {"ERROR", "CRITICAL"} for r in collector.records):
        raise RuntimeError("PyNifly reported errors: " + "; ".join(
            r["message"] for r in collector.records if r["level"] in {"ERROR", "CRITICAL"})[:600])
    report["export"]["bytes"] = os.path.getsize(output)
    report["export"]["sha256"] = sha256(output)
    try:
        from io_scene_nifly.pyn import pynifly
        log = pynifly.NifFile.message_log()
        report["export"]["niflyLog"] = [line for line in log.split("\n") if line.strip()][:200]
    except Exception:
        pass
    return output


def readback(path):
    from io_scene_nifly.pyn.pynifly import NifFile

    nif = NifFile(filepath=path)
    try:
        return {"game": nif.game, "shapes": [{
            "name": shape.name, "vertices": len(shape.verts), "triangles": len(shape.tris),
            "uvs": len(shape.uvs) if getattr(shape, "uvs", None) is not None else 0,
            "normals": len(shape.normals) if getattr(shape, "normals", None) is not None else 0,
            "bones": len(shape.bone_names), "boneNames": sorted(shape.bone_names)[:MAX_BONES],
            "partitions": len(getattr(shape, "partitions", None) or []), "shaderBlock": shape.shader_block_name,
            "textures": {slot: str(value) for slot, value in shape.textures.items()},
        } for shape in nif.shapes]}
    finally:
        nif.close()


def main():
    job = read_job()
    report = {"schema": SCHEMA, "operation": job["operation"], "profile": job["profile"], "status": "error",
              "blenderVersion": bpy.app.version_string, "sourceFormat": os.path.splitext(job["source"])[1][1:].lower(),
              "sourceSha256": sha256(job["source"]), "sourceBytes": os.path.getsize(job["source"])}
    try:
        progress("load", 0.05)
        report["workCopy"] = os.path.relpath(load_source(job["source"], job["stage"]), job["stage"])
        progress("inspect", 0.25)
        report["pynifly"] = pynifly_facts()
        report["inspection"] = inspect_scene()
        if job["operation"] == "exportNif":
            if not report["pynifly"]["available"]:
                raise RuntimeError("PyNifly is not usable: " + report["pynifly"].get("error", "unknown"))
            progress("prepare", 0.45)
            report["prepare"] = {"transforms": apply_safe_transforms() if job.get("applySafeTransforms", True)
                                 else {"applied": [], "skipped": []}}
            if job.get("reducePolycount", True):
                progress("reduce", 0.5)
                report["prepare"]["polycount"] = reduce_polycount()
            if job.get("metersToUnits", True):
                report["prepare"]["scale"] = meters_to_units()
            report["prepare"]["textures"] = stage_textures(job["stage"], job["texturePrefix"])
            report["prepared"] = inspect_scene()
            bpy.ops.wm.save_mainfile()
            progress("export", 0.7)
            output = export_nif(job, report)
            progress("readback", 0.9)
            report["readback"] = readback(output)
        if sha256(job["source"]) != report["sourceSha256"]:
            raise RuntimeError("The source model changed during the job")
        report["status"] = "ok"
        write_report(job["report"], report)
        progress("done", 1.0)
        print("SNS_FORGE_SKYRIM_OK", flush=True)
    except Exception as error:
        report.update(status="error", message=str(error)[:1000], trace=traceback.format_exc()[-4000:])
        write_report(job["report"], report)
        print("SNS_FORGE_SKYRIM_ERROR " + str(error)[:300], flush=True)
        raise


if __name__ == "__main__":
    main()
