"""Author Whirlwind's real finishing strike and level-seven FinalStagger choice.

Read-only by default. -RPGConfigureGreatSwordWhirlwindFinisher permits saving
only when the normal module exposes the native finisher and frozen section.
Existing loop notifies, choices, damage, cooldown and movement data are preserved.
The new End strike uses the existing per-hit damage, targeting and security path.
"""
import copy
import hashlib
import importlib.util
import json
import pathlib
import re
import shutil

spec = importlib.util.spec_from_file_location(
    "greatsword_finisher_helpers", pathlib.Path(__file__).with_name("ConfigureGreatSwordCombatBaseline.py"))
helpers = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helpers)
require, equivalent = helpers.require, helpers.equivalent
ASSET = "/Game/Blueprints/GameData/Skill/DA_Skill_WhirlWind"
MONTAGE = "/Game/Blueprints/Character/Player/Anim/Montage/AM_PlayerToggleSkill"
FIELDS = helpers.FIELDS + ["Contributions", "ContributionHitSection"]
TRACK = "SkillFinisher"
HIT_TIME = 1.0  # End animation occupies approximately 0.8667-1.1667 seconds.


def build_configuration(before):
    require(before["SkillTag"]["tagName"] == "Player.Ability.Skill.WhirlWind", "Different skill identity")
    require(before["DefaultExecutionPolicyClass"] == {
        "refPath": "/Script/Project_RPG.RPGSkillExecutionPolicy_Holding"}, "Preserving a different execution policy")
    execution = before["DefaultExecutionConfig"]
    require(execution.get("successSection") == "End" and execution.get("holdingSection") != "End",
            "Review changed Whirlwind sections")
    require(before["SkillMontage"] == {"refPath": MONTAGE + ".AM_PlayerToggleSkill"}, "Preserving a changed montage")
    desired = {"stagger": 20.0, "partBreak": 0.0}
    require(equivalent(before["Contributions"], {"stagger": 0.0, "partBreak": 0.0}) or
            equivalent(before["Contributions"], desired), "Preserving customized base contributions")
    require(before.get("ContributionHitSection", "None") in ("None", "End"), "Preserving a custom contribution section")
    require(before["MaxSkillLevel"] >= 7, "Level seven is unavailable")
    tiers = copy.deepcopy(before["TripodTiers"])
    matches = [tier for tier in tiers if tier["requiredSkillLevel"] == 7]
    require(len(matches) == 1, "Expected one existing level-seven row")
    expected = {
        "persistenceId": "FinalStagger", "optionName": "마지막 타격 무력화",
        "description": "회전 종료 타격의 무력화 기여가 20에서 25로 증가한다. 반복 타격과 종료 타격 전 취소·헛침에는 기여하지 않는다.",
        "optionIcon": "None", "statModifiers": [{
            "statTag": {"tagName": "Shared.Stat.StaggerContribution"}, "scalarValue": 1.25}],
        "cooldownReductionSeconds": 0.0, "tripodTag": {"tagName": "None"},
        "overrideActionClass": "None", "overrideExecutionPolicyClass": "None", "overrideExecutionConfig": {},
        "overrideTargetingPolicyClass": "None", "overrideTargetingConfig": {},
        "overrideMontage": "None", "overrideVFX": "None",
    }
    existing = [option for option in matches[0]["options"] if option.get("persistenceId") == "FinalStagger"]
    require(len(existing) <= 1, "Duplicate saved finishing choice")
    if existing:
        require(equivalent(existing[0], expected), "Preserving a customized finishing choice")
    else:
        matches[0]["options"].append(expected)
    return {"Contributions": desired, "ContributionHitSection": "End", "TripodTiers": tiers}


def notify_snapshot(unreal, montage):
    result = []
    for event in unreal.AnimationLibrary.get_animation_notify_events(montage):
        notify = event.get_editor_property("notify")
        result.append({"event": event.export_text(), "class": notify.get_class().get_path_name() if notify else None})
    return result


def is_finisher(item):
    return item["class"] == "/Script/Project_RPG.RPGAnimNotify_SkillFinisher"


