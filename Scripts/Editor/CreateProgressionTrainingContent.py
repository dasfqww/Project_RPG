"""Create the first training's isolated Pawn, map, mode and server metadata.

Read-only by default. -RPGCreateProgressionTrainingContent creates missing
targets only. -RPGConfigureProgressionTrainingCompletion explicitly reparents
the existing owned training mode after a compatible native module is loaded.
The default and reload modes remain read-only. Never publishes the dungeon.
"""

import hashlib
import json
import os
import pathlib


PAWN = "/Game/Blueprints/Character/Player/Progression/BP_TrainingPlayer"
MODE = "/Game/Blueprints/GameMode/Progression/BP_TrainingGameMode"
COMPLETION_MODE = "/Script/Project_RPG.RPGTrainingGameMode"
MAP = "/Game/Maps/Progression/L_ProgressionTraining"
SERVER = "/Game/System/Dungeons/Progression/DA_DungeonServer_Training"
SOURCE_PAWN = "/Game/Blueprints/Character/Player/BP_Player"
SOURCE_MODE = "/Game/Blueprints/GameMode/BP_GameModeBase"
REWARD = "/Game/Blueprints/GameData/DungeonReward/DA_DungeonReward_ProgressionTraining"
EXPERIENCE = "/Game/System/Experiences/DA_ClientPresentationAndOptionsHint"
MARKER = "FirstSlice.TrainingContent.v1"
ROLE_FIELDS = ["player_controller_class", "player_state_class", "game_state_class", "hud_class"]
TARGETS = [PAWN, SERVER, MODE, MAP]
GEOMETRY = {
    "Training_Floor": ((0, 0, -50), (80, 80, 1)),
    "Training_WallNorth": ((0, 4050, 200), (82, 1, 4)),
    "Training_WallSouth": ((0, -4050, 200), (82, 1, 4)),
    "Training_WallEast": ((4050, 0, 200), (1, 80, 4)),
    "Training_WallWest": ((-4050, 0, 200), (1, 80, 4)),
}
STARTS = {"Training_PlayerStart" + str(i + 1): (-600, -450 + i * 300, 110) for i in range(4)}


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def package_disk(unreal, package):
    extension = ".umap" if package == MAP else ".uasset"
    return pathlib.Path(unreal.Paths.project_content_dir()) / (package[len("/Game/"):] + extension)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest() if path.exists() else None


def dirty_packages(unreal):
    return {str(p.get_name()) for p in (
        list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
        + list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()))}


def compile_blueprint(unreal, asset):
    unreal.BlueprintEditorLibrary.compile_blueprint(asset)
    require(asset.get_editor_property("status") == unreal.BlueprintStatus.BS_UP_TO_DATE,
            "Blueprint must compile cleanly: " + asset.get_path_name())


def validate(unreal, asset, require_valid=True):
    state, errors, warnings = unreal.get_editor_subsystem(unreal.EditorValidatorSubsystem).is_object_valid(
        asset, unreal.DataValidationUsecase.SCRIPT)
    require(state != unreal.DataValidationResult.INVALID and not errors and not warnings,
            "Native validation failed: " + str((asset.get_path_name(), state, errors, warnings)))
    if require_valid:
        require(state == unreal.DataValidationResult.VALID, "Validation did not report Valid: " + asset.get_path_name())
    return {"result": str(state), "errors": [], "warnings": []}


def create_blueprint(unreal, package, parent):
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", parent)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        package.rsplit("/", 1)[1], package.rsplit("/", 1)[0], unreal.Blueprint, factory)
    require(asset is not None, "Could not create: " + package)
    unreal.EditorAssetLibrary.set_metadata_tag(asset, "RPGTrainingContent", MARKER)
    return asset


