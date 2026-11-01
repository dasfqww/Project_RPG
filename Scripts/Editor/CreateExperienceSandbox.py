"""Create isolated Experience lifecycle fixtures; preserve existing assets.

Run with UnrealEditor-Cmd -run=PythonScript -Script=<this file>.
Only creates the sandbox GameMode and three maps under Experiences/Examples.
"""
import json
import unreal

ROOT = "/Game/System/Experiences/Examples"
MODE_PATH = ROOT + "/BP_ExperienceSandboxGameMode"
MODE_OBJECT = MODE_PATH + ".BP_ExperienceSandboxGameMode"
EXPERIENCE_PATH = "/Game/System/Experiences/DA_ClientPresentationAndOptionsHint"
MAP_ROOT = ROOT + "/Maps"


def check(condition, message):
    if not condition:
        raise RuntimeError(message)


def load_class(path):
    result = unreal.load_class(None, path)
    check(result is not None, "Missing class: " + path)
    return result


def prepare_mode():
    existing = unreal.EditorAssetLibrary.does_asset_exist(MODE_PATH)
    if existing:
        blueprint = unreal.load_asset(MODE_PATH)
    else:
        factory = unreal.BlueprintFactory()
        factory.set_editor_property("parent_class", unreal.GameModeBase)
        blueprint = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "BP_ExperienceSandboxGameMode", ROOT, unreal.Blueprint, factory)
    check(blueprint is not None, "Could not create/load sandbox GameMode")
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    check(blueprint.get_editor_property("status") == unreal.BlueprintStatus.BS_UP_TO_DATE,
          "Sandbox GameMode must compile without warnings")
    defaults = unreal.get_default_object(blueprint.generated_class())
    expected = {
        "game_state_class": load_class("/Script/Project_RPG.RPGGameStateBase"),
        "player_controller_class": load_class("/Script/Engine.PlayerController"),
        "player_state_class": load_class("/Script/Engine.PlayerState"),
        "default_pawn_class": load_class("/Script/Engine.SpectatorPawn"),
        "hud_class": load_class("/Script/Engine.HUD"),
    }
    if existing:
        for name, value in expected.items():
            check(defaults.get_editor_property(name) == value,
                  "Preserved sandbox GameMode differs: " + name)
    else:
        for name, value in expected.items():
            defaults.set_editor_property(name, value)
        check(unreal.EditorAssetLibrary.save_loaded_asset(blueprint, False),
              "Could not save sandbox GameMode")
    return blueprint.generated_class()


def prepare_map(name, mode, selector_class, selector_mode, count, experience):
    path = MAP_ROOT + "/" + name
    existing = unreal.EditorAssetLibrary.does_asset_exist(path)
    world = (unreal.EditorLoadingAndSavingUtils.load_map(path) if existing
             else unreal.EditorLoadingAndSavingUtils.new_blank_map(False))
    check(world is not None, "Could not create/load fixture map: " + path)
    settings = world.get_world_settings()
    if existing:
        check(settings.get_editor_property("default_game_mode") == mode,
              "Preserved map has a different GameMode: " + path)
        selectors = list(unreal.GameplayStatics.get_all_actors_of_class(world, selector_class))
        check(len(selectors) == count, "Preserved map has a different selector count: " + path)
        for selector in selectors:
            check(selector.get_editor_property("enabled")
                  and selector.get_editor_property("selection_mode") == selector_mode,
                  "Preserved map has a different selector mode: " + path)
            if experience is not None:
                check(selector.get_editor_property("experience") == experience,
                      "Preserved map has a different Experience: " + path)
    else:
        settings.set_editor_property("default_game_mode", mode)
        actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        for index in range(count):
            selector = actor_subsystem.spawn_actor_from_class(
                selector_class, unreal.Vector(index * 100.0, 0.0, 0.0))
            check(selector is not None, "Could not create selector: " + path)
            selector.set_actor_label("ExperienceSelection_" + str(index))
            selector.set_editor_property("selection_mode", selector_mode)
            if experience is not None:
                selector.set_editor_property("experience", experience)
        check(unreal.EditorLoadingAndSavingUtils.save_map(world, path),
              "Could not save fixture map: " + path)
    return {"map": path, "selectors": count, "preserved": existing}


def main():
    mode = prepare_mode()
    selector_class = load_class("/Script/Project_RPG.RPGExperienceSelectionActor")
    experience = unreal.load_asset(EXPERIENCE_PATH)
    check(experience is not None, "Missing composed Experience")
    selection_modes = unreal.RPGExperienceSelectionMode
    maps = [
        prepare_map("L_ExperienceLoad", mode, selector_class,
                    selection_modes.LOAD_EXPERIENCE, 1, experience),
        prepare_map("L_ExperienceDisabled", mode, selector_class,
                    selection_modes.DISABLE_EXPERIENCE, 1, None),
        prepare_map("L_ExperienceDuplicate", mode, selector_class,
                    selection_modes.LOAD_EXPERIENCE, 2, experience),
    ]
    unreal.log("RPG_EXPERIENCE_SANDBOX ASSETS_READY " + json.dumps({
        "game_mode": mode.get_path_name(), "maps": maps}, sort_keys=True))


if __name__ == "__main__":
    main()
