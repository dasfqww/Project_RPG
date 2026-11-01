"""Author FirstBoss Normal/Hard rewards; read-only unless explicitly requested.

-RPGCreateProgressionBossRewards creates missing targets only. Existing,
customized or dirty packages are never overwritten. Does not publish a dungeon,
change a GameMode/server definition or enable a mixed-version backend service.
Requires the personal-loot UE module and installed FirstSlice item definitions.
Set RPG_PERSONAL_LOOT_ASSET_REPORT to an absolute result path.
"""
import hashlib
import json
import os
from pathlib import Path

ROOT = "/Game/Blueprints/GameData/DungeonReward/Progression"
TARGETS = {ROOT + "/DA_DungeonReward_FirstBoss" + mode: mode for mode in ("Normal", "Hard")}
ITEM_ROOT = "/Game/Blueprints/GameData/Item/Progression/"
SPECS = (
    ("DA_Item_ProgressionOre", "GameItem.Material.Progression.Ore", 4, 999, 0),
    ("DA_Item_ProgressionToken", "GameItem.Material.Progression.Token", 2, 999, 0),
    ("DA_Item_Progression_EnduranceGreatSword", "GameItem.Equipment.Progression.Target.EnduranceGreatSword", 1, 1, 500),
    ("DA_Item_Progression_CirculationGreatSword", "GameItem.Equipment.Progression.Target.CirculationGreatSword", 1, 1, 500),
    ("DA_Item_Progression_TargetChest", "GameItem.Equipment.Progression.Target.Chest", 1, 1, 500),
    ("DA_Item_Progression_TargetGloves", "GameItem.Equipment.Progression.Target.Gloves", 1, 1, 500),
)
VERSION = "FirstSlice.v1"
MARKER = "FirstSlice.PersonalLoot.v1"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def disk(unreal, package):
    root = Path(unreal.Paths.project_content_dir()).resolve()
    path = root / (package[len("/Game/"):] + ".uasset")
    require(root in path.resolve().parents, "Target escapes Content through a link: " + package)
    return path


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else None


def validate(unreal, asset):
    result, errors, warnings = unreal.get_editor_subsystem(unreal.EditorValidatorSubsystem).is_object_valid(
        asset, unreal.DataValidationUsecase.SCRIPT)
    require(result == unreal.DataValidationResult.VALID and not errors and not warnings,
            "Native reward validation failed: " + str((result, errors, warnings)))
    return {"result": str(result), "errors": [], "warnings": []}


def inspect(unreal, asset, package, items):
    require(isinstance(asset, unreal.RPGDungeonRewardDefinition), "Preserving a different reward class")
    for key, value in {"RPGPersonalLootSpec": MARKER, "RPGIntendedDungeonId": "Progression.FirstBoss",
                       "RPGIntendedDifficulty": TARGETS[package]}.items():
        require(unreal.EditorAssetLibrary.get_metadata_tag(asset, key) == value,
                "Preserving another reward owner/intent: " + package)
    require(str(asset.get_editor_property("reward_version")) == VERSION
            and not asset.get_editor_property("currency_changes"), "Reward version/currencies differ")
    entries = list(asset.get_editor_property("item_rewards"))
    require(len(entries) == len(SPECS), "Reward must have two fixed materials and four exclusive choices")
    for entry, item, spec in zip(entries, items, SPECS):
        _, _, quantity, _, weight = spec
        require(entry.get_editor_property("item_definition") == item
                and entry.get_editor_property("quantity") == quantity
                and entry.get_editor_property("personal_loot_weight") == weight
                and entry.get_editor_property("bind_state") == unreal.RPGItemBindState.CHARACTER_BOUND
                and entry.get_editor_property("include_equipment_progression") == bool(weight)
                and entry.get_editor_property("equipment_progression_level") == 0,
                "Preserving customized reward entry: " + package)
        require(not entry.get_editor_property("instance_tags").gameplay_tags
                and not entry.get_editor_property("stat_values")
                and entry.get_editor_property("durability").export_text() == unreal.RPGItemDurability().export_text(),
                "Preserving customized reward metadata: " + package)
    return {"reward_version": VERSION, "ore": 4, "token": 2, "equipment_weight_total": 2000,
            "equipment_weights": [500] * 4, "equipment_level": 0, "validation": validate(unreal, asset)}


