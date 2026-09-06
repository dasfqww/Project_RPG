"""Migrate the four legacy player skills to policy-driven skill definitions.

Run a read-only preflight first (the default):
    UnrealEditor-Cmd.exe Project_RPG.uproject -run=pythonscript \
        -script=Scripts/Editor/MigrateLegacySkillsToDefinitions.py ...

Apply only after the preflight succeeds:
    Add -RPGApplyLegacySkillMigration to the command line above.

The script is intentionally narrow and idempotent. It only manages the named
definitions below and refuses to reparent a legacy Blueprint when its local
ability-event graph contains connected logic.
"""

import unreal


DEFINITION_ROOT = "/Game/Blueprints/GameData/Skill"
SKILL_DATA_TABLE = "/Game/Blueprints/GameData/Skill/DT_Skill"
APPLY_SWITCH = "-RPGApplyLegacySkillMigration"
CONTAINER_CLASS_PATH = "/Script/Project_RPG.RPGGameplayAbility_SkillContainer"

COMMON_ABILITY_PROPERTIES = (
    "ability_activation_policy",
    "activation_policy",
    "aoe_trace_type",
    "camera_mode_class",
    "current_attack_type_tag",
    "damage_effect_class",
    "description",
    "event_tag",
    "gain_identity_effect_class",
    "hit_effect",
    "hit_react_tag",
    "icon",
    "input_tag",
    "legacy_maximum_damage_per_hit",
    "legacy_maximum_targets_per_damage_event",
    "legacy_server_direct_hit_distance",
    "legacy_server_hit_location_tolerance",
    "name",
    "skill_name",
    "use_legacy_manual_mana_cost",
    "weapon_hit_sound_cue_tag",
)

SKILLS = (
    {
        "ability": (
            "/Game/Blueprints/Character/Player/Abilities/Skill/"
            "GA_PlayerSkill_AssultBlade"
        ),
        "legacy_parent": "RPGInstantSkillAbility",
        "definition": "DA_Skill_AssultBlade",
        "row": "AssultBlade",
        "display_name": "Assult Blade",
        "tag": "Player.Ability.Skill.AssultBlade",
        "execution": "instant",
        "section": "AssultBlade",
        "targeting": "soft",
        "target_range": 1000.0,
        "shape_radius": 500.0,
        "shape_angle": 100.0,
    },
    {
        "ability": (
            "/Game/Blueprints/Character/Player/Abilities/Skill/"
            "GA_Player_JumpSmash"
        ),
        "legacy_parent": "RPGInstantSkillAbility",
        "definition": "DA_Skill_JumpSmash",
        "row": "JumpSmash",
        "display_name": "Jump Smash",
        "tag": "Player.Ability.Skill.JumpSmash",
        "execution": "instant",
        "section": "JumpSmash",
        "targeting": "ground",
        "target_range": 1800.0,
        "shape_radius": 450.0,
        "shape_angle": 360.0,
    },
    {
        "ability": (
            "/Game/Blueprints/Character/Player/Abilities/Skill/"
            "GA_Player_ChargeSlash"
        ),
        "legacy_parent": "RPGChargeSkillAbility",
        "definition": "DA_Skill_ChargeSlash",
        "row": "ChargeSlash",
        "display_name": "Charge Slash",
        "tag": "Player.Ability.Skill.Charge",
        "execution": "charge",
        "charge_section": "Prepare",
        "release_section": "Attack",
        "charge_time_per_level": 0.5,
        "max_charge_level": 3,
        "max_charge_hold_time": 3.0,
        "targeting": "camera",
        "target_range": 2500.0,
        "shape_radius": 650.0,
        "shape_angle": 120.0,
    },
    {
        "ability": (
            "/Game/Blueprints/Character/Player/Abilities/Skill/"
            "GA_Player_WhirlWind"
        ),
        "legacy_parent": "RPGToggleSkillAbility",
        "definition": "DA_Skill_WhirlWind",
        "row": "WhirlWind",
        "display_name": "Whirl Wind",
        "tag": "Player.Ability.Skill.WhirlWind",
        # Legacy toggle is a hold-to-channel skill in a third-person layout:
        # releasing at any time exits through End, and four seconds auto-exits.
        "execution": "holding_channel",
        "holding_section": "Default",
        "success_section": "End",
        "hold_duration": 4.0,
        "targeting": "camera",
        "target_range": 800.0,
        "shape_radius": 375.0,
        "shape_angle": 360.0,
    },
)


