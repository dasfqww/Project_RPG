"""Audit by default; normalize the two authored training action tags explicitly.

Preserves the generic attack classification and all other defaults. Never edits
input, montages, graphs, training goals, GAS completion, or reward state.
"""
import hashlib
import json
import os
import pathlib
import re
import shutil

import unreal

TARGETS = (
    ("/Game/Blueprints/Character/Player/Abilities/DefaultAttack/GA_Player_DefaultAttack",
     "Player.Ability.Attack.Light.GreatSword", None, unreal.RPGComboSkillAbility),
    ("/Game/Blueprints/Character/Player/Abilities/GA_Player_Roll",
     "Player.Ability.Roll", "Player.Roll", unreal.RPGPlayerGameplayAbility),
)
COMMON = ("net_execution_policy", "net_security_policy", "instancing_policy",
          "activation_required_tags", "activation_blocked_tags", "activation_owned_tags",
          "block_abilities_with_tag", "cancel_abilities_with_tag", "input_tag", "skill_name")


def require(value, message):
    if not value:
        raise RuntimeError(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def tags(value):
    return set(re.findall(r'TagName="([^"]+)"', value.export_text()))


def serialize(value):
    if hasattr(value, "export_text"):
        return value.export_text()
    if hasattr(value, "get_path_name"):
        return value.get_path_name()
    return str(value)


def snapshot(defaults, combo):
    # Read all exposed defaults as well as the critical native action settings.
    # dir() also includes methods; get_editor_property only reads properties.
    names = set(COMMON) | set(dir(defaults))
    if combo:
        names.update(("DefaultAttackSectionData", "DefaultAttackSectionData_Rage",
                      "InitComboCount", "TimeToTimerReset", "GainIdentityEffect"))
    result = {}
    for name in sorted(names):
        if name.startswith("_") or name in ("ability_tags", "asset_tags"):
            continue
        try:
            value = defaults.get_editor_property(name)
        except Exception:
            require(name not in COMMON, "Cannot read required default: " + name)
            continue
        result[name] = serialize(value)
    for name in COMMON:
        require(name in result, "Missing preserved default: " + name)
    return result


def main():
    apply = "-RPGConfigureTrainingAbilityTags" in unreal.SystemLibrary.get_command_line()
    report = pathlib.Path(os.environ["RPG_TRAINING_TAG_REPORT"])
    require(report.is_absolute(), "Report path must be absolute")
    assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    require(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is None,
            "Preserving active PIE")
    dirty = {str(p.get_name()) for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    rows = []
    for package, canonical, legacy, expected_type in TARGETS:
        require(package not in dirty, "Preserving unsaved ability: " + package)
        blueprint = assets.load_asset(package)
        require(isinstance(blueprint, unreal.Blueprint), "Missing ability Blueprint")
        defaults = unreal.get_default_object(blueprint.generated_class())
        require(isinstance(defaults, expected_type), "Unexpected action owner: " + package)
        require(not isinstance(defaults, unreal.RPGGameplayAbility_SkillContainer), "Not a class active")
        before = tags(defaults.get_editor_property("ability_tags"))
        after = (before - ({legacy} if legacy else set())) | {canonical}
        tag = unreal.GameplayTag()
        require(tag.import_text('(TagName="' + canonical + '")'), "Unknown canonical tag")
        path = pathlib.Path(unreal.Paths.project_content_dir()) / (package[6:] + ".uasset")
        rows.append(dict(package=package, canonical=canonical, legacy=legacy,
                         before=sorted(before), expected=sorted(after),
                         before_sha256=digest(path), defaults=snapshot(defaults, legacy is None),
                         blueprint=blueprint, cdo=defaults, path=path))
    backups = None
    written = []
    try:
        if apply:
            backups = pathlib.Path(os.environ["RPG_TRAINING_TAG_BACKUPS"])
            require(backups.is_absolute(), "Backup path must be absolute")
            backups.mkdir(parents=True, exist_ok=True)
            for index, row in enumerate(rows):
                require(digest(row["path"]) == row["before_sha256"], "Ability changed during planning")
                shutil.copy2(row["path"], backups / (str(index) + ".uasset"))
        for index, row in enumerate(rows):
            changed = row["before"] != row["expected"]
            if apply and changed:
                require(digest(row["path"]) == row["before_sha256"], "Ability changed before save")
                value = unreal.GameplayTagContainer()
                require(value.import_text("(GameplayTags=(" + ",".join(
                    '(TagName="' + tag + '")' for tag in row["expected"]) + "))"), "Invalid tag container")
                row["cdo"].set_editor_property("ability_tags", value)
                unreal.BlueprintEditorLibrary.compile_blueprint(row["blueprint"])
                current = unreal.get_default_object(row["blueprint"].generated_class())
                require(tags(current.get_editor_property("ability_tags")) == set(row["expected"]), "Compiled tags differ")
                require(snapshot(current, row["legacy"] is None) == row["defaults"], "Unrelated defaults changed")
                require(row["blueprint"].get_editor_property("status") == unreal.BlueprintStatus.BS_UP_TO_DATE,
                        "Ability compilation failed")
                require(assets.save_loaded_asset(row["blueprint"], only_if_is_dirty=False), "Ability save failed")
                written.append((index, digest(row["path"])))
            current = unreal.get_default_object(row["blueprint"].generated_class())
            require(snapshot(current, row["legacy"] is None) == row["defaults"], "Inspection changed defaults")
            row["after"] = sorted(tags(current.get_editor_property("ability_tags")))
            row["saved"] = bool(apply and changed)
            row["after_sha256"] = digest(row["path"])
            if apply:
                require(row["after"] == row["expected"], "Canonical tag is absent")
        dirty_after = {str(p.get_name()) for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
        require(all(row["package"] not in dirty_after for row in rows), "Ability is dirty after authoring")
    except Exception:
        # Restore only our exact writes; never overwrite an unexpected newer save.
        for index, saved_hash in reversed(written):
            row = rows[index]
            require(digest(row["path"]) == saved_hash, "Newer asset detected; preserving it during rollback")
            shutil.copy2(backups / (str(index) + ".uasset"), row["path"])
        raise
    proof = []
    for row in rows:
        proof.append({key: value for key, value in row.items() if key not in ("blueprint", "cdo", "path")})
    report.parent.mkdir(parents=True, exist_ok=True)
    report.write_text(json.dumps(dict(apply=apply, assets=proof,
                                     saved=sum(row["saved"] for row in rows),
                                     gameplay_executed=False), indent=2), encoding="utf-8")
    unreal.log("RPG_TRAINING_TAGS PASS Apply=" + str(int(apply)) + " Saved=" + str(sum(row["saved"] for row in rows)))


main()
