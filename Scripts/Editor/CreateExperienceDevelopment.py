"""Create the common client development Experience, HUD, GameMode and map.

Run in a separate UnrealEditor-Cmd process with -run=PythonScript.
Existing assets are checked and preserved; only missing development assets are saved.
"""
import json
import unreal

ROOT = "/Game/System/Experiences/Development"
EXPERIENCE_PATH = ROOT + "/DA_CommonClientDevelopment"
HUD_PATH = ROOT + "/BP_ExperienceDevelopmentHUD"
MODE_PATH = ROOT + "/BP_ExperienceDevelopmentGameMode"
CONTROLLER_PATH = ROOT + "/BP_ExperienceDevelopmentInputController"
INPUT_CONFIG_PATH = ROOT + "/DA_ExperienceDevelopmentInput"
INPUT_CONTEXT_PATH = ROOT + "/IMC_ExperienceDevelopment"
MAP_PATH = ROOT + "/Maps/L_ExperienceDevelopment"
OPTION_PATH = "/Game/Blueprints/UI/GameContents/Option/WBP_OptionMenu"
SET_SPECS = [
    ("/Game/System/Experiences/ActionSets/DA_ClientPresentation",
     ["RPGClientPresentation"], 1),
    ("/Game/System/Experiences/ActionSets/DA_OptionsShortcut",
     ["RPGInputMapping", "RPGOptionsUI"], 2),
    ("/Game/System/Experiences/ActionSets/DA_OptionsShortcutHint",
     ["RPGClientUI", "RPGOptionsUI"], 1),
]
FEATURES = ["RPGClientPresentation", "RPGInputMapping", "RPGOptionsUI", "RPGClientUI"]
CREATED = []
PRESERVED = []


def check(condition, message):
    if not condition:
        raise RuntimeError(message)


def load_class(path):
    value = unreal.load_class(None, path)
    check(value is not None, "Missing class: " + path)
    return value


def load_asset(path):
    value = unreal.load_asset(path)
    check(value is not None, "Missing asset: " + path)
    return value


def short_name(path):
    return path.rsplit("/", 1)[1]


def check_properties(obj, expected, context):
    for name, value in expected.items():
        actual = obj.get_editor_property(name)
        check(actual == value, context + " differs: " + name)


def save_new(asset, path):
    check(unreal.EditorAssetLibrary.save_loaded_asset(asset, False),
          "Could not save new asset: " + path)
    CREATED.append(path)


def prepare_blueprint(path, parent, expected):
    exists = unreal.EditorAssetLibrary.does_asset_exist(path)
    if exists:
        blueprint = load_asset(path)
        check(isinstance(blueprint, unreal.Blueprint), "Expected Blueprint: " + path)
        check(unreal.BlueprintEditorLibrary.get_blueprint_parent_class(blueprint) == parent,
              "Preserved Blueprint has a different parent: " + path)
    else:
        factory = unreal.BlueprintFactory()
        factory.set_editor_property("parent_class", parent)
        blueprint = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            short_name(path), ROOT, unreal.Blueprint, factory)
        check(blueprint is not None, "Could not create Blueprint: " + path)
        unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    check(blueprint.get_editor_property("status") == unreal.BlueprintStatus.BS_UP_TO_DATE,
          "Development Blueprint must compile without warnings: " + path)
    generated = blueprint.generated_class()
    check(generated is not None, "Missing generated class: " + path)
    defaults = unreal.get_default_object(generated)
    if exists:
        check_properties(defaults, expected, "Preserved " + path)
        PRESERVED.append(path)
    else:
        for name, value in expected.items():
            defaults.set_editor_property(name, value)
        check_properties(defaults, expected, "New " + path)
        save_new(blueprint, path)
    return generated


def prepare_experience(definition_class, action_sets):
    exists = unreal.EditorAssetLibrary.does_asset_exist(EXPERIENCE_PATH)
    if exists:
        experience = load_asset(EXPERIENCE_PATH)
    else:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", definition_class)
        experience = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            short_name(EXPERIENCE_PATH), ROOT, definition_class, factory)
    check(experience is not None, "Could not create/load development Experience")
    check(experience.get_class() == definition_class, "Unexpected Experience asset type")
    expected = {
        "action_sets": action_sets,
        "actions": [],
        "game_features_to_enable": [],
        "default_pawn_data": None,
    }
    if exists:
        check_properties(experience, expected, "Preserved development Experience")
        PRESERVED.append(EXPERIENCE_PATH)
    else:
        for name, value in expected.items():
            experience.set_editor_property(name, value)
        check_properties(experience, expected, "New development Experience")
        save_new(experience, EXPERIENCE_PATH)
    return experience


