"""Duplicate the existing SwordMan BP into a raid boss and wire the boss map.

Run with Unreal Editor closed, after building Project_RPGEditor:
    UnrealEditor-Cmd.exe Project_RPG.uproject -run=pythonscript \
        -script=Scripts/Editor/ConfigureRaidSwordMan.py \
        -RPGApplyRaidSwordMan -unattended -nop4 -nosplash -NullRHI

Without -RPGApplyRaidSwordMan this script only checks prerequisites.
The original BP_SwordMan is never modified.
"""

import unreal


SOURCE_BASE = "/Game/Blueprints/Character/NPC/BP_RPGNPCBase"
TARGET_BASE = "/Game/Blueprints/Character/NPC/BP_RaidNPCBase"
SOURCE = "/Game/Blueprints/Character/NPC/SwordMan/BP_SwordMan"
TARGET = "/Game/Blueprints/Character/NPC/SwordMan/BP_RaidSwordMan"
SUMMON_ENCOUNTER = "/Game/Blueprints/Character/NPC/Raid/SwordManSummonMechanic/BP_RaidSwordManSummonMechanic"
COMBO_ENCOUNTER = "/Game/Blueprints/Character/NPC/Raid/SwordManCombo/BP_RaidSwordManCombo"
DEFINITION = "/Game/Blueprints/Character/NPC/SwordMan/DA_RaidSwordMan"
GAME_MODE = "/Game/Blueprints/GameMode/BP_BossBattleGameMode"
APPLY_SWITCH = "-RPGApplyRaidSwordMan"


def require_asset(path, expected_type):
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if asset is None or not isinstance(asset, expected_type):
        raise RuntimeError("Missing or incompatible asset: {}".format(path))
    return asset


def require_class(path):
    cls = unreal.load_class(None, path)
    if cls is None:
        raise RuntimeError("Required native class is unavailable: {}".format(path))
    return cls


def save(path):
    if not unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False):
        raise RuntimeError("Could not save: {}".format(path))