def _require_asset(editor_assets, path, expected_type=None):
    asset = editor_assets.load_asset(path)
    if asset is None:
        raise RuntimeError("Required asset is missing: {}".format(path))
    if expected_type is not None and not isinstance(asset, expected_type):
        raise RuntimeError(
            "Asset {} is {}, expected {}".format(
                path,
                asset.get_class().get_name(),
                expected_type.__name__,
            )
        )
    return asset


def _connected_graph_nodes(blueprint):
    connected = []
    for graph in unreal.BlueprintEditorLibrary.list_graphs(blueprint):
        graph_editor = unreal.BlueprintGraphEditor.get_graph_editor(graph)
        for node in graph_editor.list_all_nodes():
            if any(pin.list_connected_pins() for pin in node.list_all_pins()):
                connected.append(
                    "{}:{}".format(graph.get_name(), node.get_node_title())
                )
    return connected


def _capture_common_properties(default_object):
    captured = {}
    for property_name in COMMON_ABILITY_PROPERTIES:
        try:
            captured[property_name] = default_object.get_editor_property(
                property_name
            )
        except Exception:
            # Engine-version-specific inherited properties are optional here.
            pass
    return captured


def _gameplay_tag(tag_name):
    tag = unreal.GameplayTag()
    if not tag.import_text('(TagName="{}")'.format(tag_name)):
        raise RuntimeError("Failed to construct gameplay tag: {}".format(tag_name))
    return tag


def _preflight(editor_assets, container_class):
    plans = []
    for skill in SKILLS:
        blueprint = _require_asset(
            editor_assets, skill["ability"], unreal.Blueprint
        )
        parent_class = blueprint.get_blueprint_parent_class()
        parent_name = parent_class.get_name() if parent_class else "None"
        already_migrated = parent_class == container_class
        if not already_migrated and parent_name != skill["legacy_parent"]:
            raise RuntimeError(
                "Unexpected parent for {}: {} (expected {} or {})".format(
                    skill["ability"],
                    parent_name,
                    skill["legacy_parent"],
                    container_class.get_name(),
                )
            )

        connected_nodes = _connected_graph_nodes(blueprint)
        if not already_migrated and connected_nodes:
            raise RuntimeError(
                "Refusing to reparent {} because local graph logic exists: {}"
                .format(skill["ability"], connected_nodes)
            )

        generated_class = blueprint.generated_class()
        if generated_class is None:
            raise RuntimeError(
                "Blueprint has no generated class: {}".format(skill["ability"])
            )
        default_object = unreal.get_default_object(generated_class)
        plans.append(
            {
                "skill": skill,
                "blueprint": blueprint,
                "already_migrated": already_migrated,
                "common_properties": _capture_common_properties(default_object),
            }
        )
        unreal.log(
            "RPG skill migration preflight: {} parent={} connected_logic=0"
            .format(skill["ability"], parent_name)
        )
    return plans


def _create_or_load_definition(editor_assets, asset_tools, definition_class, name):
    asset_path = "{}/{}".format(DEFINITION_ROOT, name)
    if editor_assets.does_asset_exist(asset_path):
        definition = editor_assets.load_asset(asset_path)
        if definition is None or definition.get_class() != definition_class:
            raise RuntimeError(
                "Existing definition has the wrong type: {}".format(asset_path)
            )
        return definition, asset_path

    definition = asset_tools.create_asset(
        name,
        DEFINITION_ROOT,
        definition_class,
        unreal.DataAssetFactory(),
    )
    if definition is None:
        raise RuntimeError("Failed to create definition: {}".format(asset_path))
    return definition, asset_path


