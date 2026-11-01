"""Create an isolated camera Experience using the existing common-client ActionSets.

Requires a normal build containing GameFeatureAction_RPGCameraPresentation.
Run in UnrealEditor-Cmd with -run=PythonScript -Script=<this file>
-RPGCreateCameraDevelopment -multiprocess -nowrite. Only missing packages are saved.
"""
import json
import pathlib
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import unreal
import CreateExperienceDevelopment as base

ROOT = base.ROOT + "/Camera"
ACTION_SET_PATH = ROOT + "/DA_CameraPresentation"
EXPERIENCE_PATH = ROOT + "/DA_CameraClientDevelopment"
CONTROLLER_PATH = ROOT + "/BP_CameraDevelopmentController"
MODE_PATH = ROOT + "/BP_CameraDevelopmentGameMode"
MAP_PATH = ROOT + "/Maps/L_CameraExperienceDevelopment"
CAMERA_TAG = "RPG.Development.PresentationCamera"
CAMERA_PRIORITY = 10
ALLOWED_PACKAGES = {ACTION_SET_PATH, EXPERIENCE_PATH, CONTROLLER_PATH, MODE_PATH, MAP_PATH}
CREATED = []
PRESERVED = []
COMPILED = []


def save_new(asset, path):
    base.check(path in ALLOWED_PACKAGES, "Refusing to save outside the camera development packages")
    base.check(unreal.EditorAssetLibrary.save_loaded_asset(asset, False), "Save failed: " + path)
    CREATED.append(path)
    unreal.log("RPG_CAMERA_DEVELOPMENT SAVED " + path)


def create_data(path, asset_class):
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", asset_class)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        base.short_name(path), path.rsplit("/", 1)[0], asset_class, factory)
    base.check(asset is not None, "Create failed: " + path)
    return asset


def prepare_camera_action_set(asset_class, action_class):
    exists = unreal.EditorAssetLibrary.does_asset_exist(ACTION_SET_PATH)
    asset = base.load_asset(ACTION_SET_PATH) if exists else create_data(ACTION_SET_PATH, asset_class)
    base.check(asset.get_class() == asset_class, "Unexpected camera ActionSet class")
    if not exists:
        action = unreal.new_object(action_class, outer=asset)
        base.check(action is not None, "Could not create the native camera Action")
        action.set_editor_property("CameraTargetActorTag", unreal.Name(CAMERA_TAG))
        action.set_editor_property("CameraPriority", CAMERA_PRIORITY)
        asset.set_editor_property("game_features_to_enable", ["RPGClientPresentation"])
        asset.set_editor_property("actions", [action])
    base.check(list(asset.get_editor_property("game_features_to_enable")) == ["RPGClientPresentation"],
               "Camera ActionSet dependencies differ")
    actions = list(asset.get_editor_property("actions"))
    base.check(len(actions) == 1 and actions[0].get_class() == action_class,
               "Expected exactly one native camera Action")
    base.check(actions[0].get_outer() == asset, "Camera Action must belong to this ActionSet")
    base.check_properties(actions[0], {
        "CameraTargetActorTag": unreal.Name(CAMERA_TAG), "CameraPriority": CAMERA_PRIORITY,
    }, "Camera Action")
    if exists:
        PRESERVED.append(ACTION_SET_PATH)
    else:
        save_new(asset, ACTION_SET_PATH)
    return asset


def prepare_experience(asset_class, action_sets):
    exists = unreal.EditorAssetLibrary.does_asset_exist(EXPERIENCE_PATH)
    asset = base.load_asset(EXPERIENCE_PATH) if exists else create_data(EXPERIENCE_PATH, asset_class)
    base.check(asset.get_class() == asset_class, "Unexpected camera Experience class")
    expected = {"action_sets": action_sets, "actions": [], "game_features_to_enable": [], "default_pawn_data": None}
    if not exists:
        for name, value in expected.items():
            asset.set_editor_property(name, value)
    base.check_properties(asset, expected, "Camera Experience")
    if exists:
        PRESERVED.append(EXPERIENCE_PATH)
    else:
        save_new(asset, EXPERIENCE_PATH)
    return asset


def compile_blueprint(blueprint, path):
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    base.check(blueprint.get_editor_property("status") == unreal.BlueprintStatus.BS_UP_TO_DATE,
               "Blueprint must compile cleanly: " + path)
    generated = blueprint.generated_class()
    base.check(generated is not None, "Missing generated class: " + path)
    COMPILED.append(path)
    return unreal.get_default_object(generated)


