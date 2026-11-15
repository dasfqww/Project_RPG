"""Exporter regressions and Python -> production C# contract fixtures.

The Unreal adapter uses a test double here; this does not certify real content.
All fixtures and output files live under the runner-owned --fixtures directory.
"""
import argparse
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import types
import unittest

_script = Path(__file__).resolve().parents[1] / "Editor" / "ExportRidingCatalog.py"
_loader = importlib.util.spec_from_file_location("export_riding", _script)
export = importlib.util.module_from_spec(_loader)
_loader.loader.exec_module(export)
compiler = export.manifest
ROOT = None


def authoring():
    return dict(protocolVersion=1, catalogVersion=17,
                policy=dict(rideSpeed=623.1234567, acceleration=2048, braking=2000, yawRate=500,
                            dashDistance=900, dashDuration=.6, dashCooldown=10, combatLock=10,
                            emoteDuration=3, emoteCooldown=2),
                mounts=[dict(mountId="wolf", definitionVersion=2, enabled=False, regionTags=["World.Z", "World.A"]),
                        dict(mountId="horse", definitionVersion=1, enabled=True, regionTags=[])],
                registrationItems=[dict(definitionType="RPGItemDefinition", definitionName="GameItem.Consume.Mount.Horse",
                                        definitionVersion=v, mountId="horse", maximumStackSize=99, uses=1) for v in (10, 2, 1)])


def spec():
    return dict(format="ProjectRPG.Riding.Authoring/v1", policy="/Game/Riding/Policy",
                mounts=[dict(asset="/Game/Riding/Horse", enabled=True)],
                registrationItems=[dict(asset="/Game/Riding/Item", mountId="horse")])


class Asset:
    def __init__(self, identity="", **properties):
        self.identity, self.properties = identity, properties
    def get_editor_property(self, name):
        return self.properties[name]
    def get_path_name(self):
        return self.identity


class FakeUE:
    """Small reflection/validation boundary, not a simulated Unreal engine."""
    class RPGRidingPolicy(Asset):
        pass
    class RPGMountDefinition(Asset):
        pass
    class RPGItemDefinition(Asset):
        pass
    class RPGItemConsumableDefinitionFragment:
        pass
    class RPGItemEquipmentDefinitionFragment:
        pass
    EditorAssetSubsystem, EditorValidatorSubsystem = "assets", "validation"
    DataValidationUsecase = types.SimpleNamespace(SCRIPT="script")
    DataValidationResult = types.SimpleNamespace(VALID="Valid", INVALID="Invalid", NOT_VALIDATED="NotValidated")

    def __init__(self, root):
        self.dirty, self.results, self.after = [], {}, None
        data = authoring()
        data["mounts"] = [data["mounts"][1]]
        data["registrationItems"] = [data["registrationItems"][-1]]
        values = {"protocol_version": 1, "catalog_version": data["catalogVersion"],
                  "gameplay_hash": compiler.compile_catalog(data)["gameplayHash"]}
        snake = dict(rideSpeed="ride_speed", yawRate="yaw_rate", dashDistance="dash_distance", dashDuration="dash_duration",
                     dashCooldown="dash_cooldown", combatLock="combat_lock", emoteDuration="emote_duration", emoteCooldown="emote_cooldown")
        values.update({snake.get(k, k): v for k, v in data["policy"].items()})
        self.assets = {"/Game/Riding/Policy": self.RPGRidingPolicy(values=Asset(**values)),
                       "/Game/Riding/Horse": self.RPGMountDefinition("Mount:horse", mount_id="horse", definition_version=1, region_tags=Asset(gameplay_tags=[])),
                       "/Game/Riding/Item": self.RPGItemDefinition("RPGItemDefinition:GameItem.Consume.Mount.Horse", definition_version=1, max_stack_size=99, fragments=[])}
        self.results[id(self.assets["/Game/Riding/Item"])] = ("NotValidated", [], [])
        self.Paths = types.SimpleNamespace(project_content_dir=lambda: str(root))
        self.EditorLoadingAndSavingUtils = types.SimpleNamespace(get_dirty_content_packages=lambda: [types.SimpleNamespace(get_name=lambda p=p: p) for p in self.dirty], get_dirty_map_packages=lambda: [])
        self.SystemLibrary = types.SimpleNamespace(get_primary_asset_id_from_object=lambda a: a.identity, conv_primary_asset_id_to_string=lambda value: value)
        for path in self.assets:
            target = root / (path[6:] + ".uasset")
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(b"fake saved asset")

    def get_editor_subsystem(self, _):
        return self
    def does_asset_exist(self, path):
        return path in self.assets
    def load_asset(self, path):
        return self.assets[path]
    def is_object_valid(self, asset, _):
        if self.after:
            self.after(asset)
        return self.results.get(id(asset), ("Valid", [], []))


