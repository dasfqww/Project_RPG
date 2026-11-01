"""Apply the first-slice cooldowns and Assault recovery choices, with backups.

Read-only by default. -RPGConfigureGreatSwordCombatBaseline permits saves after
the normal module exposes flat cooldown reduction. Existing choices retain their
indices and data. Legacy mana costs are set to zero for this first slice.
Damage, execution, targeting and security are preserved.
"""
import copy
import hashlib
import json
import math
import pathlib
import shutil


SKILLS = [("AssultBlade", "AssultBlade", 8.0), ("JumpSmash", "JumpSmash", 10.0),
          ("ChargeSlash", "Charge", 12.0), ("WhirlWind", "WhirlWind", 10.0)]
FIELDS = ["SkillTag", "SkillDataHandle", "SkillName", "SkillIcon", "SkillMontage", "SkillVFX",
          "DefaultActionClass", "DefaultExecutionPolicyClass", "DefaultExecutionConfig",
          "DefaultTargetingPolicyClass", "DefaultTargetingConfig", "BaseCooldown",
          "TargetingProfile", "SecurityProfile", "HitReactionPolicy", "StunDuration",
          "Knockback", "Slow", "Burn", "Stagger", "ActivationImmunities", "ModeOverrides",
          "TripodTiers", "MaxSkillLevel"]
CHARGE_FIELDS = ["MaxChargeHoldTime", "MaxChargeLevel", "ChargeTimePerLevel",
                 "MontageSections", "ChargeLevelSettings"]


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def equivalent(actual, expected):
    if isinstance(expected, dict):
        return isinstance(actual, dict) and actual.keys() == expected.keys() and all(
            equivalent(actual[key], value) for key, value in expected.items())
    if isinstance(expected, list):
        return isinstance(actual, list) and len(actual) == len(expected) and all(
            equivalent(a, b) for a, b in zip(actual, expected))
    if isinstance(expected, float):
        return isinstance(actual, (float, int)) and math.isclose(actual, expected, rel_tol=1e-6, abs_tol=1e-6)
    return actual == expected


def build_configuration(properties, tag, cooldown):
    require(properties["SkillTag"]["tagName"] == "Player.Ability.Skill." + tag, "Different skill identity")
    require(properties["BaseCooldown"] in (1.0, cooldown), "Preserving a customized base cooldown")
    update = {"BaseCooldown": cooldown}
    if tag != "AssultBlade":
        return update
    require(properties["MaxSkillLevel"] >= 7, "Level seven is unavailable")
    require(properties["DefaultExecutionPolicyClass"] == {
        "refPath": "/Script/Project_RPG.RPGSkillExecutionPolicy_Instant"}, "Review a different Assault policy")
    targeting = properties["DefaultTargetingConfig"]
    require(targeting.get("_structType") == "/Script/Project_RPG.RPGSkillSoftTargetingConfig",
            "Missing authored soft-target data")
    movement = properties["SecurityProfile"]["authorizedMovement"]
    require(movement["bEnabled"] and movement["durationSeconds"] > 0, "Missing bounded montage movement authorization")
    tiers = copy.deepcopy(properties["TripodTiers"])
    for level, identity, reduction, distance, label, description in [
        (4, "QuickApproach", 2.0, 0.8, "빠른 접근",
         "돌진 이동 거리가 20% 감소하고 기본 쿨타임이 2초 감소한다. 추가 회복과 함께 선택하면 5초다."),
        (7, "ExtraRecovery", 1.0, None, "추가 회복",
         "기본 쿨타임이 1초 추가 감소한다. 빠른 접근과 함께 선택하면 5초다."),
    ]:
        matches = [tier for tier in tiers if tier.get("requiredSkillLevel") == level]
        require(len(matches) == 1, "Expected one existing Assault tier: " + str(level))
        aim = copy.deepcopy(targeting) if distance is not None else {}
        if aim:
            aim["bFollowSourceForHitQuery"] = True
        expected = {
            "persistenceId": identity, "optionName": label, "description": description, "optionIcon": "None",
            "statModifiers": ([{"statTag": {"tagName": "Shared.Stat.MovementDistance"}, "scalarValue": distance}]
                              if distance is not None else []),
            "cooldownReductionSeconds": reduction,
            "tripodTag": {"tagName": "None"}, "overrideActionClass": "None",
            "overrideExecutionPolicyClass": "None", "overrideExecutionConfig": {},
            "overrideTargetingPolicyClass": "None", "overrideTargetingConfig": aim,
            "overrideMontage": "None", "overrideVFX": "None",
        }
        options = matches[0]["options"]
        existing = [option for option in options if option.get("persistenceId") == identity]
        require(len(existing) <= 1, "Duplicate persistent choice: " + identity)
        if existing:
            require(equivalent(existing[0], expected), "Preserving a customized choice: " + identity)
        else:
            options.append(expected)
    update["TripodTiers"] = tiers
    return update


