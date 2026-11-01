"""Read-only preflight of the real boss map and its authored multiplayer classes.

Run via UnrealEditor-Cmd -run=pythonscript -script=... -NullRHI.
Does not save packages, change map actors, or start PIE.
"""
import math
import unreal

MAP = "/Game/Maps/bossmap"
MODE = "/Game/Blueprints/GameMode/BP_BossBattleGameMode"
BOSS = "/Game/Blueprints/Character/NPC/Raid/SwordManCombo/BP_RaidSwordManCombo"


def required_class(path):
    cls = unreal.load_class(None, path)
    if cls is None:
        raise RuntimeError("Missing class: " + path)
    return cls


def required_blueprint(path):
    bp = unreal.EditorAssetLibrary.load_asset(path)
    if not isinstance(bp, unreal.Blueprint):
        raise RuntimeError("Missing Blueprint: " + path)
    return bp.generated_class()


def main():
    world = unreal.EditorLoadingAndSavingUtils.load_map(MAP)
    if world is None:
        raise RuntimeError("Cannot load raid map: " + MAP)
    mode_class = required_blueprint(MODE)
    configured_mode = world.get_world_settings().get_editor_property("default_game_mode")
    if configured_mode != mode_class:
        raise RuntimeError("bossmap is not configured with BP_BossBattleGameMode: " + str(configured_mode))
    mode = unreal.get_default_object(mode_class)
    weapon_ability = unreal.get_default_object(required_blueprint(
        "/Game/Blueprints/Character/Player/Abilities/GA_SpawnWeapon"))
    unreal.log_warning("RAIDMAP startup weapon NetExecutionPolicy=" +
                       str(weapon_ability.get_editor_property("net_execution_policy")))
    death_ability = unreal.get_default_object(required_blueprint(
        "/Game/Blueprints/Character/Player/Abilities/GA_Player_Death"))
    if death_ability.get_editor_property("net_execution_policy") != unreal.GameplayAbilityNetExecutionPolicy.SERVER_INITIATED:
        raise RuntimeError("Player death OwnedTagAdded trigger must execute on the server for remote players")
    if death_ability.get_editor_property("net_security_policy") != unreal.GameplayAbilityNetSecurityPolicy.SERVER_ONLY:
        raise RuntimeError("Clients must not be allowed to request or terminate player death")
    unreal.log_warning("RAIDMAP player death ServerInitiated=1")
    unreal.log_warning("RAIDMAP authored battle state=" + str(mode.get_editor_property("current_boss_battle_state")))
    # Inspect authored defaults without changing them. The native session uses
    # Tick for delayed player GAS readiness, late joins and wipe detection.
    tick = mode.get_editor_property("primary_actor_tick")
    unreal.log_warning("RAIDMAP tick defaults StartEnabled={} DedicatedAllowed={}".format(
        tick.get_editor_property("start_with_tick_enabled"),
        tick.get_editor_property("allow_tick_on_dedicated_server")))
    if mode.get_editor_property("boss_class") != required_blueprint(BOSS):
        raise RuntimeError("GameMode does not spawn the raid boss")
    boss_default = unreal.get_default_object(required_blueprint(BOSS))
    raid_controller = unreal.get_default_object(boss_default.get_editor_property("ai_controller_class"))
    perception = raid_controller.get_editor_property("NPCPerceptionComponent")
    sights = [sense for sense in perception.get_editor_property("senses_config")
              if isinstance(sense, unreal.AISenseConfig_Sight)] if perception else []
    sight_radius = sights[0].get_editor_property("sight_radius") if len(sights) == 1 else 0.0
    loss_radius = sights[0].get_editor_property("lose_sight_radius") if len(sights) == 1 else 0.0
    if not math.isfinite(sight_radius) or not math.isfinite(loss_radius) or sight_radius <= 0.0 or loss_radius < sight_radius:
        raise RuntimeError("Raid sight cannot retain detected targets; run ConfigureRaidSwordManPerception.py with its apply switch")
    unreal.log_warning("RAIDMAP retained sight radius verified")
    state_class = mode.get_editor_property("game_state_class")
    if state_class != required_class("/Script/Project_RPG.RPGBossBattleGameState"):
        raise RuntimeError("GameMode does not publish the native replicated boss battle state")
    controller_class = mode.get_editor_property("player_controller_class")
    pawn_class = mode.get_editor_property("default_pawn_class")
    if controller_class is None or pawn_class is None:
        raise RuntimeError("GameMode has no player controller or default pawn")
    if not unreal.MathLibrary.class_is_child_of(controller_class, required_class("/Script/Project_RPG.RPGPlayerController")):
        raise RuntimeError("Player controller cannot issue the validated retry RPC")
    if not unreal.MathLibrary.class_is_child_of(pawn_class, required_class("/Script/Project_RPG.RPGPlayer")):
        raise RuntimeError("Player pawn cannot join the native encounter")
    player = unreal.get_default_object(pawn_class)
    startup = player.get_editor_property("character_start_up_data")
    if startup is None:
        raise RuntimeError("Player startup data is missing")
    controller = unreal.get_default_object(controller_class)
    input_data = controller.get_editor_property("input_config_data_asset")
    if input_data is None or input_data.get_editor_property("default_mapping_context") is None:
        raise RuntimeError("Player input initialization is incomplete")
    defeat_class = controller.get_editor_property("boss_defeat_widget_class")
    if defeat_class is None:
        raise RuntimeError("Player controller defeat widget class is missing")
    if defeat_class != required_blueprint("/Game/Blueprints/UI/GameContents/BossBattle/WBP_DefeatWidget"):
        raise RuntimeError("Defeat widget differs from the RetryButton template covered by automation")
    # Verify the authored UI reference without constructing/firing gameplay UI.
    # The RetryButton template is checked by native ProjectRPG.Boss.Presentation.
    defeat_bp = unreal.EditorAssetLibrary.load_asset(defeat_class.get_path_name().removesuffix("_C"))
    if not isinstance(defeat_bp, unreal.WidgetBlueprint):
        raise RuntimeError("Defeat widget Blueprint is unavailable")
    actors = list(unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors())
    targets = [actor for actor in actors if isinstance(actor, unreal.TargetPoint)]
    starts = [actor for actor in actors if isinstance(actor, unreal.PlayerStart)]
    if len(targets) != 1:
        raise RuntimeError("Boss spawn must resolve unambiguously to one TargetPoint: " + str(len(targets)))
    if not starts:
        raise RuntimeError("Map has no PlayerStart")
    radius = mode.get_editor_property("participation_radius")
    for start in starts:
        distance = (start.get_actor_location() - targets[0].get_actor_location()).length()
        if distance > radius:
            raise RuntimeError("PlayerStart is outside encounter participation: " + start.get_name())
    unreal.log_warning("RAIDMAP configured GameMode/GAS player/retry controller/defeat widget verified; "
                       "TargetPoints={} PlayerStarts={} Pawn={} Controller={} Startup={}".format(
                           len(targets), len(starts), pawn_class.get_path_name(),
                           controller_class.get_path_name(), startup.get_path_name()))


main()
