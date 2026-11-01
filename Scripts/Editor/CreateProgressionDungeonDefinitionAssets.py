"""Author first-slice lobby metadata without publishing unavailable content.

Read-only by default. Pass -RPGCreateProgressionDungeonDefinitions to create
missing assets. Existing definitions must match exactly and are never saved.
Maps, server/reward definitions, GameModes, Config and lobby lists are untouched.
"""

import hashlib
import json
import pathlib


ROOT = "/Game/System/Dungeons/Progression"
RECIPES = [
    ("Training", "Progression.Training", "기본 훈련", ["NORMAL"]),
    ("Intro1", "Progression.Intro1", "입문 1: 접근", ["NORMAL"]),
    ("Intro2", "Progression.Intro2", "입문 2: 공격 기회", ["NORMAL"]),
    ("Intro3", "Progression.Intro3", "입문 3: 무력화", ["NORMAL"]),
    ("FirstBoss", "Progression.FirstBoss", "첫 보스", ["NORMAL", "HARD"]),
]


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def inspect(unreal, asset, recipe):
    suffix, dungeon_id, label, difficulties = recipe
    require(isinstance(asset, unreal.RPGDungeonDefinition), "Wrong asset class: " + suffix)
    expected = {
        "dungeon_id": dungeon_id,
        "display_name": label,
        "difficulties": difficulties,
        "default_difficulty": "NORMAL",
    }
    actual = {
        "dungeon_id": asset.get_editor_property("dungeon_id"),
        "display_name": str(asset.get_editor_property("display_name")),
        "difficulties": [value.name for value in asset.get_editor_property("difficulties")],
        "default_difficulty": asset.get_editor_property("default_difficulty").name,
    }
    require(actual == expected, "Preserving customized definition: " + asset.get_path_name())
    result, errors, warnings = unreal.get_editor_subsystem(unreal.EditorValidatorSubsystem).is_object_valid(
        asset, unreal.DataValidationUsecase.SCRIPT)
    require(result == unreal.DataValidationResult.VALID and not errors and not warnings,
            "Definition validation failed: " + str((result, errors, warnings)))
    return {**actual, "validation": str(result), "errors": [], "warnings": []}


def main():
    import unreal

    flags = unreal.SystemLibrary.get_command_line().lower().split()
    apply = "-rpgcreateprogressiondungeondefinitions" in flags
    reload = "-rpgprogressiondungeonreload" in flags
    require(not (apply and reload), "Reload inspection cannot create content")
    require(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is None,
            "Author these definitions outside PIE")
    assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    dirty = {str(p.get_name()) for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    plans = []
    ids = {recipe[1] for recipe in RECIPES}
    require(len(ids) == len(RECIPES), "Duplicate recipe identity")
    targets = {recipe[1]: ROOT + "/DA_Dungeon_" + recipe[0] for recipe in RECIPES}
    # Inspect every existing target before creating or saving any missing asset.
    for recipe in RECIPES:
        package = targets[recipe[1]]
        disk = pathlib.Path(unreal.Paths.project_content_dir()) / (package[len("/Game/"):] + ".uasset")
        require(package not in dirty, "Preserving unsaved definition: " + package)
        exists = assets.does_asset_exist(package)
        require(exists == disk.exists(), "Asset registry/disk disagree: " + package)
        asset = assets.load_asset(package) if exists else None
        state = inspect(unreal, asset, recipe) if exists else None
        digest = hashlib.sha256(disk.read_bytes()).hexdigest() if exists else None
        plans.append((recipe, package, disk, asset, state, digest))
    # The established dungeon catalog may already own a planned ID elsewhere.
    for path in assets.list_assets("/Game/System/Dungeons", recursive=True, include_folder=False):
        data = unreal.EditorAssetLibrary.find_asset_data(path)
        if str(data.asset_class_path.asset_name) != "RPGDungeonDefinition":
            continue
        existing = assets.load_asset(path)
        dungeon_id = existing.get_editor_property("dungeon_id")
        if dungeon_id in targets:
            require(existing.get_path_name().split(".")[0] == targets[dungeon_id],
                    "Another definition already owns this ID: " + existing.get_path_name())
    results = []
    saved = []
    for recipe, package, disk, asset, state, digest in plans:
        if asset is None and apply:
            require(not disk.exists() and not assets.does_asset_exist(package),
                    "Target appeared during authoring: " + package)
            asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
                "DA_Dungeon_" + recipe[0], ROOT, unreal.RPGDungeonDefinition, unreal.DataAssetFactory())
            require(asset is not None, "Could not create definition: " + package)
            asset.set_editor_property("dungeon_id", recipe[1])
            asset.set_editor_property("display_name", unreal.Text(recipe[2]))
            asset.set_editor_property("difficulties", [getattr(unreal.RPGGameDifficulty, name) for name in recipe[3]])
            asset.set_editor_property("default_difficulty", unreal.RPGGameDifficulty.NORMAL)
            state = inspect(unreal, asset, recipe)
            require(not disk.exists(), "A disk asset appeared before save: " + package)
            require(assets.save_loaded_asset(asset, only_if_is_dirty=False), "Could not save: " + package)
            saved.append(package)
        if asset is not None:
            require(disk.exists(), "Saved definition missing from disk: " + package)
            state = inspect(unreal, asset, recipe)
            current_digest = hashlib.sha256(disk.read_bytes()).hexdigest()
            require(digest is None or digest == current_digest, "Existing asset was changed: " + package)
            require(package not in {str(p.get_name()) for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()},
                    "Definition remains dirty: " + package)
        else:
            current_digest = None
        results.append({"asset": package, "exists": asset is not None, "saved": package in saved,
                        "sha256": current_digest, "properties": state})
    report = {"read_only": not apply, "saved_count": len(saved), "assets": results,
              "missing_count": sum(not row["exists"] for row in results),
              "published_to_lobby": False, "server_maps_rewards_authored": False}
    output = pathlib.Path(unreal.Paths.project_saved_dir()) / "BuildChecks/ProgressionDungeonAssets-20261029"
    output.mkdir(parents=True, exist_ok=True)
    name = "Applied.json" if apply else ("Reload.json" if reload else "Inspect.json")
    (output / name).write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    unreal.log("RPG_PROGRESSION_DUNGEON_DEFINITIONS " + json.dumps(report, ensure_ascii=False))


if __name__ == "__main__":
    main()
