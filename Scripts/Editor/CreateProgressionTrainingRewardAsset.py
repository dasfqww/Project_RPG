"""Create the first training's XP-only reward without publishing the dungeon.

Read-only by default. -RPGCreateProgressionTrainingReward creates only a missing
target. Existing/customized/dirty assets are never overwritten or saved.
Use a module containing the explicit FirstSlice.v1 empty-reward validation.
"""

import hashlib
import json
import os
import pathlib


ROOT = "/Game/Blueprints/GameData/DungeonReward"
NAME = "DA_DungeonReward_ProgressionTraining"
PACKAGE = ROOT + "/" + NAME
VERSION = "FirstSlice.v1"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def values(asset):
    return {
        "reward_version": str(asset.get_editor_property("reward_version")),
        "currency_count": len(asset.get_editor_property("currency_changes")),
        "item_count": len(asset.get_editor_property("item_rewards")),
    }


def validate(unreal, asset):
    result, errors, warnings = unreal.get_editor_subsystem(unreal.EditorValidatorSubsystem).is_object_valid(
        asset, unreal.DataValidationUsecase.SCRIPT)
    return {"result": str(result), "valid": result == unreal.DataValidationResult.VALID,
            "errors": [str(value) for value in errors], "warnings": [str(value) for value in warnings]}


def inspect(unreal, asset):
    require(isinstance(asset, unreal.RPGDungeonRewardDefinition), "Preserving target with a different class")
    actual = values(asset)
    require(actual == {"reward_version": VERSION, "currency_count": 0, "item_count": 0},
            "Preserving customized reward: " + asset.get_path_name())
    validation = validate(unreal, asset)
    require(validation["valid"] and not validation["errors"] and not validation["warnings"],
            "Training reward validation failed: " + str(validation))
    return {**actual, "validation": validation}


def main():
    import unreal

    flags = unreal.SystemLibrary.get_command_line().lower().split()
    apply = "-rpgcreateprogressiontrainingreward" in flags
    reload = "-rpgprogressiontrainingrewardreload" in flags
    require(not (apply and reload), "Reload inspection cannot author a reward")
    destination = os.environ.get("RPG_TRAINING_REWARD_REPORT")
    if destination:
        output = pathlib.Path(destination)
        require(output.is_absolute(), "Report destination must be absolute")
    else:
        output = pathlib.Path(unreal.Paths.project_saved_dir()) / "ContentChecks/ProgressionTrainingReward" / (
            "Applied.json" if apply else "Reload.json" if reload else "Inspect.json")
    require(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is None,
            "Author this reward outside PIE")
    assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    dirty = {str(package.get_name()) for package in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    require(PACKAGE not in dirty, "Preserving unsaved target reward")
    disk = pathlib.Path(unreal.Paths.project_content_dir()) / (PACKAGE[len("/Game/"):] + ".uasset")
    exists = assets.does_asset_exist(PACKAGE)
    require(exists == disk.exists(), "Target asset registry/disk disagree")
    asset = assets.load_asset(PACKAGE) if exists else None
    before = hashlib.sha256(disk.read_bytes()).hexdigest() if exists else None
    state = inspect(unreal, asset) if asset else None

    # EditDefaultsOnly values can be edited on assets, not on ordinary transient
    # instances. A newly created in-memory asset must pass native validation
    # before saving any bytes. A missing read-only target has no capability result.
    capability = state["validation"] if state else None
    supported = True if state else None

    saved = False
    if asset is None and apply:
        require(not disk.exists() and not assets.does_asset_exist(PACKAGE), "Target appeared during authoring")
        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            NAME, ROOT, unreal.RPGDungeonRewardDefinition, unreal.DataAssetFactory())
        require(asset is not None, "Could not create training reward")
        asset.set_editor_property("reward_version", unreal.Name(VERSION))
        asset.set_editor_property("currency_changes", [])
        asset.set_editor_property("item_rewards", [])
        state = inspect(unreal, asset)
        capability, supported = state["validation"], True
        require(not disk.exists(), "A disk target appeared before save")
        require(assets.save_loaded_asset(asset, only_if_is_dirty=False), "Training reward save failed")
        saved = True
    if asset:
        require(disk.exists(), "Saved reward missing from disk")
        state = inspect(unreal, asset)
        digest = hashlib.sha256(disk.read_bytes()).hexdigest()
        require(before is None or before == digest, "Existing reward bytes changed")
        target_dirty = PACKAGE in {
            str(package.get_name()) for package in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
        require(not target_dirty, "Training reward remains dirty")
    else:
        digest, target_dirty = None, False

    report = {
        "read_only": not apply, "reload": reload, "asset": PACKAGE,
        "supported": supported, "capability": capability, "exists": asset is not None,
        "saved_count": int(saved), "sha256": digest, "dirty": target_dirty,
        "properties": state, "published_to_lobby": False, "server_or_map_modified": False,
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    unreal.log("RPG_TRAINING_REWARD " + json.dumps(report, ensure_ascii=False))


if __name__ == "__main__":
    main()