def check_pawn(unreal, pawn, parent_class, training_class, actor_tools):
    require(isinstance(pawn, unreal.Blueprint) and pawn.generated_class(), "Training Pawn Blueprint unavailable")
    require(pawn.get_blueprint_parent_class() == parent_class, "Preserving a different training Pawn parent")
    require(unreal.EditorAssetLibrary.get_metadata_tag(pawn, "RPGTrainingContent") == MARKER,
            "Preserving a training Pawn authored elsewhere")
    defaults = unreal.get_default_object(pawn.generated_class())
    components = actor_tools.get_components(defaults)
    training = [c for c in components if c.get_class() == training_class]
    require(len(training) == 1, "Training Pawn requires exactly one objective component")
    replication = json.loads(unreal.ToolsetLibrary.get_object_properties(training[0], ["bReplicates"]))
    require(replication["bReplicates"] is True, "Training progress component must replicate")
    require(defaults.get_editor_property("character_start_up_data") ==
            unreal.get_default_object(parent_class).get_editor_property("character_start_up_data"),
            "Preserving a different Pawn startup recipe")
    return {"parent": parent_class.get_path_name(), "objective_components": 1,
            "component_replicates": True, "startup_inherited": True,
            "compile_status": str(pawn.get_editor_property("status"))}


def check_server(unreal, server, reward):
    require(isinstance(server, unreal.RPGDungeonServerDefinition), "Server definition has another class")
    require(server.get_editor_property("dungeon_id") == "Progression.Training", "Server content ID differs")
    require(server.get_dungeon_map_package() == MAP, "Server map differs")
    rewards = dict(server.get_editor_property("clear_rewards_by_difficulty"))
    require(rewards == {unreal.RPGGameDifficulty.NORMAL: reward}, "Server reward/difficulty differs")
    # Some inherited data assets return NotValidated on their success branch.
    # The mode CDO below must additionally return Valid after validating this catalog.
    return {"dungeon_id": "Progression.Training", "map": MAP, "difficulty": "Normal",
            "reward": reward.get_path_name(), "validation": validate(unreal, server, False)}


def check_mode(unreal, mode, pawn_class, server, roles):
    require(isinstance(mode, unreal.Blueprint) and mode.generated_class(), "Training mode unavailable")
    parent = mode.get_blueprint_parent_class().get_path_name()
    require(parent in ("/Script/Project_RPG.RPGGameModeBase", COMPLETION_MODE), "Preserving another mode parent")
    require(unreal.EditorAssetLibrary.get_metadata_tag(mode, "RPGTrainingContent") == MARKER,
            "Preserving a mode authored elsewhere")
    defaults = unreal.get_default_object(mode.generated_class())
    for name, cls in {**roles, "default_pawn_class": pawn_class}.items():
        require(defaults.get_editor_property(name) == cls, "Preserving different mode role: " + name)
    require(defaults.get_editor_property("bGiveReward") and defaults.get_editor_property("require_backend_join_ticket")
            and defaults.get_editor_property("require_steam_identity_match"), "Training mode authority/reward policy differs")
    require(list(defaults.get_editor_property("dungeon_server_definitions")) == [server], "Training mode server catalog differs")
    require(defaults.get_editor_property("current_game_difficulty") == unreal.RPGGameDifficulty.NORMAL, "Training default differs")
    return {"parent": parent, "pawn": pawn_class.get_path_name(),
            "roles": {key: cls.get_path_name() for key, cls in roles.items()},
            "server_catalog": [server.get_path_name()], "reward_enabled": True,
            "join_ticket_required": True, "steam_identity_match_required": True,
            "compile_status": str(mode.get_editor_property("status")), "validation": validate(unreal, defaults)}


def xyz(value):
    return (value.x, value.y, value.z)


