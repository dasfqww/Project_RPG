"""Create the trusted existing raid mapping and bind only an empty GameMode catalog.

First run creates/validates the definition only. In a fresh process pass
-RPGDungeonBindServerDefinition after backing up BP_BossBattleGameMode.uasset.
No map/reward/AI graph or existing reward overrides are edited.
"""
import unreal

ROOT = "/Game/System/Dungeons"
NAME = "DA_DungeonServer_RaidSwordMan"
PATH = f"{ROOT}/{NAME}"
MODE = "/Game/Blueprints/GameMode/BP_BossBattleGameMode"
MAP = "/Game/Maps/bossmap.bossmap"
ID = "Dungeon.RaidSwordMan"


def main():
    assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    dirty = {package.get_name() for package in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    if PATH in dirty or MODE in dirty:
        raise RuntimeError("Target definition/GameMode has unsaved changes; refusing to save.")
    bp = assets.load_asset(MODE)
    if not isinstance(bp, unreal.Blueprint) or bp.generated_class() is None:
        raise RuntimeError("Existing raid GameMode unavailable.")
    default = unreal.get_default_object(bp.generated_class())
    rewards = {key: value for key, value in default.get_editor_property("dungeon_clear_rewards_by_difficulty").items()}
    expected = {getattr(unreal.RPGGameDifficulty, name) for name in ("EASY", "NORMAL", "HARD", "HELL")}
    if set(rewards) != expected or any(not isinstance(value, unreal.RPGDungeonRewardDefinition) for value in rewards.values()):
        raise RuntimeError("Existing difficulty reward overrides are incomplete; refusing to replace them.")
    current = list(default.get_editor_property("dungeon_server_definitions"))
    if current and (len(current) != 1 or current[0].get_path_name() != f"{PATH}.{NAME}"):
        raise RuntimeError("Existing server catalog differs; preserving it.")
    if assets.does_asset_exist(PATH):
        definition = assets.load_asset(PATH)
        if not isinstance(definition, unreal.RPGDungeonServerDefinition) or definition.get_editor_property("dungeon_id") != ID:
            raise RuntimeError("Existing server definition differs; preserving it.")
        # Reading a TSoftObjectPtr property via Python loads its world. Use native
        # metadata instead so nested map imports cannot hold the BP file during save.
        if definition.get_dungeon_map_package() != "/Game/Maps/bossmap":
            raise RuntimeError("Existing server definition map differs; preserving it.")
        if {key: value for key, value in definition.get_editor_property("clear_rewards_by_difficulty").items()} != rewards:
            raise RuntimeError("Existing server reward mapping differs; preserving it.")
        unreal.log("RPG_DUNGEON_SERVER_DEFINITION PRESERVED NoSave=1")
    else:
        world = unreal.load_object(None, MAP)
        if not isinstance(world, unreal.World):
            raise RuntimeError("Existing raid map unavailable.")
        definition = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            NAME, ROOT, unreal.RPGDungeonServerDefinition, unreal.DataAssetFactory())
        if definition is None:
            raise RuntimeError("Could not create trusted server definition.")
        definition.set_editor_property("dungeon_id", ID)
        definition.set_editor_property("dungeon_map", world)
        definition.set_editor_property("clear_rewards_by_difficulty", rewards)
        if not assets.save_asset(PATH, only_if_is_dirty=False):
            raise RuntimeError("Could not save trusted server definition.")
        unreal.log("RPG_DUNGEON_SERVER_DEFINITION CREATED ExistingRewardsPreserved=1 MapUnchanged=1")
        unreal.log("RPG_DUNGEON_SERVER_DEFINITION BIND_REQUIRED FreshProcess=1")
        return
    if "-rpgdungeonbindserverdefinition" not in unreal.SystemLibrary.get_command_line().lower():
        unreal.log("RPG_DUNGEON_SERVER_DEFINITION VALIDATED NoGameModeSave=1")
        return
    if not current:
        default.set_editor_property("dungeon_server_definitions", [definition])
        bp.modify()
        if not assets.save_asset(MODE, only_if_is_dirty=False):
            raise RuntimeError("Could not bind trusted definition to raid GameMode.")
        unreal.log("RPG_DUNGEON_SERVER_DEFINITION BOUND EmptyCatalogOnly=1 RewardOverridesUnchanged=1 GraphUnchanged=1")
    else:
        unreal.log("RPG_DUNGEON_SERVER_DEFINITION ALREADY_BOUND NoSave=1")


main()
