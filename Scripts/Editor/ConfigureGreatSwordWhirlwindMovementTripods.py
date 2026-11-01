"""Author Whirlwind's level-four Mobile/Stationary choices.

Inspect by default. -RPGConfigureGreatSwordWhirlwindMovementTripods permits saving
only after the normal module contains holding movement and following queries.
Existing level-seven area choices and every existing array index are preserved.
"""
import copy
import json
import math


ASSET = "/Game/Blueprints/GameData/Skill/DA_Skill_WhirlWind"
FIELDS = ["SkillTag", "TripodTiers", "DefaultExecutionPolicyClass", "DefaultExecutionConfig",
          "DefaultTargetingPolicyClass", "DefaultTargetingConfig", "MaxSkillLevel"]


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def equivalent(actual, expected):
    if isinstance(expected, dict):
        return isinstance(actual, dict) and actual.keys() == expected.keys() and all(
            equivalent(actual[key], value) for key, value in expected.items())
    if isinstance(expected, list):
        return isinstance(actual, list) and len(actual) == len(expected) and all(
            equivalent(left, right) for left, right in zip(actual, expected))
    if isinstance(expected, float):
        return isinstance(actual, (int, float)) and math.isclose(actual, expected, rel_tol=1e-6, abs_tol=1e-6)
    return actual == expected


def build_configuration(properties):
    require(properties["SkillTag"]["tagName"] == "Player.Ability.Skill.WhirlWind", "Different skill identity")
    require(properties["DefaultExecutionPolicyClass"] == {
        "refPath": "/Script/Project_RPG.RPGSkillExecutionPolicy_Holding"}, "Review a different execution policy")
    require(properties["DefaultTargetingPolicyClass"] == {
        "refPath": "/Script/Project_RPG.RPGSkillTargetingPolicy_CameraDirection"}, "Review a different targeting policy")
    require(properties["MaxSkillLevel"] >= 4, "Level four is unavailable")
    execution = properties["DefaultExecutionConfig"]
    targeting = properties["DefaultTargetingConfig"]
    require(execution.get("_structType") == "/Script/Project_RPG.RPGSkillHoldingExecutionConfig",
            "Missing native holding data")
    require(targeting.get("_structType") == "/Script/Project_RPG.RPGSkillCameraDirectionTargetingConfig",
            "Missing native camera-direction data")
    tiers = copy.deepcopy(properties["TripodTiers"])
    matching = [tier for tier in tiers if tier.get("requiredSkillLevel") == 4]
    require(len(matching) == 1, "Author the reserved level-four row before movement choices")
    options = matching[0]["options"]
    changed = False
    recipes = [
        ("Mobile", "이동 회전", 0.8, 1.0, "회전 중 현재 이동 속도의 80%로 이동할 수 있다."),
        ("Stationary", "제자리 회전", 0.0, 2.8 / 2.4,
         "회전이 끝날 때까지 제자리를 유지하며 회전 피해가 이동 회전 대비 약 16.7% 증가한다."),
    ]
    for identity, label, speed, damage, description in recipes:
        config = copy.deepcopy(execution)
        config.update({"bConstrainMovement": True, "movementSpeedScalar": speed})
        aim = copy.deepcopy(targeting)
        aim["bFollowSourceForHitQuery"] = True
        expected = {
            "cooldownReductionSeconds": 0.0,
            "persistenceId": identity, "optionName": label, "description": description, "optionIcon": "None",
            "statModifiers": [{"statTag": {"tagName": "Shared.Stat.Damage"}, "scalarValue": damage}],
            "tripodTag": {"tagName": "None"}, "overrideActionClass": "None",
            "overrideExecutionPolicyClass": "None", "overrideExecutionConfig": config,
            "overrideTargetingPolicyClass": "None", "overrideTargetingConfig": aim,
            "overrideMontage": "None", "overrideVFX": "None",
        }
        existing = [option for option in options if option.get("persistenceId") == identity]
        require(len(existing) <= 1, "Duplicate persistent choice: " + identity)
        if existing:
            require(equivalent(existing[0], expected), "Preserving a customized choice: " + identity)
        else:
            options.append(expected)
            changed = True
    return {"TripodTiers": tiers}, changed


def main():
    import unreal
    from toolset_registry.helpers import require_editable

    require(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is None,
            "Finish PIE before editing skill content")
    assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    dirty = {str(package.get_name()) for package in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    require(ASSET not in dirty, "Target has unsaved changes; preserving it")
    asset = assets.load_asset(ASSET)
    require(isinstance(asset, unreal.RPGSkillDefinition), "Missing Whirlwind definition")
    properties = json.loads(unreal.ToolsetLibrary.get_object_properties(asset, FIELDS))
    update, changed = build_configuration(properties)
    apply = "-RPGConfigureGreatSwordWhirlwindMovementTripods" in unreal.SystemLibrary.get_command_line()
    supported = hasattr(unreal.RPGSkillHoldingExecutionConfig(), "movement_speed_scalar") and hasattr(
        unreal.RPGSkillCameraDirectionTargetingConfig(), "follow_source_for_hit_query")
    if apply:
        require(supported, "Load the normal module with holding movement and following queries before saving")
    if apply and changed:
        require_editable(asset)
        require(unreal.ToolsetLibrary.set_object_properties(asset, json.dumps(update, ensure_ascii=False)),
                "Could not set Whirlwind movement choices")
        readback = json.loads(unreal.ToolsetLibrary.get_object_properties(asset, FIELDS))
        _, needs_update = build_configuration(readback)
        require(not needs_update, "Readback differs; target was not saved")
        require(assets.save_loaded_asset(asset, only_if_is_dirty=False), "Could not save Whirlwind movement choices")
    unreal.log("RPG_GREATSWORD_WHIRLWIND_MOVEMENT " + json.dumps({
        "asset": ASSET, "apply": apply, "changed": apply and changed, "module_supports_movement": supported,
        "choices": ["Mobile", "Stationary"], "existing_indices_preserved": True,
        "defaults_blueprints_montages_security_modified": False,
    }))


if __name__ == "__main__":
    main()