def main():
    source_base = require_asset(SOURCE_BASE, unreal.Blueprint)
    source = require_asset(SOURCE, unreal.Blueprint)
    game_mode = require_asset(GAME_MODE, unreal.Blueprint)
    boss_parent = require_class("/Script/Project_RPG.RPGBossCharacter")
    npc_parent = require_class("/Script/Project_RPG.RPGNonPlayerCharacter")
    state_class = require_class("/Script/Project_RPG.RPGBossBattleGameState")
    definition_class = require_class("/Script/Project_RPG.RPGBossDefinition")

    base_parent = source_base.get_blueprint_parent_class()
    if base_parent is None or base_parent.get_path_name() != npc_parent.get_path_name():
        raise RuntimeError("BP_RPGNPCBase is no longer based on RPGNonPlayerCharacter")
    source_parent = source.get_blueprint_parent_class()
    if source_parent is None or source_parent.get_path_name() != source_base.generated_class().get_path_name():
        raise RuntimeError("BP_SwordMan is no longer based on BP_RPGNPCBase")
    if unreal.EditorAssetLibrary.does_asset_exist(TARGET_BASE):
        raid_base = require_asset(TARGET_BASE, unreal.Blueprint)
        if raid_base.get_blueprint_parent_class().get_path_name() != boss_parent.get_path_name():
            raise RuntimeError("Existing raid NPC base has an unexpected parent")
    if unreal.EditorAssetLibrary.does_asset_exist(TARGET):
        target = require_asset(TARGET, unreal.Blueprint)
        raid_base = require_asset(TARGET_BASE, unreal.Blueprint)
        if target.get_blueprint_parent_class().get_path_name() != raid_base.generated_class().get_path_name():
            raise RuntimeError("Existing raid boss has an unexpected parent")
    if unreal.EditorAssetLibrary.does_asset_exist(DEFINITION):
        require_asset(DEFINITION, unreal.RPGBossDefinition)

    tag = unreal.GameplayTag()
    tag_two = unreal.GameplayTag()
    if (not tag.import_text('(TagName="Boss.Phase.One")')
            or not tag_two.import_text('(TagName="Boss.Phase.Two")')):
        raise RuntimeError("Raid boss phase gameplay tags are unavailable")

    if APPLY_SWITCH.lower() not in unreal.SystemLibrary.get_command_line().lower():
        if unreal.EditorAssetLibrary.does_asset_exist(TARGET):
            target = require_asset(TARGET, unreal.Blueprint)
            definition = require_asset(DEFINITION, unreal.RPGBossDefinition)
            boss_default = unreal.get_default_object(target.generated_class())
            boss_component = boss_default.get_editor_property("boss_ai_component")
            configured_definition = (
                boss_component.get_editor_property("definition")
                if boss_component else None
            )
            game_mode_default = unreal.get_default_object(game_mode.generated_class())
            configured_boss = game_mode_default.get_editor_property("boss_class")
            configured_state = game_mode_default.get_editor_property("game_state_class")
            if (configured_definition != definition
                    or configured_boss is None
                    or configured_boss.get_path_name() not in (
                        target.generated_class().get_path_name(), SUMMON_ENCOUNTER + ".BP_RaidSwordManSummonMechanic_C",
                        COMBO_ENCOUNTER + ".BP_RaidSwordManCombo_C")
                    or configured_state.get_path_name() != state_class.get_path_name()):
                raise RuntimeError("Saved raid boss or GameMode defaults do not match")
            phases = list(definition.get_editor_property("phases"))
            if (len(phases) != 2
                    or phases[0].get_editor_property("health_threshold") != 1.0
                    or abs(phases[1].get_editor_property("health_threshold") - 0.7) > 0.0001):
                raise RuntimeError("Saved raid boss must have 100% and 70% phases")
            unreal.log_warning("Raid SwordMan saved asset verification passed")
        else:
            unreal.log_warning("Raid SwordMan preflight passed; no assets changed")
        return

    editor_assets = unreal.EditorAssetLibrary
    if editor_assets.does_asset_exist(TARGET_BASE):
        raid_base = require_asset(TARGET_BASE, unreal.Blueprint)
    else:
        raid_base = editor_assets.duplicate_asset(SOURCE_BASE, TARGET_BASE)
        if raid_base is None:
            raise RuntimeError("Could not duplicate BP_RPGNPCBase")
        unreal.BlueprintEditorLibrary.reparent_blueprint(raid_base, boss_parent)
        unreal.BlueprintEditorLibrary.compile_blueprint(raid_base)
        if raid_base.get_blueprint_parent_class().get_path_name() != boss_parent.get_path_name():
            raise RuntimeError("Raid NPC base reparenting failed")
        save(TARGET_BASE)

    if editor_assets.does_asset_exist(TARGET):
        target = require_asset(TARGET, unreal.Blueprint)
    else:
        target = editor_assets.duplicate_asset(SOURCE, TARGET)
        if target is None:
            raise RuntimeError("Could not duplicate BP_SwordMan")
        unreal.BlueprintEditorLibrary.reparent_blueprint(
            target, raid_base.generated_class()
        )
        unreal.BlueprintEditorLibrary.compile_blueprint(target)
        if target.get_blueprint_parent_class().get_path_name() != raid_base.generated_class().get_path_name():
            raise RuntimeError("Raid boss reparenting failed")

    if editor_assets.does_asset_exist(DEFINITION):
        definition = require_asset(DEFINITION, unreal.RPGBossDefinition)
    else:
        definition = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "DA_RaidSwordMan",
            "/Game/Blueprints/Character/NPC/SwordMan",
            definition_class,
            unreal.DataAssetFactory(),
        )
        if definition is None:
            raise RuntimeError("Could not create raid boss definition")
        phase = unreal.RPGBossPhaseDefinition(
            phase_tag=tag, health_threshold=1.0
        )
        phase_two = unreal.RPGBossPhaseDefinition(
            phase_tag=tag_two, health_threshold=0.7
        )
        definition.set_editor_property("phases", [phase, phase_two])
        save(DEFINITION)

    existing_phases = list(definition.get_editor_property("phases"))
    if len(existing_phases) == 1:
        if existing_phases[0].get_editor_property("health_threshold") != 1.0:
            raise RuntimeError("Existing single-phase raid definition was customized")
        phase_two = unreal.RPGBossPhaseDefinition(
            phase_tag=tag_two, health_threshold=0.7
        )
        definition.set_editor_property("phases", existing_phases + [phase_two])
        save(DEFINITION)

    boss_default = unreal.get_default_object(target.generated_class())
    boss_component = boss_default.get_editor_property("boss_ai_component")
    if boss_component is None:
        raise RuntimeError("Reparented boss has no BossAIComponent")
    boss_component.set_editor_property("definition", definition)
    target.modify()
    unreal.BlueprintEditorLibrary.compile_blueprint(target)
    save(TARGET)

    game_mode_default = unreal.get_default_object(game_mode.generated_class())
    # Re-running initial setup must preserve the subsequently selected mechanic encounter.
    configured_boss = game_mode_default.get_editor_property("boss_class")
    if configured_boss is None or configured_boss.get_path_name() not in (
            SUMMON_ENCOUNTER + ".BP_RaidSwordManSummonMechanic_C", COMBO_ENCOUNTER + ".BP_RaidSwordManCombo_C"):
        game_mode_default.set_editor_property("boss_class", target.generated_class())
    game_mode_default.set_editor_property("game_state_class", state_class)
    game_mode.modify()
    unreal.BlueprintEditorLibrary.compile_blueprint(game_mode)
    save(GAME_MODE)
    unreal.log("Raid SwordMan boss asset and boss battle GameMode configured")


main()
