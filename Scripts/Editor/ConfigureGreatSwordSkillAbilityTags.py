"""Normalize class actives' asset tags; preserve every unrelated tag/default.

Read-only by default. -RPGNormalizeClassSkillTags enables guarded saves with
byte-for-byte backups. Identity remains a separate class binding.
"""
import hashlib
import json
import pathlib
import re
import shutil

import unreal


CLASS_PATH = "/Game/Blueprints/GameData/PlayerClass/DA_Class_GreatSwordWarrior"
LEGACY_IDENTITY = "Player.Ability.IdentitySkill"
PRESERVED = ("net_execution_policy", "instancing_policy", "activation_required_tags",
             "activation_blocked_tags", "block_abilities_with_tag", "cancel_abilities_with_tag",
             "skill_definition")


def require(value, message):
    if not value:
        raise RuntimeError(message)


def tags(container):
    return set(re.findall(r'TagName="([^"]+)"', container.export_text()))


def snapshot(defaults):
    result = {}
    for field in PRESERVED:
        value = defaults.get_editor_property(field)
        result[field] = (value.export_text() if hasattr(value, "export_text") else
                         value.get_path_name() if hasattr(value, "get_path_name") else str(value))
    return result


def main():
    apply = "-RPGNormalizeClassSkillTags" in unreal.SystemLibrary.get_command_line()
    require(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is None,
            "Finish PIE before changing class skill content")
    dirty = {str(package.get_name()) for package in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    definition = assets.load_asset(CLASS_PATH)
    require(isinstance(definition, unreal.RPGPlayerClassDefinition), "Missing authored class")
    root = pathlib.Path(unreal.Paths.project_saved_dir()) / "ContentChecks/GreatSwordSkillTags"
    root.mkdir(parents=True, exist_ok=True)
    rows = []
    for binding in definition.get_editor_property("skills"):
        skill = binding.get_editor_property("definition")
        ability_class = binding.get_editor_property("ability_class")
        defaults = unreal.get_default_object(ability_class)
        require(isinstance(defaults, unreal.RPGGameplayAbility_SkillContainer), "Class active must use the skill container")
        require(defaults.get_editor_property("skill_definition") == skill, "Ability/definition mismatch")
        tag_text = skill.get_editor_property("skill_tag").export_text()
        match = re.search(r'TagName="([^"]+)"', tag_text)
        require(match is not None and match[1].startswith("Player.Ability.Skill."), "Invalid class active identity")
        skill_tag = match[1]
        old_tags = tags(defaults.get_editor_property("ability_tags"))
        new_tags = (old_tags - {LEGACY_IDENTITY}) | {skill_tag}
        package = ability_class.get_path_name().split(".")[0]
        require(package not in dirty, "Preserving an unsaved skill ability: " + package)
        blueprint = assets.load_asset(package)
        disk = pathlib.Path(unreal.Paths.project_content_dir()) / (package[len("/Game/"):] + ".uasset")
        digest = hashlib.sha256(disk.read_bytes()).hexdigest()
        rows.append((blueprint, defaults, disk, package, digest, old_tags, new_tags, snapshot(defaults)))
    require(len(rows) == 4, "Expected the authored four-skill first class")
    proof = []
    for blueprint, defaults, disk, package, digest, old_tags, new_tags, previous in rows:
        changed = old_tags != new_tags
        if changed and apply:
            require(hashlib.sha256(disk.read_bytes()).hexdigest() == digest, "Asset changed during authoring; preserving it")
            backup = root / (disk.stem + "." + digest[:12] + ".uasset.backup")
            if not backup.exists():
                shutil.copy2(disk, backup)
            value = unreal.GameplayTagContainer()
            value.import_text("(GameplayTags=(" + ",".join('(TagName="' + tag + '")' for tag in sorted(new_tags)) + "))")
            defaults.set_editor_property("ability_tags", value)
            unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
            current = unreal.get_default_object(blueprint.generated_class())
            require(tags(current.get_editor_property("ability_tags")) == new_tags, "Compiled asset tags differ")
            require(snapshot(current) == previous, "Unrelated ability defaults changed")
            require(assets.save_loaded_asset(blueprint, only_if_is_dirty=False), "Could not save skill ability")
        current = unreal.get_default_object(blueprint.generated_class())
        require(snapshot(current) == previous, "Inspection changed unrelated defaults")
        if apply:
            require(tags(current.get_editor_property("ability_tags")) == new_tags, "Saved tags differ")
            require(package not in {str(item.get_name()) for item in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()},
                    "Saved skill package is still dirty")
        proof.append({"package": package, "before": sorted(old_tags), "after": sorted(tags(current.get_editor_property("ability_tags"))),
                      "needs_change": changed, "saved": bool(changed and apply), "before_sha256": digest,
                      "after_sha256": hashlib.sha256(disk.read_bytes()).hexdigest(), "unrelated_defaults_preserved": True})
    output = root / ("Applied.json" if apply else "ReadOnly.json")
    output.write_text(json.dumps({"apply": apply, "assets": proof}, ensure_ascii=False, indent=2), encoding="utf-8")
    unreal.log("CLASS_SKILL_TAGS PASS Assets=4 Saved=" + str(sum(row["saved"] for row in proof)))


main()