def main():
    import unreal
    from toolset_registry.helpers import require_editable

    apply = "-RPGConfigureGreatSwordWhirlwindFinisher" in unreal.SystemLibrary.get_command_line()
    require(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is None, "Finish PIE first")
    assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    definition, montage = assets.load_asset(ASSET), assets.load_asset(MONTAGE)
    require(isinstance(definition, unreal.RPGSkillDefinition) and isinstance(montage, unreal.AnimMontage), "Missing Whirlwind content")
    before = json.loads(unreal.ToolsetLibrary.get_object_properties(definition, FIELDS))
    supported = hasattr(unreal, "RPGAnimNotify_SkillFinisher") and "ContributionHitSection" in before
    before.setdefault("ContributionHitSection", "None")
    update = build_configuration(before)
    properties = json.loads(unreal.ToolsetLibrary.get_object_properties(montage, ["SlotAnimTracks", "BlendIn", "BlendOut", "RateScale"]))
    segments = properties["SlotAnimTracks"][0]["animTrack"]["animSegments"]
    require(len(segments) == 3 and abs(segments[2]["startPos"] - 0.8666667) < 0.0001
            and abs(segments[2]["animEndTime"] - 0.3) < 0.0001, "Review changed End animation timing")
    require(montage.is_valid_section_name("End"), "Missing End section")
    before_notifies = notify_snapshot(unreal, montage)
    finishers = [item for item in before_notifies if is_finisher(item)]
    require(len(finishers) <= 1, "Preserving multiple native finishing notifies")
    if finishers:
        times = re.findall(r"LinkValue=([0-9.]+)", finishers[0]["event"])
        require(times and abs(float(times[-1]) - HIT_TIME) < 0.0001, "Preserving a customized finisher time")
    root = pathlib.Path(unreal.Paths.project_saved_dir()) / "ContentChecks/GreatSwordWhirlwindFinisher"
    root.mkdir(parents=True, exist_ok=True)
    expected = copy.deepcopy(before)
    expected.update(update)
    plan = {"module_supports_finisher": supported, "apply": apply,
            "definition_needs_change": not equivalent(before, expected), "montage_needs_change": not finishers,
            "prepared": update, "new_hit_time": HIT_TIME, "actual_saved": False}
    (root / "Prepared.json").write_text(json.dumps(plan, ensure_ascii=False, indent=2), encoding="utf-8")
    if not apply:
        unreal.log("GREATSWORD_WHIRLWIND_FINISHER INSPECT Supported=" + str(supported) + " NoAssetsSaved=1")
        return
    require(supported, "Load the normal module with the native finisher before saving")
    dirty = {str(p.get_name()) for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    disks = []
    for asset, package in [(definition, ASSET), (montage, MONTAGE)]:
        require(package not in dirty, "Preserving unsaved content: " + package)
        require_editable(asset)
        disk = pathlib.Path(unreal.Paths.project_content_dir()) / (package[len("/Game/"):] + ".uasset")
        digest = hashlib.sha256(disk.read_bytes()).hexdigest()
        backup = root / (disk.stem + "." + digest[:12] + ".uasset.backup")
        if not backup.exists():
            shutil.copy2(disk, backup)
        require(hashlib.sha256(backup.read_bytes()).hexdigest() == digest, "Backup differs")
        disks.append((disk, digest))
    if not finishers:
        if TRACK not in [str(name) for name in unreal.AnimationLibrary.get_animation_notify_track_names(montage)]:
            unreal.AnimationLibrary.add_animation_notify_track(montage, TRACK)
        require(unreal.AnimationLibrary.add_animation_notify_event(montage, TRACK, HIT_TIME,
                unreal.RPGAnimNotify_SkillFinisher) is not None, "Could not author the ending strike")
    if plan["definition_needs_change"]:
        require(unreal.ToolsetLibrary.set_object_properties(definition, json.dumps(update, ensure_ascii=False)), "Could not set finishing choice")
    current = json.loads(unreal.ToolsetLibrary.get_object_properties(definition, FIELDS))
    require(equivalent(current, expected), "Changed unrelated Definition data; not saved")
    require(equivalent(build_configuration(current), update), "Repeated configuration differs; not saved")
    current_notifies = notify_snapshot(unreal, montage)
    require(len([item for item in current_notifies if is_finisher(item)]) == 1, "Missing or duplicate ending strike")
    require([item for item in current_notifies if not is_finisher(item)] ==
            [item for item in before_notifies if not is_finisher(item)], "Existing notifies changed; not saved")
    require(json.loads(unreal.ToolsetLibrary.get_object_properties(montage, list(properties))) == properties,
            "Montage animation or blend data changed; not saved")
    validation, errors, warnings = unreal.get_editor_subsystem(unreal.EditorValidatorSubsystem).is_asset_valid(
        unreal.EditorAssetLibrary.find_asset_data(ASSET), unreal.DataValidationUsecase.SCRIPT)
    require(validation == unreal.DataValidationResult.VALID, "Definition validation failed: " + str(errors))
    for disk, digest in disks:
        require(hashlib.sha256(disk.read_bytes()).hexdigest() == digest, "Asset changed during authoring")
    # Save the dormant notify first. Without the Definition's frozen section it cannot execute.
    if not finishers:
        require(assets.save_loaded_asset(montage, only_if_is_dirty=False), "Could not save ending strike")
    if plan["definition_needs_change"]:
        require(assets.save_loaded_asset(definition, only_if_is_dirty=False), "Could not save finishing choice")
    require(not any(str(p.get_name()) in (ASSET, MONTAGE) for p in
            unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()), "Targets remain dirty")
    plan.update({"actual_saved": bool(plan["definition_needs_change"] or not finishers),
                 "preserved_definition_fields": len(before) - len(update), "existing_indices_preserved": True,
                 "existing_notifies_preserved": True, "validation_warnings": [str(w) for w in warnings],
                 "assets": [{"path": str(disk), "before_sha256": digest,
                             "after_sha256": hashlib.sha256(disk.read_bytes()).hexdigest()} for disk, digest in disks]})
    (root / "Applied.json").write_text(json.dumps(plan, ensure_ascii=False, indent=2), encoding="utf-8")
    unreal.log("GREATSWORD_WHIRLWIND_FINISHER PASS Saved=" + str(plan["actual_saved"]))


if __name__ == "__main__":
    main()
