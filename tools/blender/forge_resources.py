"""Character Forge: resources of a model loaded in Blender (shared by forge_bridge.py and forge_skyrim.py).

Images whose file is missing are looked for BY NAME near the source model and relinked IN THE OPEN SESSION ONLY: the
callers work on a preview export or on a working copy, never on the user's file. The search is bounded (breadth first,
folders named like textures first, limited depth, entries and time), and a name found in more than one place is relinked
only when one candidate matches the path the model expected (for example textures\\Name.png); otherwise it is reported as
ambiguous and left missing. Every decision is in the returned report, so the diagnosis can show it.
"""

import os
import shutil
import sys
import time
from collections import deque

import bpy


def announce_tempdir():
    """First line of a run: Blender's session temp folder, so ForgeBlenderBridge can remove it even when it has to kill
    Blender (cancel, timeout) before any report exists."""
    print("SNS_FORGE_TEMPDIR " + bpy.app.tempdir, flush=True)


def finish_process(code):
    """Ends Blender without its scene teardown, once the caller's report and output are on disk.

    Blender 5.2.3 was seen crashing in that teardown (0xC0000005, execute access at a freed address) after a 578 MiB
    .blend had been exported correctly. A fast exit also skips Blender's removal of its session temp folder
    (bpy.app.tempdir, "<temp>/blender_xxxx"), so it is removed here; ForgeBlenderBridge removes it after a crash too.
    """
    sys.stdout.flush()
    sys.stderr.flush()
    temp = bpy.app.tempdir
    if temp and os.path.basename(os.path.normpath(temp)).startswith("blender_"):
        shutil.rmtree(temp, ignore_errors=True)
    os._exit(code)

IMAGE_EXTENSIONS = {".png", ".jpg", ".jpeg", ".tga", ".tif", ".tiff", ".bmp", ".dds", ".exr", ".hdr", ".webp"}
TEXTURE_FOLDER_NAMES = {"textures", "texture", "texturas", "textura", "tex", "maps", "images", "imagenes"}
SKIP_FOLDER_NAMES = {"$recycle.bin", "system volume information", "windows", "program files", "program files (x86)",
                     "programdata", "appdata", ".git", "node_modules", "__pycache__"}


def _missing_images():
    missing = []
    for image in bpy.data.images:
        if image.source not in {"FILE", "TILED"} or image.packed_file is not None or not image.filepath:
            continue
        path = bpy.path.abspath(image.filepath, library=image.library)
        if not os.path.isfile(path):
            missing.append((image, path))
    return missing


def _index(root, wanted, max_depth, max_entries, max_seconds):
    """Breadth-first scan of `root` for files whose lower-case name is in `wanted`: {name: [paths]} plus scan facts."""
    found = {}
    started = time.perf_counter()
    entries = 0
    truncated = False
    queue = deque([(root, 0)])
    while queue:
        folder, depth = queue.popleft()
        try:
            with os.scandir(folder) as it:
                children = list(it)
        except OSError:
            continue
        subfolders = []
        for entry in children:
            entries += 1
            if entries > max_entries or time.perf_counter() - started > max_seconds:
                truncated = True
                queue.clear()
                break
            try:
                if entry.is_file(follow_symlinks=False):
                    name = entry.name.lower()
                    if name in wanted:
                        found.setdefault(name, []).append(entry.path)
                elif entry.is_dir(follow_symlinks=False) and depth < max_depth:
                    lowered = entry.name.lower()
                    if lowered in SKIP_FOLDER_NAMES or lowered.startswith("."):
                        continue
                    subfolders.append((lowered in TEXTURE_FOLDER_NAMES, entry.path))
            except OSError:
                continue
        # Texture-like folders first at every level
        for _, path in sorted(subfolders, key=lambda item: (not item[0], item[1].lower())):
            queue.append((path, depth + 1))
    return found, {"entries": entries, "seconds": round(time.perf_counter() - started, 3), "truncated": truncated}


def _tail(path, parts):
    pieces = [p for p in os.path.normcase(os.path.normpath(path)).replace("/", "\\").split("\\") if p]
    return pieces[-parts:]


PREVIEW_TRIANGLE_BUDGET = 300000


