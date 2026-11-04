"""Versioned Character Forge conversion: Blender input -> one GLB plus a JSON report.

Run only through the application with --background --factory-startup --disable-autoexec:
  blender ... --python forge_bridge.py -- SOURCE OUTPUT.glb REPORT.json
The C++ caller validates the output with engines/gltf before adding it to a project.
"""

import json
import os
import struct
import sys

import bpy


def args():
    values = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if len(values) != 3:
        raise ValueError("Expected source, output GLB and report JSON")
    source, output, report = map(os.path.abspath, values)
    if os.path.splitext(source)[1].lower() not in {".fbx", ".obj", ".blend"}:
        raise ValueError("Only FBX, OBJ and BLEND inputs are supported")
    if os.path.splitext(output)[1].lower() != ".glb" or not os.path.isfile(source):
        raise ValueError("Invalid source or output path")
    return source, output, report


def write_report(path, payload):
    temporary = path + ".tmp"
    with open(temporary, "w", encoding="utf-8") as stream:
        json.dump(payload, stream, ensure_ascii=False, sort_keys=True)
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, path)


def _prune_non_renderable_and_rig_widgets():
    widget_objs = set()
    for obj in list(bpy.data.objects):
        if obj.type == "ARMATURE" and getattr(obj, "pose", None):
            for pb in obj.pose.bones:
                if getattr(pb, "custom_shape", None) is not None:
                    widget_objs.add(pb.custom_shape)

    all_meshes = [obj for obj in bpy.data.objects if obj.type == "MESH"]
    non_widgets = [
        obj for obj in all_meshes
        if obj not in widget_objs
        and not obj.name.lower().startswith(("cs_", "wgt-", "wgt_", "wgts_"))
        and getattr(obj, "data", None) is not None
        and len(obj.data.vertices) > 0
        and len(obj.data.polygons) > 0
        and getattr(obj, "display_type", "TEXTURED") not in {"WIRE", "BOUNDS"}
    ]
    # Prefer visible meshes in the active view layer (respects collection visibility and object visibility)
    visible_meshes = [
        obj for obj in non_widgets
        if obj.visible_get()
        and not obj.hide_get()
        and not getattr(obj, "hide_viewport", False)
        and not getattr(obj, "hide_render", False)
    ]
    if not visible_meshes:
        visible_meshes = [
            obj for obj in non_widgets
            if not obj.hide_get()
            and not getattr(obj, "hide_viewport", False)
            and not getattr(obj, "hide_render", False)
        ]
    target_meshes = visible_meshes if visible_meshes else non_widgets
    if target_meshes:
        keep = set(target_meshes)
        for obj in list(all_meshes):
            if obj not in keep:
                bpy.data.objects.remove(obj, do_unlink=True)

    # Remove unused armatures and ensure bone-parented / deforming ancestor chains stay connected under export_def_bones
    kept_meshes = [obj for obj in bpy.data.objects if obj.type == "MESH"]
    used_armatures = set()
    for obj in kept_meshes:
        if getattr(obj, "parent", None) is not None and obj.parent.type == "ARMATURE":
            used_armatures.add(obj.parent)
        for mod in getattr(obj, "modifiers", []):
            if mod.type == "ARMATURE" and getattr(mod, "object", None) is not None:
                used_armatures.add(mod.object)
    if used_armatures:
        for obj in list(bpy.data.objects):
            if obj.type == "ARMATURE" and obj not in used_armatures:
                bpy.data.objects.remove(obj, do_unlink=True)

    for arm in [obj for obj in bpy.data.objects if obj.type == "ARMATURE" and getattr(obj, "data", None)]:
        for obj in kept_meshes:
            if obj.parent == arm and obj.parent_type == "BONE" and obj.parent_bone in arm.data.bones:
                arm.data.bones[obj.parent_bone].use_deform = True
        for bone in list(arm.data.bones):
            if bone.use_deform:
                cur = bone.parent
                while cur is not None:
                    cur.use_deform = True
                    cur = cur.parent

    # Disable expensive subdivision/multires modifiers on meshes with > 500 base vertices for preview speed
    for obj in kept_meshes:
        if getattr(obj, "data", None) and len(obj.data.vertices) > 500:
            for mod in getattr(obj, "modifiers", []):
                if mod.type in {"SUBSURF", "MULTIRES"}:
                    mod.show_viewport = False
                    mod.show_render = False


def _optimize_preview_images(max_dim=1024):
    for img in list(bpy.data.images):
        try:
            w, h = img.size[0], img.size[1]
            if w > max_dim or h > max_dim:
                scale = max_dim / float(max(w, h))
                nw = max(1, int(round(w * scale)))
                nh = max(1, int(round(h * scale)))
                img.scale(nw, nh)
        except Exception:
            pass


