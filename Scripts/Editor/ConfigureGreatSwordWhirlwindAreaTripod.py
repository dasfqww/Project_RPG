"""Author the first-slice Whirlwind level-seven area choice.

Inspect by default; -RPGConfigureGreatSwordWhirlwindAreaTripod permits saving.
The running module must expose the native radius consumer before any write.
Existing tier and option indices, including custom choices, are preserved.
"""
import copy
import json
import math


ASSET = "/Game/Blueprints/GameData/Skill/DA_Skill_WhirlWind"
FIELDS = ["SkillTag", "TripodTiers", "TargetingProfile", "SecurityProfile",
          "DefaultExecutionPolicyClass", "MaxSkillLevel"]


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
    policy = properties["DefaultExecutionPolicyClass"]
    require(isinstance(policy, dict) and policy.get("refPath", "").startswith(
        "/Script/Project_RPG.RPGSkillExecutionPolicy_"), "Native execution policy is required")
    require(properties["MaxSkillLevel"] >= 7, "Level seven is unavailable")
    shape = properties["TargetingProfile"]["shape"]
    require(shape["type"] in ("Sphere", "Sector", "RingSector"),
            "Review a non-radial Whirlwind profile explicitly")
    radius = shape["radius"]
    limit = properties["SecurityProfile"]["maximumServerHitDistance"]
    require(math.isfinite(radius) and radius > 0 and math.isfinite(limit) and limit > 0
            and radius * 1.2 <= limit, "Area choice exceeds the authored server distance limit")

    tiers = copy.deepcopy(properties["TripodTiers"])
    changed = False
    # Reserve the level-four row before appending level seven on an empty asset.
    # No movement choice or effectless persistent ID is authored here.
    for level in (4, 7):
        matching = [tier for tier in tiers if tier.get("requiredSkillLevel") == level]
        require(len(matching) <= 1, "Ambiguous unlock tier: " + str(level))
        if not matching:
            tiers.append({"options": [], "requiredSkillLevel": level})
            changed = True
    tier = next(tier for tier in tiers if tier["requiredSkillLevel"] == 7)
    expected = {
        "cooldownReductionSeconds": 0.0,
        "persistenceId": "WideArea", "optionName": "범위 확대",
        "description": "회전 공격의 반경이 20% 증가한다. 공격 높이와 지속시간은 유지한다.",
        "optionIcon": "None",
        "statModifiers": [{"statTag": {"tagName": "Shared.Stat.Radius"}, "scalarValue": 1.2}],
        "tripodTag": {"tagName": "None"}, "overrideActionClass": "None",
        "overrideExecutionPolicyClass": "None", "overrideExecutionConfig": {},
        "overrideTargetingPolicyClass": "None", "overrideTargetingConfig": {},
        "overrideMontage": "None", "overrideVFX": "None",
    }
    existing = [option for option in tier["options"] if option.get("persistenceId") == "WideArea"]
    require(len(existing) <= 1, "Duplicate persistent choice: WideArea")
    if existing:
        require(equivalent(existing[0], expected), "Preserving a customized choice: WideArea")
    else:
        tier["options"].append(expected)
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
    apply = "-RPGConfigureGreatSwordWhirlwindAreaTripod" in unreal.SystemLibrary.get_command_line()
    if apply:
        require(hasattr(unreal.RPGGameplayAbility_SkillContainer, "get_active_skill_hit_query_profile"),
                "Load the normal module with radius support before saving choices")
    if apply and changed:
        require_editable(asset)
        require(unreal.ToolsetLibrary.set_object_properties(asset, json.dumps(update, ensure_ascii=False)),
                "Could not set Whirlwind area choice")
        readback = json.loads(unreal.ToolsetLibrary.get_object_properties(asset, FIELDS))
        _, needs_update = build_configuration(readback)
        require(not needs_update, "Readback differs; target was not saved")
        require(assets.save_loaded_asset(asset, only_if_is_dirty=False), "Could not save Whirlwind area choice")
    unreal.log("RPG_GREATSWORD_WHIRLWIND_AREA " + json.dumps({
        "asset": ASSET, "apply": apply, "changed": apply and changed,
        "choices": ["WideArea"], "existing_indices_preserved": True,
        "defaults_blueprints_montages_security_modified": False,
    }))


if __name__ == "__main__":
    main()