def reduce_for_preview(budget=PREVIEW_TRIANGLE_BUDGET):
    """Lightens a heavy scene FOR THE 3D PREVIEW ONLY (forge_bridge.py; the Skyrim job re-reads the source file): when the
    meshes add up to more than `budget` triangles, each mesh over 2000 triangles is decimated (collapse) by the same ratio.
    Shape keys are dropped first (a heavy preview does not export morphs) and shared mesh data is made single-user.
    Returns {applied, budget, trianglesBefore, trianglesAfter, objects: [{object, before, after}]}."""
    meshes = [o for o in bpy.context.scene.objects if o.type == "MESH" and o.data is not None]
    counts = {}
    for obj in meshes:
        obj.data.calc_loop_triangles()
        counts[obj.name] = len(obj.data.loop_triangles)
    total = sum(counts.values())
    result = {"applied": False, "budget": budget, "trianglesBefore": total, "trianglesAfter": total, "objects": []}
    if total <= budget:
        return result
    ratio = max(0.0005, budget / float(total))
    for obj in meshes:
        before = counts[obj.name]
        if before <= 2000:
            continue
        if obj.data.users > 1:
            obj.data = obj.data.copy()
        if obj.data.shape_keys is not None:
            obj.shape_key_clear()
        modifier = obj.modifiers.new("SNS_PreviewDecimate", "DECIMATE")
        modifier.decimate_type = "COLLAPSE"
        modifier.ratio = ratio
        with bpy.context.temp_override(object=obj, active_object=obj, selected_objects=[obj]):
            bpy.ops.object.modifier_move_to_index(modifier=modifier.name, index=0)
            bpy.ops.object.modifier_apply(modifier=modifier.name)
        obj.data.calc_loop_triangles()
        after = len(obj.data.loop_triangles)
        result["objects"].append({"object": obj.name, "before": before, "after": after})
        counts[obj.name] = after
    result["applied"] = bool(result["objects"])
    result["trianglesAfter"] = sum(counts.values())
    return result


def relink_missing_images(source, extra_roots=(), max_depth=3, max_entries=40000, max_seconds=6.0):
    """Relinks missing images of the open session from files of the same name near `source`. Returns
    {missingBefore, relinked: [{name, from, to}], ambiguous: [{name, expected, candidates}], stillMissing: [{name, path}],
     roots, scan: {entries, seconds, truncated}}."""
    missing = _missing_images()
    result = {"missingBefore": len(missing), "relinked": [], "ambiguous": [], "stillMissing": [], "roots": [],
              "scan": {"entries": 0, "seconds": 0.0, "truncated": False}}
    if not missing:
        return result
    wanted = {os.path.basename(path).lower() for _, path in missing if os.path.splitext(path)[1].lower() in IMAGE_EXTENSIONS}
    roots = []
    for root in [os.path.dirname(os.path.abspath(source)), *extra_roots]:
        if root and os.path.isdir(root) and os.path.normcase(root) not in {os.path.normcase(r) for r in roots}:
            roots.append(root)
    result["roots"] = roots
    candidates = {}
    for root in roots:
        found, scan = _index(root, wanted, max_depth, max_entries, max_seconds)
        result["scan"]["entries"] += scan["entries"]
        result["scan"]["seconds"] = round(result["scan"]["seconds"] + scan["seconds"], 3)
        result["scan"]["truncated"] = result["scan"]["truncated"] or scan["truncated"]
        for name, paths in found.items():
            known = {os.path.normcase(p) for p in candidates.get(name, [])}
            for path in paths:
                if os.path.normcase(path) not in known:
                    candidates.setdefault(name, []).append(path)
                    known.add(os.path.normcase(path))
    for image, expected in missing:
        name = os.path.basename(expected).lower()
        options = candidates.get(name, [])
        chosen = None
        if len(options) == 1:
            chosen = options[0]
        elif len(options) > 1:
            # Prefer the candidate that ends like the path the model expected (folder + file), e.g. textures\x.png
            matching = [p for p in options if _tail(p, 2) == _tail(expected, 2)]
            if len(matching) == 1:
                chosen = matching[0]
            else:
                result["ambiguous"].append({"name": image.name, "expected": expected, "candidates": options[:10]})
                continue
        if chosen is None:
            result["stillMissing"].append({"name": image.name, "path": expected})
            continue
        image.filepath = chosen
        try:
            image.reload()
        except RuntimeError:
            pass
        result["relinked"].append({"name": image.name, "from": expected, "to": chosen})
    return result