def _sanitize_and_verify_glb(glb_path):
    with open(glb_path, "rb") as f:
        data = f.read()
    if len(data) < 20:
        raise RuntimeError("Generated GLB is truncated (< 20 bytes)")
    magic, version, total_len = struct.unpack_from("<4sII", data, 0)
    if magic != b"glTF" or version != 2 or total_len > len(data):
        raise RuntimeError("Generated GLB has an invalid header or length")
    json_len, json_type = struct.unpack_from("<I4s", data, 12)
    if json_type != b"JSON" or 20 + json_len > total_len:
        raise RuntimeError("Generated GLB is missing a valid JSON chunk")
    doc = json.loads(data[20:20 + json_len].decode("utf-8"))
    nodes = doc.get("nodes", [])
    scenes = doc.get("scenes", [])
    if not nodes or not scenes:
        return
    scene_idx = doc.get("scene", 0)
    if not isinstance(scene_idx, int) or scene_idx < 0 or scene_idx >= len(scenes):
        scene_idx = 0
    scene_roots = scenes[scene_idx].setdefault("nodes", [])
    parent_of = {}
    for idx, n in enumerate(nodes):
        for c in n.get("children", []):
            if isinstance(c, int) and 0 <= c < len(nodes):
                parent_of[c] = idx
    reachable = set()

    def walk(nidx):
        if nidx in reachable or not (0 <= nidx < len(nodes)):
            return
        reachable.add(nidx)
        for c in nodes[nidx].get("children", []):
            if isinstance(c, int):
                walk(c)

    for r in scene_roots:
        if isinstance(r, int):
            walk(r)

    modified = False
    for skin in doc.get("skins", []):
        joints = skin.get("joints", [])
        # Find common root parent of reachable joints if possible
        skin_root = skin.get("skeleton")
        if not isinstance(skin_root, int) or skin_root not in reachable:
            skin_root = None
            for j in joints:
                if isinstance(j, int) and j in parent_of and parent_of[j] in reachable:
                    skin_root = parent_of[j]
                    break
        for j in joints:
            if isinstance(j, int) and 0 <= j < len(nodes) and j not in reachable:
                # Climb to highest unparented ancestor of j
                top = j
                visited = {top}
                while top in parent_of and parent_of[top] not in visited:
                    top = parent_of[top]
                    visited.add(top)
                if skin_root is not None and skin_root != top:
                    children = nodes[skin_root].setdefault("children", [])
                    if top not in children:
                        children.append(top)
                    parent_of[top] = skin_root
                else:
                    if top not in scene_roots:
                        scene_roots.append(top)
                walk(top)
                modified = True

    if not modified:
        return

    new_json = json.dumps(doc, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    pad = (4 - (len(new_json) % 4)) % 4
    if pad:
        new_json += b" " * pad
    rest = data[20 + json_len:total_len]
    new_total = 20 + len(new_json) + len(rest)
    tmp_path = glb_path + ".tmp"
    with open(tmp_path, "wb") as out:
        out.write(struct.pack("<4sII", b"glTF", 2, new_total))
        out.write(struct.pack("<I4s", len(new_json), b"JSON"))
        out.write(new_json)
        out.write(rest)
        out.flush()
        os.fsync(out.fileno())
    os.replace(tmp_path, glb_path)


def convert(source, output):
    extension = os.path.splitext(source)[1].lower()
    bpy.ops.wm.read_factory_settings(use_empty=True)
    if extension == ".blend":
        result = bpy.ops.wm.open_mainfile(filepath=source, load_ui=False)
    elif extension == ".fbx":
        result = bpy.ops.import_scene.fbx(filepath=source)
    else:
        result = bpy.ops.wm.obj_import(filepath=source)
    if "FINISHED" not in result:
        raise RuntimeError("Blender did not finish importing the model")
    _prune_non_renderable_and_rig_widgets()
    mesh_objects = [obj for obj in bpy.data.objects if obj.type == "MESH"]
    meshes = len(mesh_objects)
    if not meshes:
        raise ValueError("The source has no mesh objects")
    total_vertices = sum(len(obj.data.vertices) for obj in mesh_objects if getattr(obj, "data", None))
    heavy_scene = total_vertices > 50000 or os.path.getsize(source) > 50 * 1024 * 1024
    if heavy_scene:
        _optimize_preview_images(1024)
    export_kwargs = dict(
        filepath=output,
        export_format="GLB",
        export_skins=True,
        export_morph=not heavy_scene,
        export_animations=not heavy_scene,
        export_yup=True,
    )
    if heavy_scene:
        export_kwargs["export_def_bones"] = True
        export_kwargs["export_apply"] = False
    try:
        exported = bpy.ops.export_scene.gltf(**export_kwargs)
    except TypeError:
        export_kwargs.pop("export_def_bones", None)
        export_kwargs.pop("export_apply", None)
        exported = bpy.ops.export_scene.gltf(**export_kwargs)
    if "FINISHED" not in exported or not os.path.isfile(output):
        raise RuntimeError("Blender did not produce a GLB")
    _sanitize_and_verify_glb(output)
    return meshes


def main():
    source, output, report = args()
    result = {"schema": 1, "blenderVersion": bpy.app.version_string,
              "sourceFormat": os.path.splitext(source)[1][1:].lower()}
    try:
        meshes = convert(source, output)
        result.update(status="ok", meshes=meshes, outputBytes=os.path.getsize(output))
        write_report(report, result)
        print("SNS_FORGE_BRIDGE_OK", flush=True)
        sys.stdout.flush()
        sys.stderr.flush()
        os._exit(0)
    except Exception as error:
        result.update(status="error", message=str(error)[:1000])
        write_report(report, result)
        print("SNS_FORGE_BRIDGE_ERROR", flush=True)
        raise


if __name__ == "__main__":
    main()