def prepare_map(mode, selector_class, experience, cube):
    exists = unreal.EditorAssetLibrary.does_asset_exist(MAP_PATH)
    world = (unreal.EditorLoadingAndSavingUtils.load_map(MAP_PATH) if exists
             else unreal.EditorLoadingAndSavingUtils.new_blank_map(False))
    check(world is not None, "Could not create/load development map")
    settings = world.get_world_settings()
    selection_mode = unreal.RPGExperienceSelectionMode.LOAD_EXPERIENCE
    selector_expected = {
        "enabled": True, "selection_mode": selection_mode, "experience": experience,
    }
    if exists:
        check_properties(settings, {"default_game_mode": mode}, "Preserved map")
        selectors = list(unreal.GameplayStatics.get_all_actors_of_class(world, selector_class))
        check(len(selectors) == 1, "Preserved map must have exactly one selector")
        check_properties(selectors[0], selector_expected, "Preserved selector")
        PRESERVED.append(MAP_PATH)
        return

    settings.set_editor_property("default_game_mode", mode)
    settings.set_editor_property("force_no_precomputed_lighting", True)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    selector = actors.spawn_actor_from_class(selector_class, unreal.Vector(0, 0, 0))
    check(selector is not None, "Could not create Experience selector")
    selector.set_actor_label("CommonClientExperience")
    for name, value in selector_expected.items():
        selector.set_editor_property(name, value)

    start = actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(0, 0, 250))
    check(start is not None, "Could not create development PlayerStart")
    start.set_actor_label("DevelopmentSpectatorStart")
    floor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, -20))
    check(floor is not None, "Could not create development floor")
    floor.set_actor_label("DevelopmentFloor")
    floor.static_mesh_component.set_static_mesh(cube)
    floor.set_actor_scale3d(unreal.Vector(30, 30, 0.2))
    light = actors.spawn_actor_from_class(
        unreal.DirectionalLight, unreal.Vector(0, 0, 500), unreal.Rotator(-45, 0, 0))
    check(light is not None, "Could not create development light")
    light.set_actor_label("DevelopmentLight")
    light.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    check_properties(settings, {"default_game_mode": mode}, "New map")
    check_properties(selector, selector_expected, "New selector")
    check(unreal.EditorLoadingAndSavingUtils.save_map(world, MAP_PATH),
          "Could not save development map")
    CREATED.append(MAP_PATH)


def development_controller_class():
    # The optional input migration owns this Blueprint; the base creator only checks/reuses it.
    if not unreal.EditorAssetLibrary.does_asset_exist(CONTROLLER_PATH):
        return load_class("/Script/Engine.PlayerController")
    blueprint = load_asset(CONTROLLER_PATH)
    check(isinstance(blueprint, unreal.Blueprint), "Unexpected development input controller")
    check(blueprint.get_editor_property("status") == unreal.BlueprintStatus.BS_UP_TO_DATE,
          "Development input controller must compile cleanly")
    check(unreal.BlueprintEditorLibrary.get_blueprint_parent_class(blueprint)
          == load_class("/Script/Project_RPG.RPGPlayerController"),
          "Unexpected development input controller parent")
    generated = blueprint.generated_class()
    check(generated is not None, "Missing development input controller class")
    check_properties(unreal.get_default_object(generated),
                     {"InputConfigDataAsset": load_asset(INPUT_CONFIG_PATH)}, "Development input controller")
    return generated


def main():
    # Resolve dependencies before saving any new package.
    definition_class = load_class("/Script/Project_RPG.RPGExperienceDefinition")
    hud_class = load_class("/Script/Project_RPG.RPGHUD")
    selector_class = load_class("/Script/Project_RPG.RPGExperienceSelectionActor")
    option_class = load_class(OPTION_PATH + "." + short_name(OPTION_PATH) + "_C")
    game_state_class = load_class("/Script/Project_RPG.RPGGameStateBase")
    engine_mode_class = load_class("/Script/Engine.GameModeBase")
    controller_class = development_controller_class()
    player_state_class = load_class("/Script/Engine.PlayerState")
    spectator_class = load_class("/Script/Engine.SpectatorPawn")
    cube = load_asset("/Engine/BasicShapes/Cube")
    action_sets = []
    all_features = set()
    for path, features, count in SET_SPECS:
        action_set = load_asset(path)
        check(list(action_set.get_editor_property("game_features_to_enable")) == features,
              "Existing ActionSet has different features: " + path)
        check(len(action_set.get_editor_property("actions")) == count,
              "Existing ActionSet has a different action count: " + path)
        action_sets.append(action_set)
        all_features.update(features)
    check(all_features == set(FEATURES), "Development feature composition differs")
    for feature in FEATURES:
        load_asset("/" + feature + "/" + feature)

    experience = prepare_experience(definition_class, action_sets)
    hud = prepare_blueprint(HUD_PATH, hud_class, {
        "option_menu_widget_class": option_class,
        "main_menu_widget_class": None, "interaction_widget_class": None,
        "class_selection_widget_class": None,
    })
    mode = prepare_blueprint(MODE_PATH, engine_mode_class, {
        "game_state_class": game_state_class,
        "player_controller_class": controller_class,
        "player_state_class": player_state_class,
        "default_pawn_class": spectator_class, "hud_class": hud,
    })
    prepare_map(mode, selector_class, experience, cube)
    unreal.log("RPG_EXPERIENCE_DEVELOPMENT ASSETS_READY " + json.dumps({
        "experience": EXPERIENCE_PATH, "map": MAP_PATH, "hud": HUD_PATH,
        "game_mode": MODE_PATH, "features": FEATURES,
        "action_sets": [path for path, _, _ in SET_SPECS],
        "created": CREATED, "preserved": PRESERVED,
        "gameplay_started": False,
    }, sort_keys=True))


if __name__ == "__main__":
    main()
