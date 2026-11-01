"""Create only the existing SwordMan raid's lobby definition; preserve existing assets.

Run with UnrealEditor-Cmd -run=pythonscript -script=<this file>.
Does not save maps, GameModes, rewards, Config or unrelated dirty packages.
"""
import unreal


ROOT = "/Game/System/Dungeons"
NAME = "DA_Dungeon_RaidSwordMan"
PATH = f"{ROOT}/{NAME}"
DUNGEON_ID = "Dungeon.RaidSwordMan"


def validate(asset):
    if not isinstance(asset, unreal.RPGDungeonDefinition):
        raise RuntimeError("Default dungeon definition has the wrong class; preserving it.")
    if asset.get_editor_property("dungeon_id") != DUNGEON_ID:
        raise RuntimeError("Default dungeon definition has another ID; preserving it.")
    if not str(asset.get_editor_property("display_name")).strip():
        raise RuntimeError("Default dungeon definition has no display name; preserving it.")
    expected = {getattr(unreal.RPGGameDifficulty, value) for value in ("EASY", "NORMAL", "HARD", "HELL")}
    difficulties = list(asset.get_editor_property("difficulties"))
    if len(difficulties) != 4 or set(difficulties) != expected:
        raise RuntimeError("Existing definition differs from the legacy four difficulties; preserving it.")
    if asset.get_editor_property("default_difficulty") != unreal.RPGGameDifficulty.HARD:
        raise RuntimeError("Existing definition differs from the legacy Hard default; preserving it.")


def main():
    assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    if any(package.get_name() == PATH for package in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()):
        raise RuntimeError("Target definition has unsaved changes; refusing to save it.")
    if assets.does_asset_exist(PATH):
        asset = assets.load_asset(PATH)
        validate(asset)
        unreal.log(f"RPG_DUNGEON_DEFINITION PRESERVED Asset={PATH} Dungeon={DUNGEON_ID} NoSave=1")
        return
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        NAME, ROOT, unreal.RPGDungeonDefinition, unreal.DataAssetFactory())
    if asset is None:
        raise RuntimeError("Could not create the default dungeon definition.")
    asset.set_editor_property("dungeon_id", DUNGEON_ID)
    asset.set_editor_property("display_name", unreal.Text("검사 레이드"))
    asset.set_editor_property("difficulties", [getattr(unreal.RPGGameDifficulty, value)
        for value in ("EASY", "NORMAL", "HARD", "HELL")])
    asset.set_editor_property("default_difficulty", unreal.RPGGameDifficulty.HARD)
    validate(asset)
    if not assets.save_asset(PATH, only_if_is_dirty=False):
        raise RuntimeError("Could not save the new default dungeon definition.")
    unreal.log(f"RPG_DUNGEON_DEFINITION CREATED Asset={PATH} Dungeon={DUNGEON_ID} ExistingContentChanged=0")


main()
