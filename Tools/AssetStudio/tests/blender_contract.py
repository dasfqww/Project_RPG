"""Real bpy contract regression, run only in a fresh --background --factory-startup process."""
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import uuid

import bpy

args = sys.argv[sys.argv.index("--") + 1:]
root, module_path = Path(args[0]), Path(args[1])
spec = importlib.util.spec_from_file_location("asset_studio_bridge", module_path)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
passed = []


def check(name, condition):
    assert condition, name
    passed.append(name)


def request(bridge, operation, params=None, targets=None, operation_id=None):
    return {"protocol_version": 1, "operation_id": operation_id or str(uuid.uuid4()),
            "blender_session_id": bridge.session_id, "expected_scene_revision": bridge.revision,
            "operation": operation, "target_ids": targets or [], "params": params or {}}


cube = bpy.data.objects["Cube"]
material = bpy.data.materials.new("SharedOriginal")
material.use_nodes = True
cube.data.materials.clear()
cube.data.materials.append(material)
sibling = cube.copy()
sibling.data = cube.data
sibling.name = "AnimatedSibling"
bpy.context.collection.objects.link(sibling)
sibling.keyframe_insert(data_path="location", frame=1)
sibling.location.x = 2
sibling.keyframe_insert(data_path="location", frame=10)
bpy.context.scene.frame_set(1)
action = sibling.animation_data.action
source = root / "original.blend"
bpy.ops.wm.save_as_mainfile(filepath=str(source), check_existing=False)
source_hash = hashlib.sha256(source.read_bytes()).hexdigest()
working = root / "session"
working.mkdir()
bridge = module.Bridge(working)
bridge.start()
try:
    scene = bridge.handle(request(bridge, "scene.inspect"))["result"]
    cube_id = next(obj["id"] for obj in scene["objects"] if obj["name"] == cube.name)
    check("working-copy-preserves-source-path", Path(bpy.data.filepath) != source)
    check("shared-objects-have-distinct-identities", len({obj["id"] for obj in scene["objects"]}) == len(scene["objects"]))
    cube.name = "RenamedMonster"
    check("object-identity-survives-rename", bridge._object_id(cube) == cube_id)
    bpy.context.view_layer.update()
    stale = request(bridge, "object.transform", {"location": [4, 0, 0]}, [cube_id])
    cube.location.x = 1
    bpy.context.view_layer.update()
    check("manual-change-invalidates-old-request", bridge.handle(stale)["error"]["code"] == "SCENE_CHANGED")
    edit = request(bridge, "object.transform", {"location": [3, 4, 5]}, [cube_id])
    result = bridge.handle(edit)
    check("transform-applied", result["status"] == "succeeded" and list(cube.location) == [3, 4, 5])
    revision = bridge.revision
    snapshot_count = len(list(bridge.checkpoints.iterdir()))
    check("duplicate-operation-reuses-receipt", bridge.handle(edit) == result and bridge.revision == revision and len(list(bridge.checkpoints.iterdir())) == snapshot_count)
    collision = {**edit, "params": {"location": [9, 9, 9]}}
    check("same-id-different-content-rejected", bridge.handle(collision)["error"]["code"] == "ID_REUSED")
    sibling_id = bridge._object_id(sibling)
    animated_edit = request(bridge, "object.transform", {"location": [9, 9, 9]}, [sibling_id])
    check("animated-transform-requires-specialized-operation", bridge.handle(animated_edit)["error"]["code"] == "ANIMATED_TRANSFORM")
    old_material = list(material.diffuse_color)
    style = request(bridge, "material.apply_profile", {"slot": 0, "base_color": [0.2, 0.4, 0.1, 1], "roughness": 0.8, "metallic": 0}, [sibling_id])
    styled = bridge.handle(style)
    check("material-applied", styled["status"] == "succeeded" and abs(sibling.material_slots[0].material.roughness - 0.8) < 1e-5)
    check("shared-material-and-sibling-preserved", cube.material_slots[0].material == material and list(material.diffuse_color) == old_material)
    check("mesh-and-existing-animation-preserved", cube.data == sibling.data and sibling.animation_data.action == action and len(cube.data.vertices) == 8)
    texture = material.node_tree.nodes.new("ShaderNodeTexImage")
    principled = next(node for node in material.node_tree.nodes if node.type == "BSDF_PRINCIPLED")
    link = material.node_tree.links.new(texture.outputs["Color"], principled.inputs["Base Color"])
    textured = request(bridge, "material.apply_profile", {"slot": 0, "base_color": [1, 0, 0, 1], "roughness": 0.4, "metallic": 0}, [cube_id])
    check("textured-sockets-are-not-silently-overridden", bridge.handle(textured)["error"]["code"] == "UNSUPPORTED_MATERIAL")
    material.node_tree.links.remove(link)
    material.node_tree.nodes.remove(texture)
    bare = cube.copy()
    bare.data = cube.data.copy()
    bare.data.materials.clear()
    bpy.context.collection.objects.link(bare)
    bare_sibling = bare.copy()
    bpy.context.collection.objects.link(bare_sibling)
    bpy.context.view_layer.update()
    empty_style = request(bridge, "material.apply_profile", {"slot": 0, "base_color": [0.5, 0.5, 0.5, 1], "roughness": 0.6, "metallic": 0}, [bridge._object_id(bare)])
    check("adding-first-material-isolates-shared-mesh", bridge.handle(empty_style)["status"] == "succeeded" and bare.data != bare_sibling.data and len(bare_sibling.data.materials) == 0)
    invalid = request(bridge, "object.transform", {"location": [float("nan"), 0, 0]}, [cube_id])
    check("nonfinite-input-rejected", bridge.handle(invalid)["status"] == "rejected")
    traversal = request(bridge, "checkpoint.save", operation_id="../escape")
    check("path-like-id-rejected", bridge.handle(traversal)["error"]["code"] == "INVALID_ID")
    wrong = request(bridge, "object.transform", {"scale": [2, 2, 2]}, [cube_id])
    wrong["blender_session_id"] = "old-session"
    check("old-session-rejected", bridge.handle(wrong)["error"]["code"] == "SESSION_CHANGED")
    unsigned = json.dumps({"token": "0" * 64, "request": request(bridge, "scene.inspect")}).encode()
    check("wrong-token-rejected", json.loads(bridge._dispatch(unsigned))["status"] == "rejected")
    saved = styled["result"]["artifacts"][-1]
    path = working / saved["path"]
    check("snapshot-hash-matches-file", hashlib.sha256(path.read_bytes()).hexdigest() == saved["sha256"])
    check("original-file-byte-identical", hashlib.sha256(source.read_bytes()).hexdigest() == source_hash)
    # A failure after applying the operation must block further writes, not rerun it.
    original_save = bridge._save_copy
    calls = 0
    def fail_after_apply(name):
        global calls
        calls += 1
        if calls == 2:
            raise OSError("Injected checkpoint failure")
        return original_save(name)
    bridge._save_copy = fail_after_apply
    uncertain = request(bridge, "object.transform", {"location": [7, 8, 9]}, [cube_id])
    failure = bridge.handle(uncertain)
    check("applied-without-durable-result-is-unknown", failure["status"] == "unknown" and list(cube.location) == [7, 8, 9])
    check("unknown-operation-is-not-replayed", bridge.handle(uncertain) == failure and calls == 2)
    next_edit = request(bridge, "object.transform", {"location": [0, 0, 0]}, [cube_id])
    check("unknown-outcome-blocks-further-edits", bridge.handle(next_edit)["error"]["code"] == "NEEDS_RECONCILIATION")
finally:
    bridge.stop()

bpy.ops.wm.open_mainfile(filepath=str(path))
restored = bpy.data.objects["RenamedMonster"]
check("saved-blend-reopens-with-edits-and-animation", list(restored.location) == [3, 4, 5] and bpy.data.objects["AnimatedSibling"].animation_data.action is not None)
print("ASSET_STUDIO_RESULT " + json.dumps({"passed": len(passed), "checks": passed}))