class CompilerTests(unittest.TestCase):
    def test_deterministic_order_and_exact_float32(self):
        a = authoring()
        b = copy.deepcopy(a)
        b["mounts"].reverse()
        b["registrationItems"].reverse()
        b["mounts"][1]["regionTags"].reverse()
        result = compiler.compile_catalog(a)
        self.assertEqual(result, compiler.compile_catalog(b))
        bits = struct.unpack("<I", struct.pack("<f", a["policy"]["rideSpeed"]))[0]
        self.assertIn("rideSpeed=%08x\n" % bits, result["canonical"])
        self.assertEqual(hashlib.sha256(result["canonical"].encode()).hexdigest(), result["gameplayHash"])
        self.assertFalse(result["configuration"]["Riding"]["Enabled"])

    def test_strict_json(self):
        for text in ['{"a":1,"a":2}', '{"a":NaN}', '{"a":Infinity}', '{"a":-Infinity}']:
            with self.subTest(text=text), self.assertRaises(ValueError):
                compiler.strict_json(text)

    def test_invalid_policy_and_version(self):
        mutations = [("protocolVersion", True), ("protocolVersion", 2), ("catalogVersion", 0), ("catalogVersion", 2147483648)]
        for key, value in mutations:
            a = authoring()
            a[key] = value
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                compiler.compile_catalog(a)
        for name, _, low, high in compiler.POLICY:
            for bad in [True, None, "1", float("nan"), float("inf"), 10 ** 400, low / 2, high * 2]:
                a = authoring()
                a["policy"][name] = bad
                with self.subTest(name=name, bad=str(bad)[:20]), self.assertRaises(ValueError):
                    compiler.compile_catalog(a)
        a = authoring()
        a["policy"].update(dashDuration=2, dashCooldown=1)
        with self.assertRaises(ValueError):
            compiler.compile_catalog(a)

    def test_mount_constraints(self):
        for key, bad in [("mountId", "Horse"), ("mountId", "h|bad"), ("mountId", "h" * 65), ("enabled", 1),
                         ("definitionVersion", True), ("regionTags", ["A..B"]), ("regionTags", ["A", "A"]), ("regionTags", ["A"] * 65)]:
            a = authoring()
            a["mounts"][0][key] = bad
            with self.subTest(key=key, bad=bad), self.assertRaises(ValueError):
                compiler.compile_catalog(a)
        for mounts in [[], [authoring()["mounts"][0]] * 2, [authoring()["mounts"][0]] * 513]:
            a = authoring()
            a["mounts"] = mounts
            with self.assertRaises(ValueError):
                compiler.compile_catalog(a)

    def test_registration_constraints(self):
        for key, bad in [("definitionType", "bad|type"), ("definitionName", ""), ("definitionVersion", 0),
                         ("maximumStackSize", 0), ("uses", 3), ("uses", True), ("mountId", "missing"), ("mountId", [])]:
            a = authoring()
            a["registrationItems"][0][key] = bad
            with self.subTest(key=key, bad=bad), self.assertRaises(ValueError):
                compiler.compile_catalog(a)
        for count in (2, 513):
            a = authoring()
            a["registrationItems"] *= count
            with self.assertRaises(ValueError):
                compiler.compile_catalog(a)

    def test_exact_schemas(self):
        for target in ("root", "policy", "mount", "item"):
            a = authoring()
            value = {"root": a, "policy": a["policy"], "mount": a["mounts"][0], "item": a["registrationItems"][0]}[target]
            value["typo"] = 1
            with self.assertRaises(ValueError):
                compiler.compile_catalog(a)