def main():
    import unreal
    from toolset_registry.helpers import require_editable

    apply = "-RPGConfigureGreatSwordCombatBaseline" in unreal.SystemLibrary.get_command_line()
    if apply:
        require(hasattr(unreal.RPGSkillTripodOption(), "cooldown_reduction_seconds"),
                "Load the normal module with flat cooldown reduction before saving")
    require(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is None,
            "Finish PIE before editing skill content")
    dirty = {str(package.get_name()) for package in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    root = pathlib.Path(unreal.Paths.project_saved_dir()) / "ContentChecks/GreatSwordCombatBaseline"
    root.mkdir(parents=True, exist_ok=True)
    plans = []
    tables = {}
    class_definition = assets.load_asset("/Game/Blueprints/GameData/PlayerClass/DA_Class_GreatSwordWarrior")
    require(isinstance(class_definition, unreal.RPGPlayerClassDefinition), "Missing authored greatsword class")
    bindings = {binding.get_editor_property("definition").get_path_name(): binding.get_editor_property("ability_class")
                for binding in class_definition.get_editor_property("skills")}
    # Preflight every target and its current cost before the first mutation.
    for name, tag, cooldown in SKILLS:
        package = "/Game/Blueprints/GameData/Skill/DA_Skill_" + name
        require(package not in dirty, "Preserving unsaved content: " + package)
        asset = assets.load_asset(package)
        require(isinstance(asset, unreal.RPGSkillDefinition), "Missing skill: " + package)
        fields = FIELDS + (CHARGE_FIELDS if isinstance(asset, unreal.RPGSkillDefinition_Charge) else [])
        before = json.loads(unreal.ToolsetLibrary.get_object_properties(asset, fields))
        update = build_configuration(before, tag, cooldown)
        expected = copy.deepcopy(before)
        expected.update(update)
        handle = asset.get_editor_property("skill_data_handle")
        table = handle.get_editor_property("data_table")
        table_path = table.get_path_name().split(".")[0]
        require(table_path not in dirty, "Preserving unsaved skill table")
        if table_path not in tables:
            rows = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
            tables[table_path] = {"table": table, "before": rows, "after": copy.deepcopy(rows), "rows": []}
        table_plan = tables[table_path]
        row_name = str(handle.get_editor_property("row_name"))
        row = next(row for row in table_plan["before"] if row["Name"] == row_name)
        require(isinstance(row["ManaCost"], (int, float)) and math.isfinite(row["ManaCost"]) and row["ManaCost"] >= 0,
                "Malformed existing mana metadata")
        next(row for row in table_plan["after"] if row["Name"] == row_name)["ManaCost"] = 0
        table_plan["rows"].append(row_name)
        require(asset.get_path_name() in bindings, "Skill is not bound to the authored class")
        defaults = unreal.get_default_object(bindings[asset.get_path_name()])
        require(defaults.get_editor_property("cost_gameplay_effect_class") is None, "Preserving a nonzero GAS cost")
        unreal.log("GREATSWORD_BASELINE_INSPECT " + json.dumps({"asset": package, "row": row,
                   "legacy_manual_mana_cost": defaults.get_editor_property("use_legacy_manual_mana_cost")}, ensure_ascii=False))
        disk = pathlib.Path(unreal.Paths.project_content_dir()) / (package[len("/Game/"):] + ".uasset")
        digest = hashlib.sha256(disk.read_bytes()).hexdigest()
        if apply:
            require_editable(asset)
        plans.append((asset, package, fields, before, update, expected, disk, digest, row))
    for table_path, plan in tables.items():
        disk = pathlib.Path(unreal.Paths.project_content_dir()) / (table_path[len("/Game/"):] + ".uasset")
        plan.update({"disk": disk, "sha256": hashlib.sha256(disk.read_bytes()).hexdigest()})
        if apply:
            require_editable(plan["table"])
    # Back up all four original byte streams before writing any target.
    if apply:
        for _, _, _, _, _, _, disk, digest, _ in plans:
            backup = root / (disk.stem + "." + digest[:12] + ".uasset.backup")
            if not backup.exists():
                shutil.copy2(disk, backup)
            require(hashlib.sha256(backup.read_bytes()).hexdigest() == digest, "Backup differs")
        for plan in tables.values():
            backup = root / (plan["disk"].stem + "." + plan["sha256"][:12] + ".uasset.backup")
            if not backup.exists():
                shutil.copy2(plan["disk"], backup)
            require(hashlib.sha256(backup.read_bytes()).hexdigest() == plan["sha256"], "Table backup differs")
    proof = []
    for asset, package, fields, before, update, expected, disk, digest, row in plans:
        changed = not equivalent(before, expected)
        if apply and changed:
            require(hashlib.sha256(disk.read_bytes()).hexdigest() == digest, "Asset changed during authoring")
            require(unreal.ToolsetLibrary.set_object_properties(asset, json.dumps(update, ensure_ascii=False)), "Could not set baseline")
            current = json.loads(unreal.ToolsetLibrary.get_object_properties(asset, fields))
            require(equivalent(current, expected), "Unrelated data or intended update differs; target was not saved")
            require(assets.save_loaded_asset(asset, only_if_is_dirty=False), "Could not save baseline")
        current = json.loads(unreal.ToolsetLibrary.get_object_properties(asset, fields))
        require(equivalent(current, expected if apply else before), "Inspection or save changed unexpected data")
        if apply:
            require(not any(str(p.get_name()) == package for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()),
                    "Saved package is still dirty")
            require(equivalent(build_configuration(current, package.rsplit("_", 1)[-1].replace("ChargeSlash", "Charge"),
                                                  update["BaseCooldown"]), update), "Repeated configuration differs")
        proof.append({"asset": package, "before_cooldown": before["BaseCooldown"], "cooldown": current["BaseCooldown"],
                      "previous_mana_metadata": row["ManaCost"], "runtime_mana_cost": 0,
                      "saved": apply and changed, "needs_change": changed,
                      "before_sha256": digest, "after_sha256": hashlib.sha256(disk.read_bytes()).hexdigest(),
                      "preserved_fields": len(fields) - len(update), "existing_indices_preserved": True})
    table_proof = []
    for table_path, plan in tables.items():
        changed = plan["before"] != plan["after"]
        if apply and changed:
            require(hashlib.sha256(plan["disk"].read_bytes()).hexdigest() == plan["sha256"], "Table changed during authoring")
            require(unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(plan["table"], json.dumps(plan["after"], ensure_ascii=False)),
                    "Could not align legacy mana metadata")
            current = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(plan["table"]))
            require(equivalent(current, plan["after"]), "Unrelated table values differ; table was not saved")
            require(assets.save_loaded_asset(plan["table"], only_if_is_dirty=False), "Could not save skill table")
        current = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(plan["table"]))
        require(equivalent(current, plan["after"] if apply else plan["before"]), "Table readback differs")
        require(not any(str(p.get_name()) == table_path for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()),
                "Table remains dirty")
        table_proof.append({"asset": table_path, "rows": plan["rows"], "saved": apply and changed,
                            "before_sha256": plan["sha256"], "after_sha256": hashlib.sha256(plan["disk"].read_bytes()).hexdigest(),
                            "unrelated_rows_and_fields_preserved": True})
    (root / ("Applied.json" if apply else "ReadOnly.json")).write_text(json.dumps({
        "apply": apply, "assets": proof, "tables": table_proof,
        "choices": ["QuickApproach", "ExtraRecovery"]}, ensure_ascii=False, indent=2), encoding="utf-8")
    unreal.log("GREATSWORD_COMBAT_BASELINE PASS Assets=4 Saved=" + str(sum(row["saved"] for row in proof)))


if __name__ == "__main__":
    main()
