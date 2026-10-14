"""Versioned Character Forge conversion: Blender input -> one GLB plus a JSON report.

Run only through the application with --background --factory-startup --disable-autoexec:
  blender ... --python forge_bridge.py -- SOURCE OUTPUT.glb REPORT.json
The C++ caller validates the output with engines/gltf before adding it to a project.
"""

import json
import os
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
    os.replace(temporary, path)


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
    meshes = sum(obj.type == "MESH" for obj in bpy.data.objects)
    if not meshes:
        raise ValueError("The source has no mesh objects")
    exported = bpy.ops.export_scene.gltf(
        filepath=output,
        export_format="GLB",
        export_skins=True,
        export_morph=True,
        export_animations=True,
        export_yup=True,
    )
    if "FINISHED" not in exported or not os.path.isfile(output):
        raise RuntimeError("Blender did not produce a GLB")
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
    except Exception as error:
        result.update(status="error", message=str(error)[:1000])
        write_report(report, result)
        print("SNS_FORGE_BRIDGE_ERROR", flush=True)
        raise


if __name__ == "__main__":
    main()
