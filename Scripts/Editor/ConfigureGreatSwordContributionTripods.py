"""Author real Charge/Jump contributions and opt in the existing practice target.

Read-only by default; -RPGConfigureGreatSwordContributionTripods saves backed-up
targets only. Existing options/indices, damage, timing and security are preserved.
This authors meter completion, not a boss phase, forced stun or destruction mesh.
"""
import copy
import hashlib
import importlib.util
import json
import pathlib
import shutil

helper_path = pathlib.Path(__file__).with_name("ConfigureGreatSwordCombatBaseline.py")
spec = importlib.util.spec_from_file_location("greatsword_baseline_helpers", helper_path)
helpers = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helpers)
require, equivalent = helpers.require, helpers.equivalent
FIELDS = helpers.FIELDS + ["Contributions"]
DUMMY = "/Game/Blueprints/Character/NPC/DummyNPC/BP_Dummy_Gdn"


def build_configuration(before, skill):
    require(before["SkillTag"]["tagName"] == "Player.Ability.Skill." + skill, "Different skill identity")
    desired = {"stagger": 20.0, "partBreak": 20.0}
    require(equivalent(before["Contributions"], {"stagger": 0.0, "partBreak": 0.0}) or
            equivalent(before["Contributions"], desired), "Preserving customized base contributions")
    tiers = copy.deepcopy(before["TripodTiers"])
    for level, identity, tag, label in [
        (7, "Stagger", "Shared.Stat.StaggerContribution", "무력화 기여 강화"),
        (7 if skill == "Charge" else 4, "PartBreak", "Shared.Stat.PartBreakContribution", "부위 파괴 기여 강화"),
    ]:
        matches = [tier for tier in tiers if tier["requiredSkillLevel"] == level]
        require(len(matches) <= 1, "Ambiguous unlock tier")
        if not matches:
            tiers.append({"requiredSkillLevel": level, "options": []})
            matches = [tiers[-1]]
        expected = {
            "persistenceId": identity, "optionName": label,
            "description": "서버가 승인한 적중의 해당 기여가 25% 증가한다. 같은 시전은 대상마다 한 번만 기여한다.",
            "optionIcon": "None", "statModifiers": [{"statTag": {"tagName": tag}, "scalarValue": 1.25}],
            "cooldownReductionSeconds": 0.0,
            "tripodTag": {"tagName": "None"}, "overrideActionClass": "None",
            "overrideExecutionPolicyClass": "None", "overrideExecutionConfig": {},
            "overrideTargetingPolicyClass": "None", "overrideTargetingConfig": {},
            "overrideMontage": "None", "overrideVFX": "None",
        }
        existing = [option for option in matches[0]["options"] if option.get("persistenceId") == identity]
        require(len(existing) <= 1, "Duplicate saved contribution choice")
        if existing:
            require(equivalent(existing[0], expected), "Preserving a customized contribution choice")
        else:
            matches[0]["options"].append(expected)
    return {"Contributions": desired, "TripodTiers": tiers}


