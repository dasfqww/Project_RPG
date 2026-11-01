"""Create missing FirstSlice materials and intro rewards; read-only by default.

Load Config/Tags/ProgressionMaterials.ini in the authoring project first.
Use -RPGCreateProgressionMaterialAssets to author missing targets only.
Existing/dirty/customized targets are never overwritten. Does not publish
content, grant items/XP, implement crafting, or install an export package.
"""
import hashlib
import json
import os
from pathlib import Path


ITEM_ROOT = "/Game/Blueprints/GameData/Item/Progression"
REWARD_ROOT = "/Game/Blueprints/GameData/DungeonReward/Progression"
MATERIALS = {
    ITEM_ROOT + "/DA_Item_ProgressionOre": ("GameItem.Material.Progression.Ore", "성장 광석", "첫 성장 구간의 장비 강화 재료."),
    ITEM_ROOT + "/DA_Item_ProgressionToken": ("GameItem.Material.Progression.Token", "제작 증표", "첫 성장 구간의 목표 장비 선택 제작 재료."),
}
ORE = next(iter(MATERIALS))
REWARDS = {REWARD_ROOT + "/DA_DungeonReward_Intro" + str(i): "Progression.Intro" + str(i) for i in range(1, 4)}
TARGETS = list(MATERIALS) + list(REWARDS)
MARKER = "FirstSlice.Materials.v1"
VERSION = "FirstSlice.v1"
MAX_STACK = 999


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else None


def disk(unreal, package):
    return Path(unreal.Paths.project_content_dir()) / (package[len("/Game/"):] + ".uasset")


def dirty_packages(unreal):
    return {str(p.get_name()) for p in list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
            + list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())}


def gameplay_tag(unreal, name):
    tag = unreal.GameplayTag()
    imported = tag.import_text('(TagName="' + name + '")')
    require(imported and name in tag.export_text(),
            "Load the ProgressionMaterials tag config before authoring: " + name + "; imported=" + str(imported) + "; value=" + tag.export_text())
    return tag


def validate(unreal, asset):
    state, errors, warnings = unreal.get_editor_subsystem(unreal.EditorValidatorSubsystem).is_object_valid(
        asset, unreal.DataValidationUsecase.SCRIPT)
    require(state != unreal.DataValidationResult.INVALID and not errors and not warnings,
            "Native validation rejected " + asset.get_path_name() + ": " + str((state, errors, warnings)))
    # Older compatible modules return NotValidated after successful native checks.
    # Record the exact return; never relabel it Valid or claim current-source execution.
    return {"result": str(state), "errors": [str(v) for v in errors], "warnings": [str(v) for v in warnings]}


def inspect_item(unreal, asset, package):
    tag, name, description = MATERIALS[package]
    require(isinstance(asset, unreal.RPGItemDefinition), "Preserving another item class: " + package)
    require(unreal.EditorAssetLibrary.get_metadata_tag(asset, "RPGProgressionMaterialSpec") == MARKER,
            "Preserving an item authored elsewhere: " + package)
    require(asset.get_editor_property("item_tag").export_text() == gameplay_tag(unreal, tag).export_text(), "Item tag differs")
    require(asset.get_editor_property("definition_version") == 1 and asset.get_editor_property("max_stack_size") == MAX_STACK,
            "Definition version or stack limit differs")
    require(asset.get_editor_property("item_category") == unreal.ItemCategory.CRAFT, "Material category differs")
    require(str(asset.get_editor_property("display_name")) == name and str(asset.get_editor_property("description")) == description,
            "Material presentation differs")
    size = asset.get_editor_property("grid_size")
    require(size.x == size.y == 1 and not asset.get_editor_property("fragments"), "Material grid/fragments differ")
    require(not asset.get_editor_property("item_traits").gameplay_tags, "Preserving authored material traits")
    identity = unreal.SystemLibrary.conv_primary_asset_id_to_string(unreal.SystemLibrary.get_primary_asset_id_from_object(asset))
    expected_identity = "RPGItemDefinition:" + tag
    # The public query only sees Asset Manager registrations; a new unsaved
    # asset is not registered yet. Require the actual IDs on fresh reload.
    require(not identity or identity == expected_identity, "Stable identity differs: " + identity)
    return {"expected_primary_asset_id": expected_identity, "definition_version": 1, "category": "Craft", "max_stack_size": MAX_STACK,
            "grid_size": [1, 1], "display_name": name, "description": description, "fragment_count": 0,
            "validation": validate(unreal, asset)}


def inspect_reward(unreal, asset, package, ore):
    require(isinstance(asset, unreal.RPGDungeonRewardDefinition), "Preserving another reward class")
    require(unreal.EditorAssetLibrary.get_metadata_tag(asset, "RPGProgressionMaterialSpec") == MARKER
            and unreal.EditorAssetLibrary.get_metadata_tag(asset, "RPGIntendedDungeonId") == REWARDS[package],
            "Preserving another reward owner or intended content")
    require(str(asset.get_editor_property("reward_version")) == VERSION and not asset.get_editor_property("currency_changes"),
            "Reward version/currencies differ")
    entries = list(asset.get_editor_property("item_rewards"))
    require(len(entries) == 1, "Intro reward must contain only ore")
    entry = entries[0]
    require(entry.get_editor_property("item_definition") == ore and entry.get_editor_property("quantity") == 2
            and entry.get_editor_property("bind_state") == unreal.RPGItemBindState.CHARACTER_BOUND, "Intro item reward differs")
    require(not entry.get_editor_property("instance_tags").gameplay_tags and not entry.get_editor_property("stat_values"),
            "Intro reward must not add rolled metadata")
    require(entry.get_editor_property("durability").export_text() == unreal.RPGItemDurability().export_text(),
            "Material durability differs")
    return {"intended_dungeon_id": REWARDS[package], "reward_version": VERSION, "currency_count": 0,
            "ore_quantity": 2, "token_quantity": 0, "bind_state": "CharacterBound", "random_equipment_count": 0,
            "validation": validate(unreal, asset)}