class AdapterTests(unittest.TestCase):
    def setUp(self):
        self.root = ROOT / self._testMethodName
        self.ue = FakeUE(self.root)
    def test_spec(self):
        self.assertEqual(export.parse_spec(json.dumps(spec())), spec())
        for bad in ["/Other/Policy", "/Game/../Policy", "/Game/Riding/Policy.Policy", "/Game/Riding/Po licy"]:
            value = spec()
            value["policy"] = bad
            with self.assertRaises(ValueError):
                export.parse_spec(json.dumps(value))
        value = spec()
        value["mounts"].append(value["mounts"][0])
        with self.assertRaises(ValueError):
            export.parse_spec(json.dumps(value))
    def test_read_only_success_records_actual_validation(self):
        before = {p: p.read_bytes() for p in self.root.rglob("*.uasset")}
        result, observations = export.inspect_assets(self.ue, spec())
        self.assertEqual(result["gameplayHash"], self.ue.assets[spec()["policy"]].properties["values"].properties["gameplay_hash"])
        self.assertEqual([r["validation"] for r in observations], ["Valid", "Valid", "NotValidated"])
        self.assertEqual(before, {p: p.read_bytes() for p in before})
    def test_missing_dirty_wrong_class_and_identity(self):
        path = "/Game/Riding/Horse"
        original = self.ue.assets[path]
        for mode in ("missing", "dirty", "class", "identity"):
            self.ue.assets[path] = original
            self.ue.dirty = []
            original.identity = "Mount:horse"
            if mode == "missing":
                del self.ue.assets[path]
            elif mode == "dirty":
                self.ue.dirty = [path]
            elif mode == "class":
                self.ue.assets[path] = Asset()
            else:
                original.identity = "Mount:wrong"
            with self.subTest(mode=mode), self.assertRaises(ValueError):
                export.inspect_assets(self.ue, spec())
    def test_native_errors_warnings_and_not_validated_mount(self):
        asset = self.ue.assets["/Game/Riding/Horse"]
        for state in [("Invalid", [], []), ("Valid", ["bad socket"], []), ("Valid", [], ["warning"]), ("NotValidated", [], [])]:
            self.ue.results[id(asset)] = state
            with self.subTest(state=state), self.assertRaises(ValueError):
                export.inspect_assets(self.ue, spec())
    def test_conflicting_item_uses(self):
        item = self.ue.assets["/Game/Riding/Item"]
        for fragment in [self.ue.RPGItemConsumableDefinitionFragment(), self.ue.RPGItemEquipmentDefinitionFragment()]:
            item.properties["fragments"] = [fragment]
            with self.assertRaises(ValueError):
                export.inspect_assets(self.ue, spec())
    def test_hash_mismatch_and_source_changes(self):
        values = self.ue.assets[spec()["policy"]].properties["values"].properties
        original = values["gameplay_hash"]
        values["gameplay_hash"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "Expected:"):
            export.inspect_assets(self.ue, spec())
        self.assertEqual(values["gameplay_hash"], "0" * 64)
        values["gameplay_hash"] = original
        target = self.root / "Riding/Horse.uasset"
        self.ue.after = lambda _: target.write_bytes(b"changed by another author")
        with self.assertRaisesRegex(ValueError, "Source asset changed"):
            export.inspect_assets(self.ue, spec())
    def test_region_tags_and_became_dirty(self):
        tag = types.SimpleNamespace(export_text=lambda: '(TagName="World._A")')
        self.ue.assets["/Game/Riding/Horse"].properties["region_tags"].properties["gameplay_tags"] = [tag]
        data = authoring()
        data["mounts"] = [data["mounts"][1]]
        data["registrationItems"] = [data["registrationItems"][-1]]
        data["mounts"][0]["regionTags"] = ["World._A"]
        self.ue.assets[spec()["policy"]].properties["values"].properties["gameplay_hash"] = compiler.compile_catalog(data)["gameplayHash"]
        self.assertIn("World._A", export.inspect_assets(self.ue, spec())[0]["canonical"])
        self.ue.after = lambda _: self.ue.dirty.append("/Game/Riding/Horse")
        with self.assertRaisesRegex(ValueError, "Source became dirty"):
            export.inspect_assets(self.ue, spec())
    def test_preserve_output_and_idempotent_rerun(self):
        output = self.root / "Export"
        export.write_bundle(output, {"candidate.json": "unchanged\n"})
        export.write_bundle(output, {"candidate.json": "unchanged\n"})
        with self.assertRaises(ValueError):
            export.write_bundle(output, {"candidate.json": "different\n", "extra.json": "new"})
        self.assertEqual((output / "candidate.json").read_text(), "unchanged\n")
        self.assertFalse((output / "extra.json").exists())
        with self.assertRaises(ValueError):
            export.write_bundle(output, {"../escape": "bad"})


def fixtures(root):
    values = {"standard": authoring(), "minimum": authoring(), "maximum": authoring(), "capacity": authoring()}
    for name, field, low, high in compiler.POLICY:
        values["minimum"]["policy"][name] = low
        values["maximum"]["policy"][name] = high
    values["minimum"]["registrationItems"] = []
    values["maximum"]["catalogVersion"] = 2147483647
    values["capacity"]["mounts"] = [dict(mountId="m%03d" % i, definitionVersion=i + 1, enabled=True, regionTags=["World._A", "World.Z"]) for i in range(512)]
    values["capacity"]["registrationItems"] = [dict(definitionType="RPGItemDefinition", definitionName="Item.%03d" % i, definitionVersion=1, mountId="m%03d" % i, maximumStackSize=2147483647, uses=1) for i in range(512)]
    target = root / "Contract"
    target.mkdir()
    for name, data in values.items():
        (target / (name + ".json")).write_text(export.json_text(compiler.compile_catalog(data)), encoding="utf-8", newline="\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--fixtures", type=Path, required=True)
    args = parser.parse_args()
    ROOT = args.fixtures.resolve()
    ROOT.mkdir(parents=True, exist_ok=False)
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromModule(__import__(__name__)))
    if result.wasSuccessful():
        fixtures(ROOT)
    print(json.dumps(dict(Passed=result.wasSuccessful(), CaseCount=result.testsRun, Fixtures=4 if result.wasSuccessful() else 0)))
    raise SystemExit(0 if result.wasSuccessful() else 1)
