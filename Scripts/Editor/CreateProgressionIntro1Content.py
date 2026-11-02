"""Bind Intro1's owned arena to its Pawn, completion mode and pinned server reward.

Read-only by default. -RPGConfigureProgressionIntro1Content creates missing owned
assets and connects only the untouched preview arena. Never publishes admission.
RPG_INTRO1_CONTENT_REPORT must be an absolute JSON report path.
"""
import importlib.util
import json
import os
from pathlib import Path

PAWN = "/Game/Blueprints/Character/Player/Progression/BP_Intro1Player"
MODE = "/Game/Blueprints/GameMode/Progression/BP_Intro1GameMode"
SERVER = "/Game/System/Dungeons/Progression/DA_DungeonServer_Intro1"
REWARD = "/Game/Blueprints/GameData/DungeonReward/Progression/DA_DungeonReward_Intro1"
ORE = "/Game/Blueprints/GameData/Item/Progression/DA_Item_ProgressionOre"
SOURCE_PAWN = "/Game/Blueprints/Character/Player/BP_Player"
SOURCE_MODE = "/Game/Blueprints/GameMode/BP_GameModeBase"
MARKER = "FirstSlice.Intro1Content.v1"
METADATA = "RPGIntro1Content"
ROLES = ("player_controller_class", "player_state_class", "game_state_class", "hud_class")

def require(condition, message):
    if not condition:
        raise RuntimeError(message)

def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value

def disk(unreal, package):
    root = Path(unreal.Paths.project_content_dir()).resolve()
    path = root / (package[6:] + (".umap" if package.endswith("L_ProgressionIntro1") else ".uasset"))
    require(root in path.resolve().parents, "Refusing a target outside closed Content")
    return path

def owned(unreal, asset):
    require(unreal.EditorAssetLibrary.get_metadata_tag(asset, METADATA) == MARKER, "Preserving asset authored elsewhere")

def inspect_pawn(unreal, pawn, parent, objective, tools):
    owned(unreal, pawn)
    require(isinstance(pawn, unreal.Blueprint) and pawn.generated_class()
            and pawn.get_blueprint_parent_class() == parent, "Preserving another Pawn class/parent")
    defaults = unreal.get_default_object(pawn.generated_class())
    components = tools.get_components(defaults)
    found = [c for c in components if c.get_class() == objective]
    require(len(found) == 1 and not any(c.get_class().get_path_name() == "/Script/Project_RPG.RPGTrainingObjectiveComponent" for c in components),
            "Intro1 needs its own single objective, not training goals")
    require(json.loads(unreal.ToolsetLibrary.get_object_properties(found[0], ["bReplicates"]))["bReplicates"] is True,
            "Intro1 progress must replicate")
    require(defaults.get_editor_property("character_start_up_data") == unreal.get_default_object(parent).get_editor_property("character_start_up_data"),
            "Preserving a custom startup recipe")
    return {"parent": parent.get_path_name(), "objectives": 1, "replicates": True, "startup_inherited": True}

def inspect_server(unreal, server, reward, arena, utilities):
    owned(unreal, server)
    require(isinstance(server, unreal.RPGDungeonServerDefinition)
            and server.get_editor_property("dungeon_id") == "Progression.Intro1"
            and server.get_dungeon_map_package() == arena.MAP, "Server content/map differs")
    require(dict(server.get_editor_property("clear_rewards_by_difficulty")) == {unreal.RPGGameDifficulty.NORMAL: reward},
            "Server reward/difficulty differs")
    return {"content": "Progression.Intro1", "map": arena.MAP, "reward": reward.get_path_name(),
            "validation": utilities.validate(unreal, server, False)}

def inspect_mode(unreal, mode, parent, pawn, server, roles, utilities):
    owned(unreal, mode)
    require(isinstance(mode, unreal.Blueprint) and mode.generated_class()
            and mode.get_blueprint_parent_class() == parent, "Mode completion parent differs")
    defaults = unreal.get_default_object(mode.generated_class())
    for key, value in {**roles, "default_pawn_class": pawn.generated_class()}.items():
        require(defaults.get_editor_property(key) == value, "Mode role differs: " + key)
    require(defaults.get_editor_property("bGiveReward") and defaults.get_editor_property("require_backend_join_ticket")
            and defaults.get_editor_property("require_steam_identity_match")
            and defaults.get_editor_property("current_game_difficulty") == unreal.RPGGameDifficulty.NORMAL
            and list(defaults.get_editor_property("dungeon_server_definitions")) == [server], "Mode authority/catalog differs")
    return {"parent": parent.get_path_name(), "pawn": pawn.generated_class().get_path_name(),
            "roles": {key: value.get_path_name() for key, value in roles.items()},
            "validation": utilities.validate(unreal, defaults)}