def check_map(unreal, world, mode_class, cube, experience):
    require(world.get_world_settings().get_editor_property("default_game_mode") == mode_class, "Training map mode differs")
    require(world.get_world_settings().get_editor_property("force_no_precomputed_lighting"), "Training lighting policy differs")
    actors = {a.get_actor_label(): a for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor)
              if a.get_actor_label().startswith("Training_")}
    expected = set(GEOMETRY) | set(STARTS) | {"Training_Sun", "Training_Sky", "Training_Experience"}
    require(set(actors) == expected, "Preserving a different training actor layout")
    for name, (location, scale) in GEOMETRY.items():
        actor = actors[name]
        require(isinstance(actor, unreal.StaticMeshActor) and xyz(actor.get_actor_location()) == location
                and xyz(actor.get_actor_scale3d()) == scale and actor.static_mesh_component.static_mesh == cube,
                "Training geometry differs: " + name)
        require(str(actor.static_mesh_component.get_collision_profile_name()) == "BlockAll", "Training geometry must block movement")
    for name, location in STARTS.items():
        require(isinstance(actors[name], unreal.PlayerStart) and xyz(actors[name].get_actor_location()) == location,
                "Training player start differs: " + name)
    selector = actors["Training_Experience"]
    require(isinstance(selector, unreal.RPGExperienceSelectionActor) and selector.get_editor_property("enabled")
            and selector.get_editor_property("selection_mode") == unreal.RPGExperienceSelectionMode.LOAD_EXPERIENCE
            and selector.get_editor_property("experience") == experience, "Training Experience differs")
    require(isinstance(actors["Training_Sun"], unreal.DirectionalLight)
            and isinstance(actors["Training_Sky"], unreal.SkyLight), "Training light classes differ")
    return {"game_mode": mode_class.get_path_name(), "player_starts": len(STARTS), "geometry_actors": len(GEOMETRY),
            "owned_actors": len(actors), "experience": experience.get_path_name(), "blocking_floor_and_walls": True}