def _execution_config(skill):
    execution = skill["execution"]
    library = unreal.RPGSkillConfigBlueprintLibrary
    if execution == "instant":
        config = unreal.RPGSkillInstantExecutionConfig(
            start_section=unreal.Name(skill["section"])
        )
        return (
            unreal.load_class(
                None,
                "/Script/Project_RPG.RPGSkillExecutionPolicy_Instant",
            ),
            library.make_instant_execution_config(config),
        )
    if execution == "charge":
        config = unreal.RPGSkillChargeExecutionConfig(
            charge_time_per_level=skill["charge_time_per_level"],
            max_charge_level=skill["max_charge_level"],
            max_charge_hold_time=skill["max_charge_hold_time"],
            minimum_release_level=0,
            charge_section=unreal.Name(skill["charge_section"]),
            release_section=unreal.Name(skill["release_section"]),
        )
        return (
            unreal.load_class(
                None,
                "/Script/Project_RPG.RPGSkillExecutionPolicy_Charge",
            ),
            library.make_charge_execution_config(config),
        )
    if execution == "holding_channel":
        config = unreal.RPGSkillHoldingExecutionConfig(
            hold_duration=skill["hold_duration"],
            perfect_zone_start_time=0.0,
            perfect_zone_end_time=skill["hold_duration"],
            auto_release_at_perfect_zone_end=True,
            holding_section=unreal.Name(skill["holding_section"]),
            success_section=unreal.Name(skill["success_section"]),
            failure_section=unreal.Name(skill["success_section"]),
        )
        return (
            unreal.load_class(
                None,
                "/Script/Project_RPG.RPGSkillExecutionPolicy_Holding",
            ),
            library.make_holding_execution_config(config),
        )
    raise RuntimeError("Unsupported execution migration: {}".format(execution))


def _targeting_config(skill):
    targeting = skill["targeting"]
    library = unreal.RPGSkillConfigBlueprintLibrary
    if targeting == "soft":
        config = unreal.RPGSkillSoftTargetingConfig(
            max_range=skill["target_range"],
            assist_angle_degrees=50.0,
            vertical_half_height=350.0,
            prefer_locked_target=True,
            fallback_to_camera_direction=True,
        )
        return (
            unreal.load_class(
                None,
                "/Script/Project_RPG.RPGSkillTargetingPolicy_SoftTarget",
            ),
            library.make_soft_targeting_config(config),
        )
    if targeting == "ground":
        config = unreal.RPGSkillGroundPointTargetingConfig(
            max_range=skill["target_range"],
            ground_trace_height=1200.0,
            ground_trace_depth=3500.0,
            require_ground_hit=True,
        )
        return (
            unreal.load_class(
                None,
                "/Script/Project_RPG.RPGSkillTargetingPolicy_GroundPoint",
            ),
            library.make_ground_point_targeting_config(config),
        )

    config = unreal.RPGSkillCameraDirectionTargetingConfig(
        max_range=skill["target_range"],
        use_locked_target_first=True,
        flatten_aim_direction=True,
        require_blocking_hit=False,
    )
    return (
        unreal.load_class(
            None,
            "/Script/Project_RPG.RPGSkillTargetingPolicy_CameraDirection",
        ),
        library.make_camera_direction_targeting_config(config),
    )


def _configure_query_and_security(definition, skill):
    profile = definition.get_editor_property("targeting_profile")
    shape = profile.get_editor_property("shape")
    shape.set_editor_property("radius", skill["shape_radius"])
    shape.set_editor_property("half_height", 250.0)
    shape.set_editor_property("angle_degrees", skill["shape_angle"])
    profile.set_editor_property("shape", shape)
    query_filter = profile.get_editor_property("filter")
    query_filter.set_editor_property("max_results", 16)
    profile.set_editor_property("filter", query_filter)
    definition.set_editor_property("targeting_profile", profile)

    security = definition.get_editor_property("security_profile")
    security.set_editor_property(
        "maximum_server_hit_distance", skill["target_range"] + 300.0
    )
    security.set_editor_property("maximum_targets_per_query", 16)
    definition.set_editor_property("security_profile", security)


def _configure_definition(definition, skill, data_table, legacy_cdo):
    execution_policy, execution_config = _execution_config(skill)
    targeting_policy, targeting_config = _targeting_config(skill)

    montage_property = (
        "single_section_data"
        if skill["execution"] == "instant"
        else "multiple_section_data"
    )
    legacy_montage_data = legacy_cdo.get_editor_property(montage_property)
    montage = legacy_montage_data.get_editor_property("montage_to_play")
    if montage is None:
        raise RuntimeError("Legacy skill has no montage: {}".format(skill["ability"]))

    definition.modify()
    definition.set_editor_property("skill_name", skill["display_name"])
    definition.set_editor_property(
        "skill_tag",
        _gameplay_tag(skill["tag"]),
    )
    definition.set_editor_property(
        "skill_data_handle",
        unreal.DataTableRowHandle(
            data_table=data_table,
            row_name=unreal.Name(skill["row"]),
        ),
    )
    definition.set_editor_property("skill_montage", montage)
    definition.set_editor_property("base_cooldown", 1.0)
    definition.set_editor_property("default_execution_policy_class", execution_policy)
    definition.set_editor_property("default_execution_config", execution_config)
    definition.set_editor_property("default_targeting_policy_class", targeting_policy)
    definition.set_editor_property("default_targeting_config", targeting_config)
    _configure_query_and_security(definition, skill)

    if skill["execution"] == "charge":
        definition.set_editor_property(
            "skill_vfx", legacy_cdo.get_editor_property("charge_ns")
        )


