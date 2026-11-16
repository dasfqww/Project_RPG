"""Read-only UE asset -> disabled server Riding candidate.

Set RPG_RIDING_AUTHORING_SPEC and RPG_RIDING_EXPORT_DIR to absolute paths.
Never saves assets, sets hashes, activates/deploys Riding or overwrites outputs.
"""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re

_module = importlib.util.spec_from_file_location("rpg_riding_manifest", Path(__file__).with_name("RidingCatalogManifest.py"))
manifest = importlib.util.module_from_spec(_module)
_module.loader.exec_module(manifest)


def package(value):
    manifest.require(type(value) is str and re.fullmatch(r"/Game/(?:[A-Za-z0-9_]+/)*[A-Za-z0-9_]+", value), "Use a saved /Game package path")
    return value


def parse_spec(text):
    spec = manifest.strict_json(text)
    manifest.exact(spec, ("format", "policy", "mounts", "registrationItems"))
    manifest.require(spec["format"] == "ProjectRPG.Riding.Authoring/v1", "Unknown authoring format")
    package(spec["policy"])
    manifest.require(type(spec["mounts"]) is list and 1 <= len(spec["mounts"]) <= 512, "Mount list must contain 1..512 assets")
    manifest.require(type(spec["registrationItems"]) is list and len(spec["registrationItems"]) <= 512, "Registration list exceeds limit")
    for row in spec["mounts"]:
        manifest.exact(row, ("asset", "enabled"))
        package(row["asset"])
        manifest.require(type(row["enabled"]) is bool, "Enabled must be boolean")
    for row in spec["registrationItems"]:
        manifest.exact(row, ("asset", "mountId"))
        package(row["asset"])
        manifest.identifier(row["mountId"], lower=True)
    paths = [spec["policy"]] + [row["asset"] for row in spec["mounts"]] + [row["asset"] for row in spec["registrationItems"]]
    manifest.require(len(set(paths)) == len(paths), "Duplicate source asset")
    return spec


def json_text(value):
    return json.dumps(value, ensure_ascii=False, allow_nan=False, sort_keys=True, indent=2) + "\n"


def read_content_binding(unreal):
    settings_type = getattr(unreal, "RPGRidingContentSettings", None)
    manifest.require(settings_type is not None, "Load the current game module with RPGRidingContentSettings")
    settings = unreal.get_default_object(settings_type)
    manifest.require(settings is not None, "Project Riding content settings unavailable")

    def path(asset, expected):
        manifest.require(isinstance(asset, expected), "Project Riding content reference is empty, missing or has the wrong class")
        value = str(asset.get_path_name())
        source, separator, name = value.partition(".")
        package(source)
        manifest.require(separator and name == source.rsplit("/", 1)[-1], "Use a canonical saved Riding asset reference")
        return value

    # UE 5.8 PythonizeProperty calls LoadObjectPropertyValue for soft object
    # properties too. These are loaded metadata objects, not path wrapper structs.
    policy = path(settings.get_editor_property("policy"), unreal.RPGRidingPolicy)
    definitions = settings.get_editor_property("mount_definitions")
    manifest.require(1 <= len(definitions) <= 512, "Project mount definitions must contain 1..512 assets")
    mounts = [path(asset, unreal.RPGMountDefinition) for asset in definitions]
    manifest.require(len(set(mounts)) == len(mounts), "Project mount definitions contain duplicate asset references")
    return dict(policy=policy, mountDefinitions=sorted(mounts))


def check_content_binding(unreal, spec):
    binding = read_content_binding(unreal)
    def object_path(value):
        return value + "." + value.rsplit("/", 1)[-1]
    manifest.require(binding["policy"] == object_path(spec["policy"]), "Project policy differs from the authoring specification")
    manifest.require(binding["mountDefinitions"] == sorted(object_path(row["asset"]) for row in spec["mounts"]),
                     "Project mount definitions differ from the authoring specification")
    return binding


def write_bundle(output, files):
    output = Path(output)
    manifest.require(output.is_absolute(), "Absolute output directory required")
    for ancestor in [output] + list(output.parents):
        if ancestor.exists():
            info = ancestor.lstat()
            manifest.require(not ancestor.is_symlink() and not (getattr(info, "st_file_attributes", 0) & 0x400), "Redirected output ancestor")
    for name, text in files.items():
        manifest.require(name not in ("", ".", "..") and Path(name).name == name, "Output must be a filename")
        target = output / name
        manifest.require(not target.is_symlink() and (not target.exists() or not (getattr(target.lstat(), "st_file_attributes", 0) & 0x400)), "Redirected output file")
        manifest.require(not target.exists() or target.read_bytes() == text.encode("utf-8"), "Preserving different output: " + name)
    output.mkdir(parents=True, exist_ok=True)
    for name, text in files.items():
        target = output / name
        if not target.exists():
            with target.open("x", encoding="utf-8", newline="\n") as stream:
                stream.write(text)