def main():
    import unreal
    flags = unreal.SystemLibrary.get_command_line().lower().split()
    apply = "-rpgcreateprogressionmaterialassets" in flags
    reload = "-rpgprogressionmaterialassetsreload" in flags
    require(not (apply and reload), "Reload must stay read-only")
    output = os.environ.get("RPG_PROGRESSION_MATERIAL_REPORT")
    require(output and Path(output).is_absolute(), "Set an absolute RPG_PROGRESSION_MATERIAL_REPORT")
    require(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is None, "Author outside PIE")
    editor = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    require(not dirty_packages(unreal).intersection(TARGETS), "Preserving unsaved target packages")
    before = {}
    for package in TARGETS:
        path = disk(unreal, package)
        require(Path(unreal.Paths.project_content_dir()).resolve() in path.resolve().parents,
                "Target escapes the authoring Content directory through a link: " + package)
        require(editor.does_asset_exist(package) == path.exists(), "Registry/disk mismatch: " + package)
        before[package] = digest(path)
    tags = {package: gameplay_tag(unreal, spec[0]) for package, spec in MATERIALS.items()}
    assets = {p: editor.load_asset(p) if before[p] else None for p in TARGETS}
    states = {}
    # Check every existing target before the first mutation.
    for package in MATERIALS:
        if assets[package]: states[package] = inspect_item(unreal, assets[package], package)
    for package in REWARDS:
        if assets[package]:
            require(assets[ORE], "Existing reward has no authored ore dependency")
            states[package] = inspect_reward(unreal, assets[package], package, assets[ORE])
    saved = []
    if apply:
        for package in TARGETS:
            if assets[package]: continue
            require(not disk(unreal, package).exists() and not editor.does_asset_exist(package), "Target appeared: " + package)
            cls = unreal.RPGItemDefinition if package in MATERIALS else unreal.RPGDungeonRewardDefinition
            asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
                package.rsplit("/", 1)[1], package.rsplit("/", 1)[0], cls, unreal.DataAssetFactory())
            require(asset is not None, "Could not create " + package)
            unreal.EditorAssetLibrary.set_metadata_tag(asset, "RPGProgressionMaterialSpec", MARKER)
            if package in MATERIALS:
                _, name, description = MATERIALS[package]
                for key, value in {"item_tag": tags[package], "definition_version": 1, "item_category": unreal.ItemCategory.CRAFT,
                                   "display_name": unreal.Text(name), "description": unreal.Text(description),
                                   "max_stack_size": MAX_STACK, "grid_size": unreal.IntPoint(1, 1), "fragments": []}.items():
                    asset.set_editor_property(key, value)
                states[package] = inspect_item(unreal, asset, package)
            else:
                unreal.EditorAssetLibrary.set_metadata_tag(asset, "RPGIntendedDungeonId", REWARDS[package])
                asset.set_editor_property("reward_version", unreal.Name(VERSION))
                asset.set_editor_property("currency_changes", [])
                asset.set_editor_property("item_rewards", [unreal.RPGDungeonItemRewardEntry(
                    item_definition=assets[ORE], quantity=2, bind_state=unreal.RPGItemBindState.CHARACTER_BOUND)])
                states[package] = inspect_reward(unreal, asset, package, assets[ORE])
            require(not disk(unreal, package).exists(), "Disk target appeared before save")
            require(editor.save_loaded_asset(asset, only_if_is_dirty=False), "Save failed: " + package)
            assets[package] = asset
            saved.append(package)
    rows = []
    current_dirty = dirty_packages(unreal)
    for package in TARGETS:
        current = digest(disk(unreal, package))
        require(before[package] is None or before[package] == current, "Existing target changed: " + package)
        require(package not in current_dirty, "Target remains dirty: " + package)
        rows.append({"asset": package, "exists": current is not None, "saved": package in saved,
                     "sha256": current, "dirty": False, "properties": states.get(package)})
    report = {"read_only": not apply, "saved_count": len(saved), "missing_count": sum(not r["exists"] for r in rows),
              "assets": rows, "tag_config_required": "Config/Tags/ProgressionMaterials.ini",
              "content_published": False, "gameplay_or_settlement_executed": False}
    registered = {package: unreal.SystemLibrary.conv_primary_asset_id_to_string(
        unreal.SystemLibrary.get_primary_asset_id_from_object(assets[package])) for package in MATERIALS if assets[package]}
    if reload:
        for package, identity in registered.items():
            require(identity == "RPGItemDefinition:" + MATERIALS[package][0], "Reloaded primary ID is not registered: " + package)
    report["registered_primary_asset_ids"] = registered
    Path(output).parent.mkdir(parents=True, exist_ok=True)
    Path(output).write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    unreal.log("RPG_PROGRESSION_MATERIAL_ASSETS " + json.dumps(report, ensure_ascii=False))


if __name__ == "__main__":
    main()