def _validate_definition(definition, definition_path):
    validator = unreal.get_editor_subsystem(unreal.EditorValidatorSubsystem)
    result, errors, warnings = validator.is_object_valid(
        definition,
        unreal.DataValidationUsecase.SCRIPT,
    )
    for warning in warnings:
        unreal.log_warning("{}: {}".format(definition_path, warning))
    if result != unreal.DataValidationResult.VALID:
        raise RuntimeError(
            "Skill definition validation failed for {}: {}".format(
                definition_path,
                [str(error) for error in errors],
            )
        )


def _restore_common_properties(default_object, captured):
    for property_name, value in captured.items():
        try:
            default_object.set_editor_property(property_name, value)
        except Exception as error:
            raise RuntimeError(
                "Failed to preserve ability property {}: {}".format(
                    property_name, error
                )
            )


def _apply(plans, editor_assets, asset_tools, definition_class, container_class):
    data_table = _require_asset(editor_assets, SKILL_DATA_TABLE, unreal.DataTable)
    definitions = {}

    # Definitions are fully authored before any gameplay-ability class changes.
    for plan in plans:
        skill = plan["skill"]
        blueprint = plan["blueprint"]
        legacy_cdo = unreal.get_default_object(blueprint.generated_class())
        definition, definition_path = _create_or_load_definition(
            editor_assets,
            asset_tools,
            definition_class,
            skill["definition"],
        )
        _configure_definition(definition, skill, data_table, legacy_cdo)
        _validate_definition(definition, definition_path)
        if not editor_assets.save_asset(definition_path, only_if_is_dirty=False):
            raise RuntimeError(
                "Failed to save skill definition: {}".format(definition_path)
            )
        definitions[skill["definition"]] = definition
        unreal.log("Authored RPG skill definition: {}".format(definition_path))

    for plan in plans:
        skill = plan["skill"]
        blueprint = plan["blueprint"]
        if not plan["already_migrated"]:
            unreal.BlueprintEditorLibrary.reparent_blueprint(
                blueprint, container_class
            )
        unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)

        generated_class = blueprint.generated_class()
        if generated_class is None:
            raise RuntimeError(
                "Reparented Blueprint has no generated class: {}".format(
                    skill["ability"]
                )
            )
        default_object = unreal.get_default_object(generated_class)
        _restore_common_properties(default_object, plan["common_properties"])
        default_object.set_editor_property(
            "skill_definition", definitions[skill["definition"]]
        )
        # Existing content has no GAS CostGameplayEffect yet. Preserve its data-
        # table mana path until cost effects are migrated in a separate change.
        default_object.set_editor_property("use_legacy_manual_mana_cost", True)
        blueprint.modify()
        unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
        if not editor_assets.save_asset(
            skill["ability"], only_if_is_dirty=False
        ):
            raise RuntimeError(
                "Failed to save migrated ability: {}".format(skill["ability"])
            )
        unreal.log("Migrated RPG skill ability: {}".format(skill["ability"]))


def main():
    editor_assets = unreal.EditorAssetLibrary
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    definition_class = unreal.load_class(
        None, "/Script/Project_RPG.RPGSkillDefinition"
    )
    container_class = unreal.load_class(None, CONTAINER_CLASS_PATH)
    if definition_class is None or container_class is None:
        raise RuntimeError("Project RPG skill runtime classes are unavailable")

    plans = _preflight(editor_assets, container_class)
    command_line = unreal.SystemLibrary.get_command_line()
    if APPLY_SWITCH.lower() not in command_line.lower():
        unreal.log(
            "RPG skill migration preflight succeeded for {} skills. "
            "No assets changed; add {} to apply.".format(
                len(plans), APPLY_SWITCH
            )
        )
        return

    _apply(
        plans,
        editor_assets,
        asset_tools,
        definition_class,
        container_class,
    )
    unreal.log("RPG skill migration completed for {} skills.".format(len(plans)))


main()
