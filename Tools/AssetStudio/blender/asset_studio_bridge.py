"""Asset Studio v1: main-thread, authenticated loopback editing of a working copy."""
import hashlib
import hmac
import json
import math
import os
from pathlib import Path
import re
import secrets
import socket
import time
import uuid

import bpy
from bpy.app.handlers import persistent

MAX_FRAME = 1024 * 1024
ID_PATTERN = re.compile(r"^[A-Za-z0-9_-]{1,80}$")
FIELDS = {"protocol_version", "operation_id", "blender_session_id", "expected_scene_revision", "operation", "target_ids", "params"}
OPERATIONS = {"scene.inspect", "object.transform", "material.apply_profile", "checkpoint.save"}


class Rejected(Exception):
    def __init__(self, code, message):
        self.code = code
        super().__init__(message)


def atomic_json(path, value):
    temporary = path.with_suffix(path.suffix + ".tmp")
    with temporary.open("x", encoding="utf-8") as stream:
        json.dump(value, stream, ensure_ascii=False, allow_nan=False)
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, path)


def strict_pairs(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("Duplicate JSON field")
        result[key] = value
    return result


def vector(value, length, minimum=-1e6, maximum=1e6):
    return (isinstance(value, list) and len(value) == length and
            all(type(x) in (float, int) and math.isfinite(x) and minimum <= x <= maximum for x in value))


class Bridge:
    def __init__(self, root):
        self.root = Path(root)
        if self.root.is_symlink() or self.root.resolve() != self.root.absolute():
            raise Rejected("UNSAFE_DIRECTORY", "Session directory must be a real local directory.")
        self.root = self.root.resolve()
        self.session_id = str(uuid.uuid4())
        self.revision = 0
        self.busy = False
        self.blocked = False
        self.objects = {}
        self.receipts = {}
        self.clients = {}
        self.listener = None
        self.token = secrets.token_hex(32)
        self.stopped = False
        self.timer_callback = self.tick
        self.checkpoints = self.root / "checkpoints"
        self.journal = self.root / "operations"
        self.checkpoints.mkdir(exist_ok=False)
        self.journal.mkdir(exist_ok=False)
        working = self.root / "working.blend"
        if working.exists():
            raise Rejected("WORKING_COPY_EXISTS", "Use a fresh session directory.")
        result = bpy.ops.wm.save_as_mainfile(filepath=str(working), check_existing=False)
        if "FINISHED" not in result:
            raise Rejected("SAVE_FAILED", "Could not create working copy.")
        self.revision = 1

    def _safe_directories(self):
        for directory in (self.root, self.checkpoints, self.journal):
            if directory.is_symlink() or directory.resolve() != directory:
                raise Rejected("DIRECTORY_CHANGED", "Session directory was replaced by a link.")

    def _object_id(self, obj):
        pointer = obj.as_pointer()
        entry = self.objects.get(pointer)
        try:
            same = entry is not None and entry[0] == obj
        except ReferenceError:
            same = False
        if not same:
            self.objects[pointer] = (obj, str(uuid.uuid4()))
        return self.objects[pointer][1]

    def inspect(self):
        if len(bpy.context.scene.objects) > 2000:
            raise Rejected("SCENE_TOO_LARGE", "The first bridge supports up to 2000 scene objects.")
        objects = []
        for obj in bpy.context.scene.objects:
            objects.append({
                "id": self._object_id(obj), "name": obj.name, "type": obj.type,
                "location": list(obj.location), "scale": list(obj.scale),
                "selected": obj.select_get(),
                "materials": [slot.material.name if slot.material else None for slot in obj.material_slots],
                "vertex_count": len(obj.data.vertices) if obj.type == "MESH" else None,
                "bone_count": len(obj.data.bones) if obj.type == "ARMATURE" else None,
            })
        return {"blender_session_id": self.session_id, "scene_revision": self.revision,
                "objects": objects, "working_file": bpy.data.filepath,
                "mutation_blocked": self.blocked,
                "actions": [action.name for action in bpy.data.actions]}

    def _target(self, target_ids):
        if len(target_ids) != 1 or not isinstance(target_ids[0], str):
            raise Rejected("TARGET_REQUIRED", "Exactly one object ID is required.")
        for obj in bpy.context.scene.objects:
            if self._object_id(obj) == target_ids[0]:
                if obj.library or obj.override_library:
                    raise Rejected("LINKED_OBJECT", "Linked and overridden objects are not editable by this bridge.")
                return obj
        raise Rejected("TARGET_MISSING", "Object is no longer in this scene.")

    def _prepare(self, request):
        op, params, targets = request["operation"], request["params"], request["target_ids"]
        if op in ("scene.inspect", "checkpoint.save"):
            if params or targets:
                raise Rejected("INVALID_PARAMS", "This operation takes no parameters or targets.")
            return None
        obj = self._target(targets)
        if op == "object.transform":
            if obj.constraints or (obj.animation_data and (obj.animation_data.action or obj.animation_data.drivers)):
                raise Rejected("ANIMATED_TRANSFORM", "Animated or constrained transforms require an animation-aware operation.")
            if not params or set(params) - {"location", "scale"}:
                raise Rejected("INVALID_PARAMS", "Specify local location and/or scale.")
            for key, value in params.items():
                if not vector(value, 3) or (key == "scale" and any(x == 0 for x in value)):
                    raise Rejected("INVALID_PARAMS", "Expected three finite values; scale must be nonzero.")
            return obj
        if obj.type != "MESH":
            raise Rejected("MESH_REQUIRED", "Material editing requires a mesh.")
        if set(params) != {"slot", "base_color", "roughness", "metallic"}:
            raise Rejected("INVALID_PARAMS", "Expected slot, base_color, roughness and metallic.")
        if type(params["slot"]) is not int or not 0 <= params["slot"] <= max(0, len(obj.material_slots) - 1):
            raise Rejected("INVALID_PARAMS", "Material slot does not exist.")
        if not vector(params["base_color"], 4, 0, 1) or not vector([params["roughness"], params["metallic"]], 2, 0, 1):
            raise Rejected("INVALID_PARAMS", "Material values must be finite and within 0..1.")
        source = obj.material_slots[params["slot"]].material if obj.material_slots else None
        if source and source.node_tree:
            nodes = [node for node in source.node_tree.nodes if node.type == "BSDF_PRINCIPLED"]
            if len(nodes) != 1 or any(nodes[0].inputs[name].is_linked for name in ("Base Color", "Roughness", "Metallic")):
                raise Rejected("UNSUPPORTED_MATERIAL", "Use an unlinked, single Principled shader for this first bridge.")
        return obj

    def _apply(self, op, params, obj):
        if op == "object.transform":
            for key, value in params.items():
                setattr(obj, key, value)
        elif op == "material.apply_profile":
            source = obj.material_slots[params["slot"]].material if obj.material_slots else None
            material = source.copy() if source else bpy.data.materials.new("AssetStudioMaterial")
            material.use_nodes = True
            node = next(node for node in material.node_tree.nodes if node.type == "BSDF_PRINCIPLED")
            node.inputs["Base Color"].default_value = params["base_color"]
            node.inputs["Roughness"].default_value = params["roughness"]
            node.inputs["Metallic"].default_value = params["metallic"]
            material.diffuse_color = params["base_color"]
            material.roughness = params["roughness"]
            material.metallic = params["metallic"]
            if not obj.material_slots:
                if obj.data.users > 1 or obj.data.library:
                    obj.data = obj.data.copy()
                obj.data.materials.append(None)
            slot = obj.material_slots[params["slot"]]
            slot.link = "OBJECT"
            slot.material = material

    def _save_copy(self, name):
        self._safe_directories()
        destination = self.checkpoints / (name + ".blend")
        if destination.exists():
            raise Rejected("CHECKPOINT_EXISTS", "Checkpoint cannot be overwritten.")
        result = bpy.ops.wm.save_as_mainfile(filepath=str(destination), copy=True, check_existing=False)
        if "FINISHED" not in result or not destination.is_file():
            raise RuntimeError("Checkpoint save failed")
        digest = hashlib.sha256()
        with destination.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
        return {"path": str(destination.relative_to(self.root)).replace("\\", "/"),
                "sha256": digest.hexdigest(), "bytes": destination.stat().st_size}

    def handle(self, request):
        operation_id = request.get("operation_id") if isinstance(request, dict) else None
        response = {"protocol_version": 1, "operation_id": operation_id, "status": "rejected"}
        applied = False
        digest = None
        try:
            if not isinstance(request, dict) or set(request) != FIELDS or type(request["protocol_version"]) is not int or request["protocol_version"] != 1:
                raise Rejected("INVALID_REQUEST", "Expected a complete protocol version 1 envelope.")
            if not isinstance(operation_id, str) or not ID_PATTERN.fullmatch(operation_id):
                raise Rejected("INVALID_ID", "Invalid operation ID.")
            if request["operation"] not in OPERATIONS or not isinstance(request["params"], dict) or not isinstance(request["target_ids"], list):
                raise Rejected("INVALID_REQUEST", "Unsupported operation or parameters.")
            digest = hashlib.sha256(json.dumps(request, sort_keys=True, separators=(",", ":"), allow_nan=False).encode()).hexdigest()
            previous = self.receipts.get(operation_id)
            if previous:
                if previous["request_hash"] != digest:
                    raise Rejected("ID_REUSED", "Operation ID was already used with different content.")
                return previous["result"]
            op = request["operation"]
            if op != "scene.inspect":
                if request["blender_session_id"] != self.session_id:
                    raise Rejected("SESSION_CHANGED", "Inspect the current Blender session before editing.")
                if type(request["expected_scene_revision"]) is not int or request["expected_scene_revision"] != self.revision:
                    raise Rejected("SCENE_CHANGED", "Scene changed since the request was prepared.")
                if Path(bpy.data.filepath).resolve() != self.root / 'working.blend':
                    raise Rejected('WORKING_COPY_CHANGED', 'Open the asset in a fresh Asset Studio session before editing.')
                if self.blocked:
                    raise Rejected("NEEDS_RECONCILIATION", "An earlier operation has an unknown outcome. Inspect saved checkpoints before starting a fresh session.")
                if bpy.context.mode != "OBJECT" or any(screen.is_animation_playing for screen in bpy.data.screens) or bpy.app.is_job_running("RENDER"):
                    raise Rejected("BLENDER_BUSY", "Stop playback, rendering and edit mode before editing.")
                if len(self.receipts) >= 1000:
                    raise Rejected("SESSION_LIMIT", "Start a fresh working session after reviewing checkpoints.")
            target = self._prepare(request)
            if op == "scene.inspect":
                response.update(status="succeeded", result=self.inspect())
                return response
            self._safe_directories()
            prepared = self.journal / (operation_id + ".prepared.json")
            if prepared.exists() or (self.journal / (operation_id + ".json")).exists():
                raise Rejected("NEEDS_RECONCILIATION", "An operation record already exists without a matching in-memory receipt.")
            before_revision = self.revision
            atomic_json(prepared, {"request_hash": digest, "request": request, "status": "prepared"})
            self.busy = True
            applied = True  # From here, even a save failure is reconciled rather than retried blindly.
            artifacts = []
            if op != "checkpoint.save":
                artifacts.append(self._save_copy("before-" + operation_id))
                self._apply(op, request["params"], target)
                bpy.context.view_layer.update()
                self.revision += 1
            artifacts.append(self._save_copy(operation_id))
            response.update(status="succeeded", result={"blender_session_id": self.session_id,
                "before_scene_revision": before_revision, "scene_revision": self.revision,
                "artifacts": artifacts, "effect_state": "applied"})
            record = {"request_hash": digest, "result": response}
            atomic_json(self.journal / (operation_id + ".json"), record)
            self.receipts[operation_id] = record
            return response
        except Rejected as error:
            response["error"] = {"code": error.code, "message": str(error), "effect_state": "not_applied"}
        except Exception:
            response["error"] = {"code": "EXECUTION_UNCONFIRMED", "message": "Execution or checkpoint persistence failed; inspect the working copy and operation journal.", "effect_state": "unknown" if applied else "not_applied"}
        finally:
            self.busy = False
        if applied:
            self.blocked = True
            response["status"] = "unknown"
            response["error"]["effect_state"] = "unknown"
            self.receipts[operation_id] = {"request_hash": digest, "result": response}
        return response

    @persistent
    def changed(self, *_args):
        if not self.busy:
            self.revision += 1

    @persistent
    def reloaded(self, *_args):
        self.session_id = str(uuid.uuid4())
        self.revision += 1
        self.objects.clear()
        # Existing receipts remain as evidence. A load/undo does not replay old edits.

    def start(self):
        self.listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen(8)
        self.listener.setblocking(False)
        bpy.app.handlers.depsgraph_update_post.append(self.changed)
        for handlers in (bpy.app.handlers.load_post, bpy.app.handlers.undo_post, bpy.app.handlers.redo_post):
            handlers.append(self.reloaded)
        bpy.app.timers.register(self.timer_callback, first_interval=0.05, persistent=True)
        atomic_json(self.root / "connection.json", {"protocol_version": 1, "host": "127.0.0.1",
            "port": self.listener.getsockname()[1], "token": self.token, "pid": os.getpid()})

    def _dispatch(self, raw):
        operation_id = None
        try:
            envelope = json.loads(raw, object_pairs_hook=strict_pairs, parse_constant=lambda value: (_ for _ in ()).throw(ValueError(value)))
            if isinstance(envelope, dict) and isinstance(envelope.get("request"), dict):
                operation_id = envelope["request"].get("operation_id")
            if (not isinstance(envelope, dict) or set(envelope) != {"token", "request"} or
                    not isinstance(envelope["token"], str) or not hmac.compare_digest(envelope["token"], self.token)):
                raise Rejected("UNAUTHORIZED", "Invalid bridge credentials.")
            result = self.handle(envelope["request"])
        except Exception:
            result = {"protocol_version": 1, "operation_id": operation_id, "status": "rejected",
                      "error": {"code": "INVALID_FRAME", "message": "Invalid or unauthenticated request.", "effect_state": "not_applied"}}
        wire = (json.dumps(result, ensure_ascii=False, allow_nan=False) + "\n").encode()
        if len(wire) > MAX_FRAME:
            wire = (json.dumps({"protocol_version": 1, "operation_id": operation_id, "status": "rejected",
                "error": {"code": "RESULT_TOO_LARGE", "message": "Scene response exceeds bridge limit.", "effect_state": "not_applied"}}) + "\n").encode()
        return wire

    def tick(self):
        if self.stopped:
            return None
        for _ in range(4):
            try:
                connection, _address = self.listener.accept()
                connection.setblocking(False)
                if len(self.clients) >= 8:
                    connection.close()
                else:
                    self.clients[connection] = {"input": bytearray(), "output": None, "deadline": time.monotonic() + 30}
            except BlockingIOError:
                break
        for connection, state in list(self.clients.items()):
            try:
                if time.monotonic() > state["deadline"]:
                    raise ConnectionError("Client timeout")
                if state["output"] is None:
                    chunk = connection.recv(65536)
                    if not chunk:
                        raise ConnectionError("Client closed")
                    state["input"].extend(chunk)
                    if len(state["input"]) > MAX_FRAME:
                        raise ConnectionError("Frame too large")
                    if b"\n" in state["input"]:
                        raw, trailing = state["input"].split(b"\n", 1)
                        if trailing.strip():
                            raise ConnectionError("One request per connection")
                        state["output"] = self._dispatch(raw)
                if state["output"] is not None:
                    sent = connection.send(state["output"])
                    state["output"] = state["output"][sent:]
                    if not state["output"]:
                        raise ConnectionError("Complete")
            except BlockingIOError:
                pass
            except (ConnectionError, OSError):
                connection.close()
                self.clients.pop(connection, None)
        return 0.05

    def stop(self):
        self.stopped = True
        if bpy.app.timers.is_registered(self.timer_callback):
            bpy.app.timers.unregister(self.timer_callback)
        if self.changed in bpy.app.handlers.depsgraph_update_post:
            bpy.app.handlers.depsgraph_update_post.remove(self.changed)
        for handlers in (bpy.app.handlers.load_post, bpy.app.handlers.undo_post, bpy.app.handlers.redo_post):
            if self.reloaded in handlers:
                handlers.remove(self.reloaded)
        for connection in self.clients:
            connection.close()
        self.clients.clear()
        if self.listener:
            self.listener.close()


if __name__ == "__main__":
    session_dir = os.environ.get("ASSET_STUDIO_SESSION_DIR")
    if not session_dir:
        raise RuntimeError("Start this bridge through Asset Studio with an explicit session directory.")
    try:
        asset_studio_bridge = Bridge(session_dir)
        asset_studio_bridge.start()
    except Exception as error:
        atomic_json(Path(session_dir) / "startup-error.json", {"code": getattr(error, "code", "BRIDGE_START_FAILED")})
        raise