def main():
    import unreal
    from editor_toolset.toolsets.actor import ActorTools

    flags = unreal.SystemLibrary.get_command_line().lower().split()
    apply = "-rpgcreateprogressiontrainingcontent" in flags
    reload = "-rpgprogressiontrainingcontentreload" in flags
    configure_completion = "-rpgconfigureprogressiontrainingcompletion" in flags
    require(sum((apply, reload, configure_completion)) <= 1, "Use one authoring/reload operation at a time")
    output = os.environ.get("RPG_TRAINING_CONTENT_REPORT")
    require(output and pathlib.Path(output).is_absolute(), "Set an absolute RPG_TRAINING_CONTENT_REPORT path")
    require(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is None, "Author outside PIE")
    assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    dirty = dirty_packages(unreal)
    before = {}
    for package in TARGETS:
        require(package not in dirty, "Preserving unsaved content: " + package)
        disk = package_disk(unreal, package)
        exists = assets.does_asset_exist(package)
        require(exists == disk.exists(), "Asset registry/disk disagree: " + package)
        before[package] = digest(disk)
    source_pawn = assets.load_asset(SOURCE_PAWN)
    source_mode = assets.load_asset(SOURCE_MODE)
    require(isinstance(source_pawn, unreal.Blueprint) and source_pawn.generated_class(), "Existing Pawn unavailable")
    require(isinstance(source_mode, unreal.Blueprint) and source_mode.generated_class(), "Existing role recipe unavailable")
    roles = {key: unreal.get_default_object(source_mode.generated_class()).get_editor_property(key) for key in ROLE_FIELDS}
    require(all(roles.values()), "Existing controller/state/HUD roles are incomplete")
    training_class = unreal.load_class(None, "/Script/Project_RPG.RPGTrainingObjectiveComponent")
    require(training_class is not None, "Load a module containing training objectives before authoring")
    reward = assets.load_asset(REWARD)
    require(isinstance(reward, unreal.RPGDungeonRewardDefinition)
            and str(reward.get_editor_property("reward_version")) == "FirstSlice.v1"
            and not reward.get_editor_property("currency_changes") and not reward.get_editor_property("item_rewards"),
            "XP-only training reward unavailable or customized")
    reward_validation = validate(unreal, reward)
    cube = assets.load_asset("/Engine/BasicShapes/Cube")
    experience = assets.load_asset(EXPERIENCE)
    require(cube and experience, "Existing floor/Experience dependencies unavailable")
    pawn = assets.load_asset(PAWN) if before[PAWN] else None
    server = assets.load_asset(SERVER) if before[SERVER] else None
    mode = assets.load_asset(MODE) if before[MODE] else None
    world = None
    # Existing targets are checked before any mutation. Only the explicit completion flag permits reparenting MODE.
    states = {}
    if pawn:
        states[PAWN] = check_pawn(unreal, pawn, source_pawn.generated_class(), training_class, ActorTools)
    if server:
        states[SERVER] = check_server(unreal, server, reward)
    if mode:
        require(pawn and server, "Existing mode has missing authored dependencies")
        states[MODE] = check_mode(unreal, mode, pawn.generated_class(), server, roles)
    if before[MAP]:
        require(mode, "Existing training map has no authored mode")
        world = unreal.EditorLoadingAndSavingUtils.load_map(MAP)
        require(world is not None, "Existing training map unavailable")
        states[MAP] = check_map(unreal, world, mode.generated_class(), cube, experience)
    saved = []
    if configure_completion:
        require(all(before.values()) and pawn and server and mode and world, "Create all four training targets before configuring completion")
        completion_class = unreal.load_class(None, COMPLETION_MODE)
        require(completion_class is not None, "Load a module containing RPGTrainingGameMode before configuring completion")
        if mode.get_blueprint_parent_class() != completion_class:
            require(digest(package_disk(unreal, MODE)) == before[MODE], "Training mode changed after inspection")
            dirty_now = dirty_packages(unreal)
            require(not dirty_now.intersection(TARGETS), "Preserving targets dirtied during inspection")
            prior_mode = states[MODE]
            # Unload the clean training world before class reinstancing can dirty its settings.
            world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
            require(world is not None, "Could not release the inspected training world")
            unreal.BlueprintEditorLibrary.reparent_blueprint(mode, completion_class)
            compile_blueprint(unreal, mode)
            current_mode = check_mode(unreal, mode, pawn.generated_class(), server, roles)
            for name in prior_mode.keys() - {"parent", "compile_status", "validation"}:
                require(current_mode[name] == prior_mode[name], "Mode setting changed while reparenting: " + name)
            world = unreal.EditorLoadingAndSavingUtils.load_map(MAP)
            require(world is not None, "Could not reload training map after reparenting")
            states[MAP] = check_map(unreal, world, mode.generated_class(), cube, experience)
            states[MODE] = current_mode
            require(not (dirty_packages(unreal).intersection(TARGETS) - {MODE}), "Preserving dependencies dirtied while reparenting")
            require(assets.save_loaded_asset(mode, only_if_is_dirty=False), "Could not save training completion parent")
            saved.append(MODE)
    if apply:
        def save_new(asset, package):
            require(before[package] is None and not package_disk(unreal, package).exists(), "Target appeared: " + package)
            require(assets.save_loaded_asset(asset, only_if_is_dirty=False), "Could not save: " + package)
            saved.append(package)

        if not pawn:
            pawn = create_blueprint(unreal, PAWN, source_pawn.generated_class())
            require(ActorTools.add_component(pawn, training_class, "TrainingObjectives") is not None, "Could not add objective component")
            compile_blueprint(unreal, pawn)
            states[PAWN] = check_pawn(unreal, pawn, source_pawn.generated_class(), training_class, ActorTools)
            save_new(pawn, PAWN)
        if not server:
            server = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
                SERVER.rsplit("/", 1)[1], SERVER.rsplit("/", 1)[0], unreal.RPGDungeonServerDefinition, unreal.DataAssetFactory())
            require(server is not None, "Could not create training server definition")
            server.set_editor_property("dungeon_id", "Progression.Training")
            require(unreal.ToolsetLibrary.set_object_properties(server, json.dumps({"DungeonMap": MAP + "." + MAP.rsplit("/", 1)[1]})),
                    "Could not author the soft map reference")
            server.set_editor_property("clear_rewards_by_difficulty", {unreal.RPGGameDifficulty.NORMAL: reward})
            states[SERVER] = check_server(unreal, server, reward)
            save_new(server, SERVER)
        if not mode:
            mode = create_blueprint(unreal, MODE, unreal.RPGGameModeBase.static_class())
            compile_blueprint(unreal, mode)
            defaults = unreal.get_default_object(mode.generated_class())
            for key, value in {**roles, "default_pawn_class": pawn.generated_class(), "bGiveReward": True,
                               "dungeon_server_definitions": [server], "current_game_difficulty": unreal.RPGGameDifficulty.NORMAL}.items():
                defaults.set_editor_property(key, value)
            states[MODE] = check_mode(unreal, mode, pawn.generated_class(), server, roles)
            save_new(mode, MODE)
        if not world:
            world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
            require(world is not None, "Could not create training map")
            settings = world.get_world_settings()
            settings.set_editor_property("default_game_mode", mode.generated_class())
            settings.set_editor_property("force_no_precomputed_lighting", True)
            editor_actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
            for name, (location, scale) in GEOMETRY.items():
                actor = editor_actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*location))
                require(actor is not None, "Could not create geometry: " + name)
                actor.set_actor_label(name)
                actor.static_mesh_component.set_static_mesh(cube)
                actor.static_mesh_component.set_collision_profile_name("BlockAll")
                actor.set_actor_scale3d(unreal.Vector(*scale))
            for name, location in STARTS.items():
                actor = editor_actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(*location))
                require(actor is not None, "Could not create player start")
                actor.set_actor_label(name)
            sun = editor_actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 1500), unreal.Rotator(-50, -30, 0))
            sky = editor_actors.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 1000))
            require(sun and sky, "Could not create training lights")
            sun.set_actor_label("Training_Sun"); sky.set_actor_label("Training_Sky")
            sun.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
            sky.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
            selector = editor_actors.spawn_actor_from_class(unreal.RPGExperienceSelectionActor, unreal.Vector(0, 0, 0))
            require(selector is not None, "Could not create Experience selector")
            selector.set_actor_label("Training_Experience")
            selector.set_editor_property("experience", experience)
            states[MAP] = check_map(unreal, world, mode.generated_class(), cube, experience)
            require(not package_disk(unreal, MAP).exists(), "Training map appeared before save")
            require(unreal.EditorLoadingAndSavingUtils.save_map(world, MAP), "Could not save training map")
            saved.append(MAP)
    current_dirty = dirty_packages(unreal)
    rows = []
    for package in TARGETS:
        current = digest(package_disk(unreal, package))
        permitted_reparent = configure_completion and package == MODE and package in saved
        require(permitted_reparent or before[package] is None or before[package] == current, "Existing target changed: " + package)
        require(package not in current_dirty, "Authored target remains dirty: " + package)
        rows.append({"asset": package, "saved": package in saved, "exists": current is not None,
                     "sha256": current, "properties": states.get(package)})
    completion_configured = bool(states.get(MODE) and states[MODE]["parent"] == COMPLETION_MODE)
    report = {"read_only": not (apply or configure_completion), "saved_count": len(saved), "missing_count": sum(not x["exists"] for x in rows),
              "assets": rows, "reward_validation": reward_validation,
              "published_to_lobby_or_backend": False, "party_completion_connected": completion_configured,
              "completion_mode_parent_configured": completion_configured, "party_completion_runtime_verified": False,
              "gameplay_executed": False}
    destination = pathlib.Path(output)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    unreal.log("RPG_TRAINING_CONTENT " + json.dumps(report, ensure_ascii=False))


if __name__ == "__main__":
    main()
