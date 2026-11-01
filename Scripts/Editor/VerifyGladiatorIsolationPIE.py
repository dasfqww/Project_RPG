"""Verify current-map PIE in a fresh editor with GladiatorCore disabled.

Launch a dedicated editor with -ExecCmds="py <this file>" and /Game/Maps/testmap.
Does not save assets or alter gameplay data. Starts/stops only this editor's PIE,
then exits. This is a startup/PIE smoke check, not rendered skill input coverage.
"""
import json
import time
import unreal


_STATE = {"phase": "startup", "handle": None, "deadline": time.monotonic() + 180.0}
_RESULT = {}


def _check(condition, message):
    if not condition:
        raise RuntimeError(message)


def _plugin_isolated():
    _check("GladiatorCore" not in unreal.PluginBlueprintLibrary.get_enabled_plugin_names(),
           "GladiatorCore is still enabled")
    _check(not unreal.PluginBlueprintLibrary.is_plugin_mounted("GladiatorCore"),
           "GladiatorCore content is still mounted")


def _finish(success, message):
    unreal.unregister_slate_post_tick_callback(_STATE["handle"])
    result = dict(_RESULT, success=success, message=message)
    (unreal.log if success else unreal.log_error)(
        "[RPG_GLADIATOR_PIE] " + json.dumps(result, sort_keys=True))
    unreal.SystemLibrary.quit_editor()


def _tick(_delta):
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    try:
        if time.monotonic() > _STATE["deadline"]:
            raise RuntimeError("Timed out in phase " + _STATE["phase"])
        if _STATE["phase"] == "startup":
            if unreal.AssetRegistryHelpers.get_asset_registry().is_loading_assets():
                return
            _plugin_isolated()
            world = editor.get_editor_world()
            _check(world is not None and world.get_name() == "testmap", "Expected testmap editor world")
            _check(not level.is_in_play_in_editor(), "Refusing to interfere with an existing PIE session")
            _check(bool(level.get_viewport_config_keys()), "No editor viewport available for actual PIE")
            level.editor_request_begin_play()
            _STATE["phase"] = "starting_pie"
            unreal.log("[RPG_GLADIATOR_PIE] Requested real current-map PIE")
        elif _STATE["phase"] == "starting_pie":
            world = editor.get_game_world()
            if world is None or not level.is_in_play_in_editor():
                return
            controller = unreal.GameplayStatics.get_player_controller(world, 0)
            pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
            if controller is None or pawn is None:
                return
            _check(controller.get_controlled_pawn() == pawn, "Player pawn is not possessed")
            asc_class = unreal.load_class(None, "/Script/Project_RPG.RPGAbilitySystemComponent")
            skill_class = unreal.load_class(None, "/Script/Project_RPG.RPGPlayerSkillComponent")
            _check(pawn.get_component_by_class(asc_class) is not None, "Player ASC is missing")
            skill = pawn.get_component_by_class(skill_class)
            _check(skill is not None and skill.get_total_sp() > 0, "Skill component/SP is not ready")
            hud = controller.get_hud()
            _check(hud is not None, "Current HUD is missing")
            # The retired native class-selection types no longer exist. Inspect
            # actual widget assets without trying to load their removed parents.
            legacy_windows = [
                widget for widget in unreal.WidgetLibrary.get_all_widgets_of_class(
                    world, unreal.UserWidget, False)
                if widget.get_class().get_path_name().startswith("/GladiatorCore/")
            ]
            _check(not legacy_windows, "Legacy Gladiator widget was created on startup")
            _RESULT.update(pawn=pawn.get_class().get_path_name(),
                           controller=controller.get_class().get_path_name(),
                           hud=hud.get_class().get_path_name(), total_sp=skill.get_total_sp(),
                           gladiator_enabled=False, gladiator_mounted=False,
                           legacy_gladiator_widgets=len(legacy_windows))
            _STATE["phase"] = "running_pie"
            _STATE["settled_at"] = time.monotonic() + 5.0
        elif _STATE["phase"] == "running_pie":
            _check(level.is_in_play_in_editor(), "PIE ended unexpectedly")
            _plugin_isolated()
            if time.monotonic() < _STATE["settled_at"]:
                return
            level.editor_request_end_play()
            _STATE["phase"] = "ending_pie"
        elif _STATE["phase"] == "ending_pie":
            if level.is_in_play_in_editor() or editor.get_game_world() is not None:
                return
            _plugin_isolated()
            _finish(True, "Current-map PIE/player/GAS/skill/HUD startup and clean PIE shutdown verified")
        elif _STATE["phase"] == "failed":
            if level.is_in_play_in_editor():
                return
            _finish(False, _STATE["failure"])
    except Exception as error:
        if _STATE["phase"] == "failed":
            _finish(False, str(error))
        else:
            _STATE["failure"] = str(error)
            _STATE["phase"] = "failed"
            if level.is_in_play_in_editor():
                level.editor_request_end_play()


_STATE["handle"] = unreal.register_slate_post_tick_callback(_tick)
