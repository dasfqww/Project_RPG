"""Create missing Intro3 practice assets; existing owned assets are inspected only.

Requires the compatible game module and RPG_INTRO3_REPORT (absolute JSON path).
Opt in with -RPGCreateProgressionIntro3Content. Preserves all existing rewards,
selection definitions, other maps and other agents' unsaved assets.
"""
import importlib.util
import json
import os
from pathlib import Path

MAP = "/Game/Maps/Progression/L_ProgressionIntro3"
PAWN = "/Game/Blueprints/Character/Player/Progression/BP_Intro3Player"
TARGET = "/Game/Blueprints/Character/NPC/Progression/BP_Intro3Target"
MODE = "/Game/Blueprints/GameMode/Progression/BP_Intro3GameMode"
SERVER = "/Game/System/Dungeons/Progression/DA_DungeonServer_Intro3"
MARKER = "FirstSlice.Intro3Rescue.v1"
META = "RPGIntro3Rescue"

def require(value, message):
    if not value:
        raise RuntimeError(message)

def module(name, file):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(file))
    result = importlib.util.module_from_spec(spec); spec.loader.exec_module(result)
    return result

def main():
    import unreal as u
    output = Path(os.environ.get("RPG_INTRO3_REPORT", ""))
    require(output.is_absolute(), "Set absolute RPG_INTRO3_REPORT")
    report = {"passed": False, "saved": [], "published": False}
    try:
        apply = "-rpgcreateprogressionintro3content" in u.SystemLibrary.get_command_line().lower().split()
        report["read_only"] = not apply
        require(u.get_editor_subsystem(u.UnrealEditorSubsystem).get_game_world() is None, "Author outside PIE")
        arena = module("intro3_geometry_helpers", "CreateProgressionIntro1Arena.py")
        tools = module("intro3_validation_helpers", "CreateProgressionTrainingContent.py")
        materials = module("intro3_existing_reward", "CreateProgressionMaterialAssets.py")
        assets = u.get_editor_subsystem(u.EditorAssetSubsystem)
        dirty = arena.dirty_packages(u)
        targets = (PAWN, TARGET, SERVER, MODE, MAP)
        require(not any(p in dirty for p in targets), "Preserving unsaved owned content")
        existing = {p: assets.does_asset_exist(p) for p in targets}
        require(apply or all(existing.values()), "Missing Intro3 assets; use explicit authoring flag")
        for p in targets:
            if existing[p]:
                require(u.EditorAssetLibrary.get_metadata_tag(assets.load_asset(p), META) == MARKER, "Preserving another author: " + p)
        reward_path = "/Game/Blueprints/GameData/DungeonReward/Progression/DA_DungeonReward_Intro3"
        reward = assets.load_asset(reward_path)
        ore = assets.load_asset("/Game/Blueprints/GameData/Item/Progression/DA_Item_ProgressionOre")
        report["existing_reward"] = materials.inspect_reward(u, reward, reward_path, ore)
        def native(name):
            result = u.load_class(None, "/Script/Project_RPG." + name)
            require(result, "Missing native class: " + name); return result
        def save(asset, package):
            require(not existing[package], "Existing assets are read-only")
            u.EditorAssetLibrary.set_metadata_tag(asset, META, MARKER)
            require(assets.save_loaded_asset(asset, only_if_is_dirty=False), "Save failed: " + package)
            report["saved"].append(package)
        def blueprint(package, parent):
            if existing[package]: return assets.load_asset(package)
            factory = u.BlueprintFactory(); factory.set_editor_property("parent_class", parent)
            result = u.AssetToolsHelpers.get_asset_tools().create_asset(package.rsplit("/",1)[1], package.rsplit("/",1)[0], u.Blueprint, factory)
            require(result, "Blueprint creation failed"); return result
        pawn = assets.load_asset(PAWN) if existing[PAWN] else assets.duplicate_asset("/Game/Blueprints/Character/Player/BP_Player", PAWN)
        require(pawn, "Player recipe unavailable")
        if not existing[PAWN]:
            u.BlueprintEditorLibrary.reparent_blueprint(pawn, native("RPGIntroRescuePlayer"))
            tools.compile_blueprint(u, pawn); save(pawn, PAWN)
        require(pawn.get_blueprint_parent_class() == native("RPGIntroRescuePlayer"), "Player parent differs")
        from editor_toolset.toolsets.actor import ActorTools
        pawn_defaults = u.get_default_object(pawn.generated_class())
        require(sum(c.get_class() == native("RPGIntroRescueObjectives") for c in ActorTools.get_components(pawn_defaults)) == 1, "Exactly one rescue objective required")
        target = blueprint(TARGET, native("RPGIntroRescuePawn"))
        if not existing[TARGET]:
            source = assets.load_asset("/Game/Blueprints/Character/NPC/Gruntling/Guardian/BP_Gruntling_Guardian")
            source = u.get_default_object(source.generated_class()); defaults = u.get_default_object(target.generated_class())
            defaults.set_editor_property("character_start_up_data", source.get_editor_property("character_start_up_data"))
            mesh, recipe = defaults.get_editor_property("mesh"), source.get_editor_property("mesh")
            mesh.set_skeletal_mesh_asset(recipe.get_editor_property("skeletal_mesh_asset"))
            mesh.set_anim_instance_class(recipe.get_editor_property("anim_class"))
            for prop in ("relative_location", "relative_rotation", "relative_scale3d"):
                mesh.set_editor_property(prop, recipe.get_editor_property(prop))
            tools.compile_blueprint(u, target); save(target, TARGET)
        require(target.get_blueprint_parent_class() == native("RPGIntroRescuePawn"), "Target parent differs")
        target_defaults = u.get_default_object(target.generated_class())
        require(str(target_defaults.get_editor_property("character_start_up_data")) not in ("None", ""), "Target startup recipe required")
        require(target_defaults.get_editor_property("mesh").get_editor_property("skeletal_mesh_asset"), "Target visible mesh required")
        require(target_defaults.get_editor_property("ai_controller_class") == u.AIController.static_class(), "Target must preserve stationary native controller")
        server = assets.load_asset(SERVER) if existing[SERVER] else u.AssetToolsHelpers.get_asset_tools().create_asset(SERVER.rsplit("/",1)[1], SERVER.rsplit("/",1)[0], u.RPGDungeonServerDefinition, u.DataAssetFactory())
        if not existing[SERVER]:
            server.set_editor_property("dungeon_id", "Progression.Intro3")
            require(u.ToolsetLibrary.set_object_properties(server, json.dumps({"DungeonMap": MAP + ".L_ProgressionIntro3"})), "Map assignment failed")
            server.set_editor_property("clear_rewards_by_difficulty", {u.RPGGameDifficulty.NORMAL: reward}); save(server, SERVER)
        require(server.get_dungeon_map_package() == MAP and server.get_editor_property("dungeon_id") == "Progression.Intro3", "Server identity/map differs")
        require(dict(server.get_editor_property("clear_rewards_by_difficulty")) == {u.RPGGameDifficulty.NORMAL: reward}, "Reward policy differs")
        mode = blueprint(MODE, native("RPGIntroRescueGameMode"))
        defaults = u.get_default_object(mode.generated_class())
        if not existing[MODE]:
            source = u.get_default_object(assets.load_asset("/Game/Blueprints/GameMode/BP_GameModeBase").generated_class())
            for prop in ("player_controller_class", "player_state_class", "game_state_class", "hud_class"):
                defaults.set_editor_property(prop, source.get_editor_property(prop))
            for prop, value in {"default_pawn_class": pawn.generated_class(), "practice_pawn_class": target.generated_class(), "bGiveReward": True,
                                "current_game_difficulty": u.RPGGameDifficulty.NORMAL, "dungeon_server_definitions": [server]}.items():
                defaults.set_editor_property(prop, value)
            tools.compile_blueprint(u, mode); save(mode, MODE)
        require(defaults.get_editor_property("practice_pawn_class") == target.generated_class() and defaults.get_editor_property("default_pawn_class") == pawn.generated_class(), "Mode targets differ")
        require(defaults.get_editor_property("require_backend_join_ticket") and defaults.get_editor_property("require_steam_identity_match") and defaults.get_editor_property("bGiveReward"), "Authority policy differs")
        require(list(defaults.get_editor_property("dungeon_server_definitions")) == [server] and defaults.get_editor_property("current_game_difficulty") == u.RPGGameDifficulty.NORMAL, "Pinned server catalog differs")
        arena.MAP, arena.MARKER, arena.METADATA, arena.PREFIX = MAP, MARKER, META, "Intro3_"
        arena.GEOMETRY = {k: v for k, v in arena.GEOMETRY.items() if k.startswith("Wall") or k == "Floor"}
        arena.STARTS = {"PlayerStart"+str(i): (-800, (i-1)*250, 110) for i in range(4)}
        arena.GEOMETRY["SafetyPad"] = ("Cylinder", (2000,1400,3), (3.6,3.6,.06))
        arena.ZONES = {}; arena.SIGNS = {"TargetSign": ("01  STAGGER 100", (600,0,300), (1,.75,.2)),
            "ProtectionSign": ("02  PROTECT 45s / 2 WAVES", (1400,0,370), (.3,.8,1)),
            "SafetySign": ("03  SAFETY - ENTER AFTER DEFENSE", (2400,1400,250), (.3,1,.4))}
        meshes = {name: assets.load_asset("/Engine/BasicShapes/"+name) for name in ("Cube","Cylinder")}
        world = u.EditorLoadingAndSavingUtils.load_map(MAP) if existing[MAP] else arena.create(u, meshes, mode.generated_class())
        if not existing[MAP]:
            # Python Rotator positional order differs from the C++ FRotator constructor.
            for sign in u.GameplayStatics.get_all_actors_of_class(world, u.TextRenderActor):
                if sign.get_actor_label().endswith("Sign"):
                    sign.set_actor_rotation(u.Rotator(pitch=0, yaw=180, roll=0), False)
            selector = u.get_editor_subsystem(u.EditorActorSubsystem).spawn_actor_from_class(u.RPGExperienceSelectionActor, u.Vector())
            selector.set_actor_label("Intro3_Experience"); selector.set_editor_property("experience", assets.load_asset(arena.EXPERIENCE))
            selector.set_editor_property("selection_mode", u.RPGExperienceSelectionMode.LOAD_EXPERIENCE); selector.set_editor_property("enabled", True)
            require(u.EditorLoadingAndSavingUtils.save_map(world, MAP), "Map save failed"); report["saved"].append(MAP)
        require(world.get_world_settings().get_editor_property("default_game_mode") == mode.generated_class(), "Map mode differs")
        starts = u.GameplayStatics.get_all_actors_of_class(world, u.PlayerStart)
        require(len(starts) == 4, "Four authored player starts required")
        signs = [a for a in u.GameplayStatics.get_all_actors_of_class(world, u.TextRenderActor) if a.get_actor_label() == "Intro3_TargetSign"]
        require(len(signs) == 1, "Single target sign required")
        rotation = signs[0].get_actor_rotation()
        require(abs(rotation.pitch) < .01 and abs(rotation.roll) < .01 and abs(abs(rotation.yaw)-180) < .01, "Target sign must face the approach upright")
        actors = u.GameplayStatics.get_all_actors_of_class(world, u.Actor)
        by_name = {a.get_actor_label(): a for a in actors if a.get_actor_label().startswith("Intro3_")}
        for name, (shape, location, scale) in arena.GEOMETRY.items():
            actor = by_name.get("Intro3_" + name)
            require(isinstance(actor, u.StaticMeshActor) and arena.close_vector(actor.get_actor_location(), location)
                    and arena.close_vector(actor.get_actor_scale3d(), scale) and actor.static_mesh_component.static_mesh == meshes[shape]
                    and str(actor.static_mesh_component.get_collision_profile_name()) == "BlockAll", "Arena geometry differs: " + name)
        require(not any(p in arena.dirty_packages(u) for p in targets), "Owned assets remain dirty")
        report.update({"passed": True, "map": MAP, "starts": len(starts), "target": TARGET, "gameplay_verified": False})
    except Exception as error:
        report["error"] = str(error); raise
    finally:
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")

if __name__ == "__main__":
    main()