def inspect_assets(unreal, spec):
    binding = check_content_binding(unreal, spec)
    editor = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    validator = unreal.get_editor_subsystem(unreal.EditorValidatorSubsystem)
    dirty = {str(p.get_name()) for p in list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()) + list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())}
    observations = []
    def load(path, expected):
        manifest.require(path not in dirty and editor.does_asset_exist(path), "Missing/dirty authored asset: " + path)
        content = Path(unreal.Paths.project_content_dir()).resolve()
        disk = content / (path[len("/Game/"):] + ".uasset")
        manifest.require(disk.is_file() and content in disk.resolve().parents, "Asset file escapes Content or is unsaved: " + path)
        before = hashlib.sha256(disk.read_bytes()).hexdigest()
        asset = editor.load_asset(path)
        manifest.require(isinstance(asset, expected), "Wrong asset class: " + path)
        manifest.require(str(asset.get_path_name()) == path + "." + path.rsplit("/", 1)[-1], "Use the current canonical asset path, not a redirector: " + path)
        observations.append(dict(package=path, file=str(disk), sha256=before))
        return asset
    def valid(asset, item=False):
        state, errors, warnings = validator.is_object_valid(asset, unreal.DataValidationUsecase.SCRIPT)
        # RPGItemDefinition keeps its superclass NotValidated result after its
        # native checks succeed. Preserve that result; never relabel it Valid.
        accepted = [unreal.DataValidationResult.VALID]
        if item:
            accepted.append(unreal.DataValidationResult.NOT_VALIDATED)
        manifest.require(state in accepted and not errors and not warnings,
                         "Native asset validation did not succeed: " + str(asset.get_path_name()) + ": " + str((state, errors, warnings)))
        return str(state)
    def identity(asset):
        value = str(unreal.SystemLibrary.conv_primary_asset_id_to_string(unreal.SystemLibrary.get_primary_asset_id_from_object(asset)))
        manifest.require(value.count(":") == 1, "PrimaryAsset registration/cook scan is required")
        return value.split(":", 1)
    policy = load(spec["policy"], unreal.RPGRidingPolicy)
    values = policy.get_editor_property("values")
    snake = {"rideSpeed": "ride_speed", "yawRate": "yaw_rate", "dashDistance": "dash_distance", "dashDuration": "dash_duration",
             "dashCooldown": "dash_cooldown", "combatLock": "combat_lock", "emoteDuration": "emote_duration", "emoteCooldown": "emote_cooldown"}
    data = dict(protocolVersion=values.get_editor_property("protocol_version"), catalogVersion=values.get_editor_property("catalog_version"),
                policy={name: values.get_editor_property(snake.get(name, name)) for name, *_ in manifest.POLICY}, mounts=[], registrationItems=[])
    for row in spec["mounts"]:
        asset = load(row["asset"], unreal.RPGMountDefinition)
        observations[-1]["validation"] = valid(asset)
        mount = str(asset.get_editor_property("mount_id"))
        manifest.require(identity(asset) == ["Mount", mount], "Mount PrimaryAsset identity mismatch")
        tags = []
        for tag in asset.get_editor_property("region_tags").get_editor_property("gameplay_tags"):
            match = re.fullmatch(r'\(TagName="([A-Za-z0-9_.]+)"\)', tag.export_text())
            manifest.require(match is not None, "Cannot read authored region tag")
            tags.append(match.group(1))
        data["mounts"].append(dict(mountId=mount, definitionVersion=asset.get_editor_property("definition_version"), enabled=row["enabled"], regionTags=tags))
    forbidden = (getattr(unreal, "RPGItemConsumableDefinitionFragment", None), getattr(unreal, "RPGItemEquipmentDefinitionFragment", None))
    manifest.require(all(forbidden), "Load the current game module with item fragment classes")
    for row in spec["registrationItems"]:
        asset = load(row["asset"], unreal.RPGItemDefinition)
        observations[-1]["validation"] = valid(asset, item=True)
        manifest.require(not any(isinstance(fragment, forbidden) for fragment in asset.get_editor_property("fragments")), "Registration item cannot also consume effects or equip")
        kind, name = identity(asset)
        data["registrationItems"].append(dict(definitionType=kind, definitionName=name, definitionVersion=asset.get_editor_property("definition_version"),
                                              mountId=row["mountId"], maximumStackSize=asset.get_editor_property("max_stack_size"), uses=1))
    result = manifest.compile_catalog(data)
    manifest.require(str(values.get_editor_property("gameplay_hash")) == result["gameplayHash"],
                     "Policy GameplayHash differs; set the authored hash explicitly and rerun. Expected: " + result["gameplayHash"])
    observations[0]["validation"] = valid(policy)
    for entry in observations:
        manifest.require(hashlib.sha256(Path(entry["file"]).read_bytes()).hexdigest() == entry["sha256"], "Source asset changed while reading")
    dirty_after = {str(p.get_name()) for p in list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()) + list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())}
    manifest.require(not dirty_after.intersection(entry["package"] for entry in observations), "Source became dirty while reading")
    manifest.require(read_content_binding(unreal) == binding, "Project content settings changed while reading")
    result["contentBinding"] = dict(scope="LoadedProjectDefaults", componentOverridesVerified=False, **binding)
    return result, observations


def main():
    import unreal
    source = Path(os.environ.get("RPG_RIDING_AUTHORING_SPEC", ""))
    output = Path(os.environ.get("RPG_RIDING_EXPORT_DIR", ""))
    manifest.require(source.is_absolute() and source.is_file() and output.is_absolute(), "Set absolute RPG_RIDING_AUTHORING_SPEC and RPG_RIDING_EXPORT_DIR")
    manifest.require(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is None, "Export outside PIE")
    text = source.read_text(encoding="utf-8-sig")
    result, observations = inspect_assets(unreal, parse_spec(text))
    manifest.require(source.read_text(encoding="utf-8-sig") == text, "Authoring specification changed while reading")
    report = dict(format=result["format"], gameplayHash=result["gameplayHash"], sources=observations, contentBinding=result["contentBinding"],
                  activation="Disabled candidate; no deployment, policy changes or grants")
    write_bundle(output, {"Riding.Candidate.json": json_text(result["configuration"]), "Gameplay.canonical.txt": result["canonical"], "Sources.json": json_text(report)})
    unreal.log("Riding candidate exported with activation disabled: " + str(output))


if __name__ == "__main__":
    main()