def prepare_blueprint(path, parent, expected):
    exists = unreal.EditorAssetLibrary.does_asset_exist(path)
    if exists:
        blueprint = base.load_asset(path)
        base.check(isinstance(blueprint, unreal.Blueprint), "Expected Blueprint: " + path)
        base.check(unreal.BlueprintEditorLibrary.get_blueprint_parent_class(blueprint) == parent,
                   "Unexpected parent: " + path)
        base.check(blueprint.get_editor_property("status") == unreal.BlueprintStatus.BS_UP_TO_DATE,
                   "Preserved Blueprint must compile cleanly: " + path)
        defaults = unreal.get_default_object(blueprint.generated_class())
    else:
        factory = unreal.BlueprintFactory()
        factory.set_editor_property("parent_class", parent)
        blueprint = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            base.short_name(path), path.rsplit("/", 1)[0], unreal.Blueprint, factory)
        base.check(blueprint is not None, "Create failed: " + path)
        defaults = compile_blueprint(blueprint, path)
        for name, value in expected.items():
            defaults.set_editor_property(name, value)
        defaults = compile_blueprint(blueprint, path)
    base.check_properties(defaults, expected, path)
    if exists:
        PRESERVED.append(path)
    else:
        save_new(blueprint, path)
    return blueprint.generated_class()


def check_map(world, mode, selector_class, experience):
    base.check_properties(world.get_world_settings(), {"default_game_mode": mode}, "Camera map")
    selectors = list(unreal.GameplayStatics.get_all_actors_of_class(world, selector_class))
    base.check(len(selectors) == 1, "Camera map must have exactly one Experience selector")
    base.check_properties(selectors[0], {
        "enabled": True, "selection_mode": unreal.RPGExperienceSelectionMode.LOAD_EXPERIENCE,
        "experience": experience,
    }, "Camera map selector")
    cameras = [actor for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.CameraActor)
               if actor.actor_has_tag(unreal.Name(CAMERA_TAG))]
    base.check(len(cameras) == 1, "Camera map must have exactly one tagged CameraActor")


def prepare_map(mode, selector_class, experience, cube):
    exists = unreal.EditorAssetLibrary.does_asset_exist(MAP_PATH)
    world = (unreal.EditorLoadingAndSavingUtils.load_map(MAP_PATH) if exists
             else unreal.EditorLoadingAndSavingUtils.new_blank_map(False))
    base.check(world is not None, "Could not create/load camera development map")
    if exists:
        check_map(world, mode, selector_class, experience)
        PRESERVED.append(MAP_PATH)
        return
    settings = world.get_world_settings()
    settings.set_editor_property("default_game_mode", mode)
    settings.set_editor_property("force_no_precomputed_lighting", True)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    selector = actors.spawn_actor_from_class(selector_class, unreal.Vector(0, 0, 0))
    base.check(selector is not None, "Could not create camera Experience selector")
    selector.set_actor_label("CameraClientExperience")
    for name, value in {
        "enabled": True, "selection_mode": unreal.RPGExperienceSelectionMode.LOAD_EXPERIENCE,
        "experience": experience,
    }.items():
        selector.set_editor_property(name, value)
    camera = actors.spawn_actor_from_class(
        unreal.CameraActor, unreal.Vector(-1200, -1400, 900), unreal.Rotator(-25, 49, 0))
    base.check(camera is not None, "Could not create presentation CameraActor")
    camera.set_actor_label("ExperiencePresentationCamera")
    camera.set_editor_property("tags", [unreal.Name(CAMERA_TAG)])
    camera_component = camera.get_component_by_class(unreal.CameraComponent)
    base.check(camera_component is not None, "CameraActor has no camera component")
    camera_component.set_editor_property("field_of_view", 70.0)
    start = actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(0, 0, 250))
    base.check(start is not None, "Could not create spectator start")
    start.set_actor_label("CameraDevelopmentSpectatorStart")
    floor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, -20))
    base.check(floor is not None, "Could not create camera development floor")
    floor.set_actor_label("CameraDevelopmentFloor")
    floor.static_mesh_component.set_static_mesh(cube)
    floor.set_actor_scale3d(unreal.Vector(30, 30, 0.2))
    light = actors.spawn_actor_from_class(
        unreal.DirectionalLight, unreal.Vector(0, 0, 500), unreal.Rotator(-45, 0, 0))
    base.check(light is not None, "Could not create camera development light")
    light.set_actor_label("CameraDevelopmentLight")
    light.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    check_map(world, mode, selector_class, experience)
    base.check(MAP_PATH in ALLOWED_PACKAGES, "Map save is outside the camera development packages")
    base.check(unreal.EditorLoadingAndSavingUtils.save_map(world, MAP_PATH), "Could not save camera map")
    CREATED.append(MAP_PATH)


