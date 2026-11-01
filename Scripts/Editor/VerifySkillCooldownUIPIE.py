"""Verify the new skill's HUD cooldown notification in a disposable editor.

Launch with -RPGSkillCooldownUIProbe, testmap and -ExecCmds="py <this file>".
Starts/stops only that process's PIE, never saves assets or changes a CDO.
Tests the real UI delegate and GAS effect, NOT rendered HUD or physical input.
"""
import json
import time
import unreal


if "-RPGSkillCooldownUIProbe" not in unreal.SystemLibrary.get_command_line():
    raise RuntimeError("Run this probe only in a dedicated disposable editor")

_STATE = {"phase": "startup", "deadline": time.monotonic() + 180.0,
          "handle": None, "owns_pie": False}
_RESULT = {}
_EVENTS = []
_UI = None
_ASC = None
_DEFINITION = None
_ABILITY_CLASS = None


def _check(condition, message):
    if not condition:
        raise AssertionError(message)


def _on_cooldown(tag, total, remaining):
    _EVENTS.append({"input_tag": str(tag), "total": total, "remaining": remaining})


def _finish(success, message):
    if _UI is not None:
        _UI.on_ability_cooldown_begin.remove_callable(_on_cooldown)
    unreal.unregister_slate_post_tick_callback(_STATE["handle"])
    result = dict(_RESULT, success=success, message=message, events=_EVENTS)
    (unreal.log if success else unreal.log_error)(
        "[RPG_SKILL_COOLDOWN_UI] " + json.dumps(result, sort_keys=True))
    unreal.SystemLibrary.quit_editor()


def _cooldown_effects():
    query = unreal.GameplayEffectQuery()
    query.set_editor_property("effect_definition", unreal.load_class(None, "/Script/Project_RPG.GE_CoolDown"))
    query.set_editor_property("effect_source", _DEFINITION)
    return list(_ASC.get_active_effects(query))


def _tick(_delta):
    global _UI, _ASC, _DEFINITION, _ABILITY_CLASS
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    try:
        _check(time.monotonic() < _STATE["deadline"], "Probe timed out in " + _STATE["phase"])
        if _STATE["phase"] == "startup":
            if unreal.AssetRegistryHelpers.get_asset_registry().is_loading_assets():
                return
            _check(not level.is_in_play_in_editor(), "Existing PIE is not owned by this probe")
            _check(editor.get_editor_world().get_name() == "testmap", "Expected testmap")
            level.editor_request_begin_play()
            _STATE.update(phase="starting", owns_pie=True)
        elif _STATE["phase"] == "starting":
            world = editor.get_game_world()
            if world is None:
                return
            pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
            controller = unreal.GameplayStatics.get_player_controller(world, 0)
            if pawn is None or controller is None:
                return
            _check(pawn.get_local_role() == unreal.NetRole.ROLE_AUTHORITY,
                   "Probe requires standalone authoritative PIE")
            _ASC = pawn.get_component_by_class(unreal.load_class(None, "/Script/Project_RPG.RPGAbilitySystemComponent"))
            _UI = pawn.get_component_by_class(unreal.load_class(None, "/Script/Project_RPG.PlayerUIComponent"))
            _check(_ASC is not None and _UI is not None, "Player ASC/UI component missing")
            _DEFINITION = unreal.load_asset("/Game/Blueprints/GameData/Skill/DA_Skill_ChargeSlash")
            _ABILITY_CLASS = unreal.load_class(None,
                "/Game/Blueprints/Character/Player/Abilities/Skill/GA_Player_ChargeSlash.GA_Player_ChargeSlash_C")
            _check(_DEFINITION is not None and _ABILITY_CLASS is not None, "Authored skill missing")
            _UI.on_ability_cooldown_begin.add_callable(_on_cooldown)
            # One disposable grant makes this independent of the weapon startup loadout.
            _ASC.give_ability(_ABILITY_CLASS, 1, -1)
            _STATE.update(phase="activate", settle_at=time.monotonic() + 2.0)
        elif _STATE["phase"] == "activate":
            if time.monotonic() < _STATE["settle_at"]:
                return
            _check(not _cooldown_effects(), "Fixture already has this skill's cooldown")
            accepted = _ASC.try_activate_ability_by_class(_ABILITY_CLASS, False)
            effects = _cooldown_effects()
            _RESULT.update(activation_accepted=accepted, cooldown_effects=len(effects))
            _check(accepted and len(effects) == 1, "Ability did not commit exactly one Definition cooldown")
            duration = unreal.AbilitySystemBlueprintLibrary.get_active_gameplay_effect_total_duration(effects[0])
            _RESULT["effect_duration"] = duration
            _check(len(_EVENTS) == 1, "Committed new skill must notify the local HUD exactly once")
            _check(abs(_EVENTS[0]["total"] - duration) < 0.01, "HUD total differs from actual GAS duration")
            _check(0 < _EVENTS[0]["remaining"] <= duration + 0.01, "Invalid HUD remaining duration")
            _check("InputTag." in _EVENTS[0]["input_tag"], "HUD received no input-slot identity")
            _check(not _ASC.try_activate_ability_by_class(_ABILITY_CLASS, False), "Immediate reinput should be blocked")
            _check(len(_EVENTS) == 1 and len(_cooldown_effects()) == 1,
                   "Blocked reinput must neither notify nor duplicate the cooldown")
            level.editor_request_end_play()
            _STATE["phase"] = "ending"
        elif _STATE["phase"] in ("ending", "failed"):
            if level.is_in_play_in_editor() or editor.get_game_world() is not None:
                return
            _finish(_STATE["phase"] == "ending", _STATE.get("failure", "Local HUD cooldown notification verified"))
    except Exception as error:
        _STATE.update(phase="failed", failure=str(error))
        if _STATE["owns_pie"] and level.is_in_play_in_editor():
            level.editor_request_end_play()
        else:
            _finish(False, str(error))


_STATE["handle"] = unreal.register_slate_post_tick_callback(_tick)