def main():
    import unreal
    from editor_toolset.toolsets.actor import ActorTools
    arena = module("intro1_arena_authoring", "CreateProgressionIntro1Arena.py")
    utilities = module("training_authoring_utilities", "CreateProgressionTrainingContent.py")
    output = Path(os.environ.get("RPG_INTRO1_CONTENT_REPORT", ""))
    require(output.is_absolute(), "Set absolute RPG_INTRO1_CONTENT_REPORT")
    apply = "-rpgconfigureprogressionintro1content" in unreal.SystemLibrary.get_command_line().lower().split()
    report = {"passed": False, "read_only": not apply, "saved": [], "published": False, "gameplay_verified": False}
    try:
        require(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is None, "Author outside PIE")
        targets = (PAWN, SERVER, MODE, arena.MAP)
        assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
        dirty = arena.dirty_packages(unreal)
        before = {p: arena.digest(disk(unreal, p)) for p in targets}
        for package in targets:
            require(package not in dirty, "Preserving unsaved content: " + package)
            require(assets.does_asset_exist(package) == bool(before[package]), "Registry/disk disagree: " + package)
        require(before[arena.MAP], "Create the owned arena first")
        source_pawn = assets.load_asset(SOURCE_PAWN)
        source_mode = assets.load_asset(SOURCE_MODE)
        require(isinstance(source_pawn, unreal.Blueprint) and source_pawn.generated_class()
                and isinstance(source_mode, unreal.Blueprint) and source_mode.generated_class(), "Existing role recipes unavailable")
        roles = {key: unreal.get_default_object(source_mode.generated_class()).get_editor_property(key) for key in ROLES}
        require(all(roles.values()), "Existing role recipe incomplete")
        objective = unreal.load_class(None, "/Script/Project_RPG.RPGIntroApproachObjectiveComponent")
        completion = unreal.load_class(None, "/Script/Project_RPG.RPGIntroApproachGameMode")
        require(objective and completion, "Load a compatible Intro1 game module")
        reward, ore = assets.load_asset(REWARD), assets.load_asset(ORE)
        require(reward and ore, "Installed Intro1 reward/ore unavailable")
        materials = module("intro1_material_rules", "CreateProgressionMaterialAssets.py")
        report["reward"] = materials.inspect_reward(unreal, reward, REWARD, ore)
        meshes = {name: assets.load_asset("/Engine/BasicShapes/" + name) for name in ("Cube", "Cylinder")}
        experience = assets.load_asset(arena.EXPERIENCE)
        require(all(meshes.values()) and experience, "Existing presentation dependencies unavailable")
        world = unreal.EditorLoadingAndSavingUtils.load_map(arena.MAP)
        require(world, "Could not load owned arena")
        preview = unreal.load_class(None, "/Script/Engine.GameModeBase")
        actual_mode = world.get_world_settings().get_editor_property("default_game_mode")
        was_preview = actual_mode == preview
        if was_preview:
            require(not unreal.EditorAssetLibrary.get_metadata_tag(world, METADATA), "Preserving incomplete/custom binding")
        else:
            owned(unreal, world)
            require(actual_mode and actual_mode.get_path_name() == arena.BOUND_MODE, "Preserving another map mode")
        # Validate all owned geometry before creating or saving anything.
        report["map_before"] = arena.inspect(unreal, world, meshes, actual_mode)
        pawn = assets.load_asset(PAWN) if before[PAWN] else None
        server = assets.load_asset(SERVER) if before[SERVER] else None
        mode = assets.load_asset(MODE) if before[MODE] else None
        states = {}
        if pawn: states[PAWN] = inspect_pawn(unreal, pawn, source_pawn.generated_class(), objective, ActorTools)
        if server: states[SERVER] = inspect_server(unreal, server, reward, arena, utilities)
        if mode:
            require(pawn and server, "Existing mode has missing dependencies")
            states[MODE] = inspect_mode(unreal, mode, completion, pawn, server, roles, utilities)
        if not was_preview:
            require(mode and pawn and server and actual_mode == mode.generated_class(), "Bound map dependencies differ")
        if apply:
            def save_missing(asset, package):
                require(not before[package] and not disk(unreal, package).exists(), "Target appeared: " + package)
                require(assets.save_loaded_asset(asset, only_if_is_dirty=False), "Could not save: " + package)
                report["saved"].append(package)
            def blueprint(package, parent):
                factory = unreal.BlueprintFactory(); factory.set_editor_property("parent_class", parent)
                asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(package.rsplit("/", 1)[1], package.rsplit("/", 1)[0], unreal.Blueprint, factory)
                require(asset, "Could not create: " + package)
                unreal.EditorAssetLibrary.set_metadata_tag(asset, METADATA, MARKER)
                return asset
            if not pawn:
                pawn = blueprint(PAWN, source_pawn.generated_class())
                require(ActorTools.add_component(pawn, objective, "Intro1Objectives"), "Could not add objective component")
                utilities.compile_blueprint(unreal, pawn)
                states[PAWN] = inspect_pawn(unreal, pawn, source_pawn.generated_class(), objective, ActorTools)
                save_missing(pawn, PAWN)
            if not server:
                server = unreal.AssetToolsHelpers.get_asset_tools().create_asset(SERVER.rsplit("/", 1)[1], SERVER.rsplit("/", 1)[0], unreal.RPGDungeonServerDefinition, unreal.DataAssetFactory())
                require(server, "Could not create server definition")
                unreal.EditorAssetLibrary.set_metadata_tag(server, METADATA, MARKER)
                server.set_editor_property("dungeon_id", "Progression.Intro1")
                require(unreal.ToolsetLibrary.set_object_properties(server, json.dumps({"DungeonMap": arena.MAP + "." + arena.MAP.rsplit("/", 1)[1]})), "Could not assign server map")
                server.set_editor_property("clear_rewards_by_difficulty", {unreal.RPGGameDifficulty.NORMAL: reward})
                states[SERVER] = inspect_server(unreal, server, reward, arena, utilities)
                save_missing(server, SERVER)
            if not mode:
                mode = blueprint(MODE, completion)
                require(mode.generated_class(), "New mode class unavailable before configuring defaults")
                defaults = unreal.get_default_object(mode.generated_class())
                for key, value in {**roles, "default_pawn_class": pawn.generated_class(), "bGiveReward": True,
                                   "current_game_difficulty": unreal.RPGGameDifficulty.NORMAL, "dungeon_server_definitions": [server]}.items():
                    defaults.set_editor_property(key, value)
                # The native parent enables rewards. Its pinned server definition
                # must be populated before Blueprint compilation validates the CDO.
                utilities.compile_blueprint(unreal, mode)
                states[MODE] = inspect_mode(unreal, mode, completion, pawn, server, roles, utilities)
                save_missing(mode, MODE)
            if was_preview:
                require(arena.digest(disk(unreal, arena.MAP)) == before[arena.MAP], "Arena changed before binding")
                selector = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).spawn_actor_from_class(unreal.RPGExperienceSelectionActor, unreal.Vector(0, 0, 0))
                require(selector, "Could not add client presentation selector")
                selector.set_actor_label(arena.PREFIX + "Experience")
                selector.set_editor_property("tags", [unreal.Name(arena.MARKER), unreal.Name(MARKER)])
                selector.set_editor_property("experience", experience)
                selector.set_editor_property("selection_mode", unreal.RPGExperienceSelectionMode.LOAD_EXPERIENCE)
                selector.set_editor_property("enabled", True)
                world.get_world_settings().set_editor_property("default_game_mode", mode.generated_class())
                unreal.EditorAssetLibrary.set_metadata_tag(world, METADATA, MARKER)
                arena.inspect(unreal, world, meshes, mode.generated_class())
                require(unreal.EditorLoadingAndSavingUtils.save_map(world, arena.MAP), "Could not save bound map")
                report["saved"].append(arena.MAP)
        for package in targets:
            require(package not in arena.dirty_packages(unreal), "Target remains dirty: " + package)
            after = arena.digest(disk(unreal, package))
            if package not in report["saved"]:
                require(after == before[package], "Existing target changed: " + package)
            states.setdefault(package, {})["sha256"] = after
        current_mode = world.get_world_settings().get_editor_property("default_game_mode")
        report.update(passed=True, saved_count=len(report["saved"]), missing_count=sum(not disk(unreal, p).is_file() for p in targets),
                      states=states, map=arena.inspect(unreal, world, meshes, current_mode), content_configured=current_mode.get_path_name() == arena.BOUND_MODE)
        unreal.log("RPG_INTRO1_CONTENT " + json.dumps(report, ensure_ascii=False))
    except Exception as error:
        report["error"] = str(error)
        raise
    finally:
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")

if __name__ == "__main__":
    main()
