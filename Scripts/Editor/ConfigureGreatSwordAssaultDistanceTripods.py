"""Author the two distance-only first-slice Assault choices.

Inspect by default; -RPGConfigureGreatSwordAssaultDistanceTripods permits saving
after the normal module contains the scaled montage task. Legacy charge/combo
indices are preserved. Cooldown alternatives require a separate design decision.
"""
import copy
import json
import math


ASSET = "/Game/Blueprints/GameData/Skill/DA_Skill_AssultBlade"
FIELDS = ["SkillTag", "TripodTiers", "DefaultExecutionPolicyClass", "DefaultExecutionConfig",
          "DefaultTargetingPolicyClass", "DefaultTargetingConfig", "SecurityProfile",
          "SkillMontage", "BaseCooldown", "MaxSkillLevel"]


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
    require(properties["SkillTag"]["tagName"] == "Player.Ability.Skill.AssultBlade", "Different skill identity")
    require(properties["DefaultExecutionPolicyClass"] == {
        "refPath": "/Script/Project_RPG.RPGSkillExecutionPolicy_Instant"}, "Review a different default execution policy")
    require(properties["DefaultTargetingPolicyClass"] == {
        "refPath": "/Script/Project_RPG.RPGSkillTargetingPolicy_SoftTarget"}, "Review a different targeting policy")
    require(properties["MaxSkillLevel"] >= 7, "Level seven is unavailable")
    targeting = properties["DefaultTargetingConfig"]
    require(targeting.get("_structType") == "/Script/Project_RPG.RPGSkillSoftTargetingConfig",
            "Missing native soft-target data")

    security = copy.deepcopy(properties["SecurityProfile"])
    original = {"bEnabled": False, "durationSeconds": 0.5, "extraDistance": 600,
                "reason": "SkillMovement", "bResetBaselineWhenFinished": False}
    desired = copy.deepcopy(original)
    desired.update({"bEnabled": True, "durationSeconds": 10})
    require(equivalent(security["authorizedMovement"], original) or
            equivalent(security["authorizedMovement"], desired), "Preserving a customized movement authorization")
    changed = not equivalent(security["authorizedMovement"], desired)
    security["authorizedMovement"] = desired
    tiers = copy.deepcopy(properties["TripodTiers"])
    for level, identity, scalar, label, description in [
        (4, "LongReach", 1.2, "긴 돌진", "몽타주 루트 이동 거리가 20% 증가한다."),
        (7, "ExtraReach", 1.1, "추가 돌진 거리",
         "현재 몽타주 루트 이동 거리가 10% 추가 증가한다. 긴 돌진과 함께 선택하면 기본 대비 32% 증가한다."),
    ]:
        matches = [tier for tier in tiers if tier.get("requiredSkillLevel") == level]
        require(len(matches) <= 1, "Ambiguous unlock tier: " + str(level))
        if not matches:
            tiers.append({"options": [], "requiredSkillLevel": level})
            matches = [tiers[-1]]
            changed = True
        aim = copy.deepcopy(targeting)
        aim["bFollowSourceForHitQuery"] = True
        expected = {
            "cooldownReductionSeconds": 0.0,
            "persistenceId": identity, "optionName": label, "description": description, "optionIcon": "None",
            "statModifiers": [{"statTag": {"tagName": "Shared.Stat.MovementDistance"}, "scalarValue": scalar}],
            "tripodTag": {"tagName": "None"}, "overrideActionClass": "None",
            "overrideExecutionPolicyClass": "None", "overrideExecutionConfig": {},
            "overrideTargetingPolicyClass": "None", "overrideTargetingConfig": aim,
            "overrideMontage": "None", "overrideVFX": "None",
        }
        existing = [option for option in matches[0]["options"] if option.get("persistenceId") == identity]
        require(len(existing) <= 1, "Duplicate persistent choice: " + identity)
        if existing:
            require(equivalent(existing[0], expected), "Preserving a customized choice: " + identity)
        else:
            matches[0]["options"].append(expected)
            changed = True
    return {"TripodTiers": tiers, "SecurityProfile": security}, changed


def main():
    import unreal
    from toolset_registry.helpers import require_editable

    require(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is None,
            "Finish PIE before editing skill content")
    assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    dirty = {str(package.get_name()) for package in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    require(ASSET not in dirty, "Target has unsaved changes; preserving it")
    asset = assets.load_asset(ASSET)
    require(isinstance(asset, unreal.RPGSkillDefinition), "Missing Assault definition")
    properties = json.loads(unreal.ToolsetLibrary.get_object_properties(asset, FIELDS))
    update, changed = build_configuration(properties)
    apply = "-RPGConfigureGreatSwordAssaultDistanceTripods" in unreal.SystemLibrary.get_command_line()
    supported = hasattr(unreal, "RPGAbilityTask_PlayScaledSkillMontageAndWait")
    if apply:
        require(supported, "Load the normal module with the scaled montage task before saving choices")
    if apply and changed:
        require_editable(asset)
        # ArrayAdd requires the existing outer elements to stay identical while
        # adding a tier. Append options to existing tiers in a separate step.
        existing_count = len(properties["TripodTiers"])
        if existing_count and len(update["TripodTiers"]) > existing_count:
            intermediate = {"TripodTiers": update["TripodTiers"][:existing_count],
                            "SecurityProfile": update["SecurityProfile"]}
            require(unreal.ToolsetLibrary.set_object_properties(asset, json.dumps(intermediate, ensure_ascii=False)),
                    "Could not update existing Assault tiers before appending a new tier")
            # Preserve the engine's exact serialized prefix (including float32
            # values) so ArrayAdd sees only the intended new outer element.
            current = json.loads(unreal.ToolsetLibrary.get_object_properties(asset, ["TripodTiers"]))
            update["TripodTiers"][:existing_count] = current["TripodTiers"]
        require(unreal.ToolsetLibrary.set_object_properties(asset, json.dumps(update, ensure_ascii=False)),
                "Could not set Assault distance choices")
        readback = json.loads(unreal.ToolsetLibrary.get_object_properties(asset, FIELDS))
        _, needs_update = build_configuration(readback)
        require(not needs_update, "Readback differs; target was not saved")
        require(assets.save_loaded_asset(asset, only_if_is_dirty=False), "Could not save Assault distance choices")
    unreal.log("RPG_GREATSWORD_ASSAULT_DISTANCE " + json.dumps({
        "asset": ASSET, "apply": apply, "changed": apply and changed,
        "module_supports_scaled_montage": supported, "choices": ["LongReach", "ExtraReach"],
        "existing_indices_preserved": True, "cooldown_blueprints_montages_modified": False,
    }))


if __name__ == "__main__":
    main()