def main():
    import unreal
    flags = unreal.SystemLibrary.get_command_line().lower().split()
    apply = "-rpgcreateprogressionbossrewards" in flags
    reload = "-rpgprogressionbossrewardsreload" in flags
    require(not (apply and reload), "Reload inspection must remain read-only")
    output = os.environ.get("RPG_PERSONAL_LOOT_ASSET_REPORT")
    require(output and Path(output).is_absolute(), "Set absolute RPG_PERSONAL_LOOT_ASSET_REPORT")
    require(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is None, "Author outside PIE")
    editor = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    dirty = {str(p.get_name()) for p in list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
             + list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())}
    require(not dirty.intersection(TARGETS), "Preserving unsaved target rewards")
    items = []
    for name, tag, _, stack, _ in SPECS:
        item = editor.load_asset(ITEM_ROOT + name)
        require(isinstance(item, unreal.RPGItemDefinition), "Missing installed item: " + name)
        require(item.get_editor_property("item_tag").export_text() == '(TagName="' + tag + '")'
                and item.get_editor_property("definition_version") == 1
                and item.get_editor_property("max_stack_size") == stack, "Item identity/stack differs: " + name)
        items.append(item)
    # Preflight both targets before creating either. Reused assets are read-only.
    plans = []
    for package in TARGETS:
        path = disk(unreal, package)
        require(editor.does_asset_exist(package) == path.exists(), "Registry/disk mismatch: " + package)
        asset = editor.load_asset(package) if path.exists() else None
        state = inspect(unreal, asset, package, items) if asset else None
        plans.append((package, path, asset, state, digest(path)))
    results = []
    for package, path, asset, state, before in plans:
        saved = False
        if asset is None and apply:
            require(not path.exists() and not editor.does_asset_exist(package), "Target appeared: " + package)
            asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
                package.rsplit("/", 1)[1], ROOT, unreal.RPGDungeonRewardDefinition, unreal.DataAssetFactory())
            require(asset is not None, "Could not create reward: " + package)
            entries = [unreal.RPGDungeonItemRewardEntry(
                item_definition=item, quantity=spec[2], personal_loot_weight=spec[4],
                bind_state=unreal.RPGItemBindState.CHARACTER_BOUND,
                include_equipment_progression=bool(spec[4]), equipment_progression_level=0)
                for item, spec in zip(items, SPECS)]
            asset.set_editor_property("reward_version", unreal.Name(VERSION))
            asset.set_editor_property("currency_changes", [])
            asset.set_editor_property("item_rewards", entries)
            for key, value in {"RPGPersonalLootSpec": MARKER, "RPGIntendedDungeonId": "Progression.FirstBoss",
                               "RPGIntendedDifficulty": TARGETS[package]}.items():
                unreal.EditorAssetLibrary.set_metadata_tag(asset, key, value)
            state = inspect(unreal, asset, package, items)
            require(not path.exists(), "Disk target appeared before save")
            require(editor.save_loaded_asset(asset, only_if_is_dirty=False), "Could not save: " + package)
            saved = True
        if asset:
            state = inspect(unreal, asset, package, items)
            require(path.exists() and (before is None or digest(path) == before), "Existing target changed")
        results.append({"asset": package, "saved": saved, "exists": bool(asset), "sha256": digest(path), "state": state})
    require(not {str(p.get_name()) for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}.intersection(TARGETS),
            "A reward remains dirty")
    report = {"read_only": not apply, "saved_count": sum(r["saved"] for r in results), "assets": results,
              "published_to_lobby": False, "server_binding_changed": False, "backend_deployed": False}
    Path(output).parent.mkdir(parents=True, exist_ok=True)
    Path(output).write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    unreal.log("RPG_PERSONAL_LOOT_ASSETS " + json.dumps(report, ensure_ascii=False))


if __name__ == "__main__":
    main()
