"""Append the first-slice charge choices without replacing existing options.

Normal UE5.8 Editor/ToolsetLibrary is required for authoring. Inspect by default;
-RPGConfigureGreatSwordChargeTripods saves only DA_Skill_ChargeSlash. No Blueprint,
montage, table, default timing/cooldown, tier order or existing option is edited.
"""
import copy
import json
import math


ASSET = "/Game/Blueprints/GameData/Skill/DA_Skill_ChargeSlash"
CHARGE_POLICY = "/Script/Project_RPG.RPGSkillExecutionPolicy_Charge"
CHARGE_CONFIG = "/Script/Project_RPG.RPGSkillChargeExecutionConfig"
FIELDS = ["SkillTag", "TripodTiers", "DefaultExecutionPolicyClass", "DefaultExecutionConfig"]


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def equivalent(actual, expected):
    """UE stores floats as float32 and exports their rounded values."""
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
    require(properties["SkillTag"]["tagName"] == "Player.Ability.Skill.Charge", "Different skill identity")
    require(properties["DefaultExecutionPolicyClass"].get("refPath") == CHARGE_POLICY,
            "Preserving a different default execution policy")
    base = properties["DefaultExecutionConfig"]
    require(base.get("_structType") == CHARGE_CONFIG, "Missing typed native charge config")
    levels = base.get("maxChargeLevel", 0)
    require(type(levels) is int and 1 <= levels <= 32, "Invalid charge stage count")
    tiers = copy.deepcopy(properties["TripodTiers"])
    matching = [tier for tier in tiers if tier.get("requiredSkillLevel") == 4]
    require(len(matching) == 1, "Expected exactly one existing level-4 tier")
    options = matching[0]["options"]
    changed = False
    recipes = [
        ("FullCharge", "완충 강화", 1.2, 3.2 / 2.8,
         "최대 충전까지 1.2초. 기존 기본 피해 대비 14.29% 증가. 완충 전에 놓으면 시전을 취소한다."),
        ("QuickCharge", "빠른 충전", 0.7, 2.6 / 2.8,
         "최대 충전까지 0.7초. 기존 기본 피해 대비 7.14% 감소. 완충 전에 놓으면 시전을 취소한다."),
    ]
    for identity, label, duration, damage_ratio, description in recipes:
        config = copy.deepcopy(base)
        config["chargeTimePerLevel"] = duration / levels
        config["minimumReleaseLevel"] = levels
        expected = {
            "cooldownReductionSeconds": 0.0,
            "persistenceId": identity, "optionName": label, "description": description,
            "optionIcon": "None", "statModifiers": [
                {"statTag": {"tagName": "Shared.Stat.Damage"}, "scalarValue": damage_ratio}],
            "tripodTag": {"tagName": "None"}, "overrideActionClass": "None",
            "overrideExecutionPolicyClass": {"refPath": CHARGE_POLICY}, "overrideExecutionConfig": config,
            "overrideTargetingPolicyClass": "None", "overrideTargetingConfig": {},
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
            "Finish PIE before authoring skill content")
    assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    dirty = {str(package.get_name()) for package in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    require(ASSET not in dirty, "Skill has unsaved changes; preserving it")
    asset = assets.load_asset(ASSET)
    require(isinstance(asset, unreal.RPGSkillDefinition), "Missing skill definition")
    properties = json.loads(unreal.ToolsetLibrary.get_object_properties(asset, FIELDS))
    update, changed = build_configuration(properties)
    apply = "-RPGConfigureGreatSwordChargeTripods" in unreal.SystemLibrary.get_command_line()
    if apply and changed:
        require_editable(asset)
        require(unreal.ToolsetLibrary.set_object_properties(asset, json.dumps(update, ensure_ascii=False)),
                "Could not apply charge choices")
        readback = json.loads(unreal.ToolsetLibrary.get_object_properties(asset, FIELDS))
        _, needs_update = build_configuration(readback)
        require(not needs_update, "Charge choice readback differs; asset was not saved")
        require(assets.save_loaded_asset(asset, only_if_is_dirty=False), "Could not save charge choices")
    unreal.log("RPG_GREATSWORD_CHARGE_TRIPODS " + json.dumps({
        "asset": ASSET, "apply": apply, "changed": apply and changed,
        "choices": ["FullCharge", "QuickCharge"], "existing_indices_preserved": True,
        "defaults_blueprints_tables_montages_modified": False,
    }))


if __name__ == "__main__":
    main()