def main():
    import unreal
    from toolset_registry.helpers import require_editable

    apply = "-RPGConfigureGreatSwordContributionTripods" in unreal.SystemLibrary.get_command_line()
    require(hasattr(unreal, "RPGCombatContributionComponent"), "Load the normal module with real contribution receivers first")
    require(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is None, "Finish PIE first")
    assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    dirty = {str(p.get_name()) for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    root = pathlib.Path(unreal.Paths.project_saved_dir()) / "ContentChecks/GreatSwordContributions"
    root.mkdir(parents=True, exist_ok=True)
    plans = []
    for name, skill in [("ChargeSlash", "Charge"), ("JumpSmash", "JumpSmash")]:
        package = "/Game/Blueprints/GameData/Skill/DA_Skill_" + name
        asset = assets.load_asset(package)
        require(isinstance(asset, unreal.RPGSkillDefinition), "Missing skill")
        before = json.loads(unreal.ToolsetLibrary.get_object_properties(asset, FIELDS))
        update = build_configuration(before, skill)
        plans.append((asset, asset, package, FIELDS, before, update, skill))
    blueprint = assets.load_asset(DUMMY)
    cdo = unreal.get_default_object(blueprint.generated_class())
    receiver = cdo.get_editor_property("combat_contribution_component")
    require(isinstance(receiver, unreal.RPGCombatContributionComponent) and receiver.get_outer() == cdo,
            "Practice receiver must belong to this Blueprint CDO")
    before = json.loads(unreal.ToolsetLibrary.get_object_properties(receiver, ["ReceiverProfile"]))
    profile = {"bEnabled": True, "staggerThreshold": 100.0, "partBreakThreshold": 100.0}
    require(equivalent(before["ReceiverProfile"], dict(profile, bEnabled=False)) or
            equivalent(before["ReceiverProfile"], profile), "Preserving a customized practice profile")
    plans.append((blueprint, receiver, DUMMY, ["ReceiverProfile"], before, {"ReceiverProfile": profile}, None))
    disks = {}
    for asset, _, package, _, _, _, _ in plans:
        require(package not in dirty, "Preserving unsaved content: " + package)
        disk = pathlib.Path(unreal.Paths.project_content_dir()) / (package[len("/Game/"):] + ".uasset")
        digest = hashlib.sha256(disk.read_bytes()).hexdigest()
        disks[package] = (disk, digest)
        if apply:
            require_editable(asset)
            backup = root / (disk.stem + "." + digest[:12] + ".uasset.backup")
            if not backup.exists():
                shutil.copy2(disk, backup)
            require(hashlib.sha256(backup.read_bytes()).hexdigest() == digest, "Backup differs")
    proof = []
    for asset, target, package, fields, before, update, skill in plans:
        expected = copy.deepcopy(before)
        expected.update(update)
        changed = not equivalent(before, expected)
        disk, digest = disks[package]
        if apply and changed:
            require(hashlib.sha256(disk.read_bytes()).hexdigest() == digest, "Target changed during authoring")
            # Preserve exact serialized outer prefix while appending a new tier.
            if skill and len(update["TripodTiers"]) > len(before["TripodTiers"]):
                prefix = {"TripodTiers": update["TripodTiers"][:len(before["TripodTiers"])]}
                require(unreal.ToolsetLibrary.set_object_properties(target, json.dumps(prefix, ensure_ascii=False)), "Could not prepare existing tiers")
                current = json.loads(unreal.ToolsetLibrary.get_object_properties(target, ["TripodTiers"]))
                update["TripodTiers"][:len(before["TripodTiers"])] = current["TripodTiers"]
            require(unreal.ToolsetLibrary.set_object_properties(target, json.dumps(update, ensure_ascii=False)), "Could not set contribution content")
            current = json.loads(unreal.ToolsetLibrary.get_object_properties(target, fields))
            require(equivalent(current, expected), "Unrelated or intended data differs; not saved")
            require(assets.save_loaded_asset(asset, only_if_is_dirty=False), "Could not save contribution content")
        current = json.loads(unreal.ToolsetLibrary.get_object_properties(target, fields))
        require(equivalent(current, expected if apply else before), "Unexpected readback")
        if skill and apply:
            require(equivalent(build_configuration(current, skill), update), "Repeated authoring differs")
        require(not any(str(p.get_name()) == package for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()), "Package remains dirty")
        proof.append({"asset": package, "saved": apply and changed, "needs_change": changed,
                      "before_sha256": digest, "after_sha256": hashlib.sha256(disk.read_bytes()).hexdigest(),
                      "preserved_fields": len(fields) - len(update), "existing_indices_preserved": True})
    (root / ("Applied.json" if apply else "ReadOnly.json")).write_text(json.dumps({"apply": apply, "assets": proof},
        ensure_ascii=False, indent=2), encoding="utf-8")
    unreal.log("GREATSWORD_CONTRIBUTIONS PASS Assets=3 Saved=" + str(sum(p["saved"] for p in proof)))


if __name__ == "__main__":
    main()