def main():
    base.check("-RPGCreateCameraDevelopment" in unreal.SystemLibrary.get_command_line(),
               "Explicit -RPGCreateCameraDevelopment is required")
    # Resolve the compiled Action and shared dependencies before saving any package.
    action_class = base.load_class("/Script/RPGClientPresentationRuntime.GameFeatureAction_RPGCameraPresentation")
    action_defaults = unreal.get_default_object(action_class)
    action_defaults.get_editor_property("CameraTargetActorTag")
    action_defaults.get_editor_property("CameraPriority")
    definition_class = base.load_class("/Script/Project_RPG.RPGExperienceDefinition")
    action_set_class = base.load_class("/Script/Project_RPG.RPGExperienceActionSet")
    camera_manager = base.load_class("/Script/Project_RPG.RPGPlayerCameraManager")
    selector_class = base.load_class("/Script/Project_RPG.RPGExperienceSelectionActor")
    source_controller = base.development_controller_class()
    expected_controller = base.load_class(base.CONTROLLER_PATH + "." + base.short_name(base.CONTROLLER_PATH) + "_C")
    base.check(source_controller == expected_controller, "Run ConfigureExperienceDevelopmentInput.py first")
    config = base.load_asset(base.INPUT_CONFIG_PATH)
    source_mode = base.load_asset(base.MODE_PATH)
    base.check(isinstance(source_mode, unreal.Blueprint), "Expected common development GameMode")
    base.check(source_mode.get_editor_property("status") == unreal.BlueprintStatus.BS_UP_TO_DATE,
               "Common development GameMode must compile cleanly")
    mode_parent = source_mode.generated_class()
    mode_defaults = unreal.get_default_object(mode_parent)
    expected_mode = {
        "game_state_class": base.load_class("/Script/Project_RPG.RPGGameStateBase"),
        "player_controller_class": source_controller,
        "player_state_class": base.load_class("/Script/Engine.PlayerState"),
        "default_pawn_class": base.load_class("/Script/Engine.SpectatorPawn"),
        "hud_class": base.load_class(base.HUD_PATH + "." + base.short_name(base.HUD_PATH) + "_C"),
    }
    base.check_properties(mode_defaults, expected_mode, "Common development GameMode")
    action_sets = []
    for path, features, count in base.SET_SPECS:
        asset = base.load_asset(path)
        base.check(list(asset.get_editor_property("game_features_to_enable")) == features
                   and len(asset.get_editor_property("actions")) == count, "Shared composition changed: " + path)
        action_sets.append(asset)
    base.check_properties(base.load_asset(base.EXPERIENCE_PATH), {
        "action_sets": action_sets, "actions": [], "game_features_to_enable": [], "default_pawn_data": None,
    }, "Common development Experience")
    for feature in base.FEATURES:
        base.load_asset("/" + feature + "/" + feature)
    cube = base.load_asset("/Engine/BasicShapes/Cube")

    camera_set = prepare_camera_action_set(action_set_class, action_class)
    experience = prepare_experience(definition_class, action_sets + [camera_set])
    controller = prepare_blueprint(CONTROLLER_PATH, source_controller, {
        "player_camera_manager_class": camera_manager, "InputConfigDataAsset": config,
    })
    mode = prepare_blueprint(MODE_PATH, mode_parent, dict(expected_mode, player_controller_class=controller))
    prepare_map(mode, selector_class, experience, cube)
    result = {
        "created": CREATED, "preserved": PRESERVED, "blueprints_compiled": COMPILED,
        "experience": EXPERIENCE_PATH, "action_set": ACTION_SET_PATH,
        "controller": CONTROLLER_PATH, "game_mode": MODE_PATH, "map": MAP_PATH,
        "camera_tag": CAMERA_TAG, "camera_priority": CAMERA_PRIORITY,
        "gameplay_started": False, "tests_run": False,
    }
    record = pathlib.Path(__file__).resolve().parents[2] / "Saved/Logs/RPGCameraDevelopmentExperience-20261024"
    record.mkdir(parents=True, exist_ok=True)
    (record / "AssetApplication.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    unreal.log("RPG_CAMERA_DEVELOPMENT ASSETS_READY " + json.dumps(result, sort_keys=True))


if __name__ == "__main__":
    main()
