"""Prepare the first-slice JumpSmash area choices; preserve existing indices.

Default execution only inspects. -RPGConfigureGreatSwordJumpAreaTripods may save
after a normal module load exposes GetActiveSkillHitQueryProfile. It never
substitutes an effectless choice when the running module lacks radius support.
"""
import copy
import json
import math


ASSET = "/Game/Blueprints/GameData/Skill/DA_Skill_JumpSmash"
FIELDS = ["SkillTag", "TripodTiers", "TargetingProfile", "SecurityProfile", "DefaultExecutionPolicyClass"]


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
    require(properties["SkillTag"]["tagName"] == "Player.Ability.Skill.JumpSmash", "Different skill identity")
    policy = properties["DefaultExecutionPolicyClass"]
    require(isinstance(policy, dict) and bool(policy.get("refPath")), "Native execution policy is required")
    shape = properties["TargetingProfile"]["shape"]
    require(shape["type"] in ("Sphere", "Sector", "RingSector"), "Review a non-radial JumpSmash profile explicitly")
    radius = shape["radius"]
    limit = properties["SecurityProfile"]["maximumServerHitDistance"]
    require(math.isfinite(radius) and radius > 0 and math.isfinite(limit) and limit > 0
            and radius * 1.2 * 1.1 <= limit, "Prepared area exceeds the authored server distance limit")
    tiers = copy.deepcopy(properties["TripodTiers"])
    recipes = [
        (4, "WideArea", "범위 확대", 1.2, "공격 반경이 20% 증가한다. 조준 거리와 공격 높이는 유지한다."),
        (7, "ExtraArea", "추가 범위 확대", 1.1,
         "현재 공격 반경이 10% 추가 증가한다. 범위 확대와 함께 선택하면 기존 반경 대비 32% 증가한다."),
    ]
    changed = False
    for level, identity, label, scalar, description in recipes:
        matching = [tier for tier in tiers if tier.get("requiredSkillLevel") == level]
        require(len(matching) <= 1, "Ambiguous unlock tier: " + str(level))
        if not matching:
            tiers.append({"options": [], "requiredSkillLevel": level})
            matching = [tiers[-1]]
            changed = True
        expected = {
            "cooldownReductionSeconds": 0.0,
            "persistenceId": identity, "optionName": label, "description": description, "optionIcon": "None",
            "statModifiers": [{"statTag": {"tagName": "Shared.Stat.Radius"}, "scalarValue": scalar}],
            "tripodTag": {"tagName": "None"}, "overrideActionClass": "None",
            "overrideExecutionPolicyClass": "None", "overrideExecutionConfig": {},
            "overrideTargetingPolicyClass": "None", "overrideTargetingConfig": {},
            "overrideMontage": "None", "overrideVFX": "None",
        }
        options = matching[0]["options"]
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
    require(isinstance(asset, unreal.RPGSkillDefinition), "Missing JumpSmash definition")
    properties = json.loads(unreal.ToolsetLibrary.get_object_properties(asset, FIELDS))
    update, changed = build_configuration(properties)
    apply = "-RPGConfigureGreatSwordJumpAreaTripods" in unreal.SystemLibrary.get_command_line()
    if apply:
        require(hasattr(unreal.RPGGameplayAbility_SkillContainer, "get_active_skill_hit_query_profile"),
                "Load the normal module with radius support before saving choices")
    if apply and changed:
        require_editable(asset)
        require(unreal.ToolsetLibrary.set_object_properties(asset, json.dumps(update, ensure_ascii=False)),
                "Could not set JumpSmash choices")
        readback = json.loads(unreal.ToolsetLibrary.get_object_properties(asset, FIELDS))
        _, needs_update = build_configuration(readback)
        require(not needs_update, "Readback differs; target was not saved")
        require(assets.save_loaded_asset(asset, only_if_is_dirty=False), "Could not save JumpSmash choices")
    unreal.log("RPG_GREATSWORD_JUMP_AREA " + json.dumps({
        "asset": ASSET, "apply": apply, "changed": apply and changed,
        "choices": ["WideArea", "ExtraArea"], "existing_indices_preserved": True,
        "defaults_blueprints_montages_security_modified": False,
    }))


if __name__ == "__main__":
    main()
