"""Bind the native first-slice resolve identity to the existing greatsword class.

Requires a normal module build containing RPGGreatSwordResolveAbility. Run in
an editor with that module loaded, outside PIE, or an exclusive commandlet.
Inspect by default; -RPGConfigureGreatSwordIdentity applies only the exact
unconfigured class below. Never edit skill Definitions, montages or profiles.
"""
import json
import unreal


CLASS_PATH = "/Game/Blueprints/GameData/PlayerClass/DA_Class_GreatSwordWarrior"
CLASS_ID = "GreatSwordWarrior"
INPUT = "InputTag.IdentitySkill"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def make_tag(name):
    tag = unreal.GameplayTag()
    require(tag.import_text('(TagName="{}")'.format(name)), "Invalid gameplay tag: " + name)
    return tag


def main():
    identity_type = getattr(unreal, "RPGGreatSwordResolveAbility", None)
    require(identity_type is not None, "Normal module build/load required; refusing any legacy identity fallback")
    require(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is None,
            "Finish PIE before editing class content")
    assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    dirty = {str(package.get_name()) for package in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    require(CLASS_PATH not in dirty, "Class has unsaved changes; preserving it")
    asset = assets.load_asset(CLASS_PATH)
    require(isinstance(asset, unreal.RPGPlayerClassDefinition), "Missing authored class: " + CLASS_PATH)
    require(str(asset.get_editor_property("class_id")) == CLASS_ID, "Preserving a different stable ClassId")
    expected_ability = identity_type.static_class()
    expected_input = make_tag(INPUT)
    current_ability = asset.get_editor_property("identity_ability")
    current_input = asset.get_editor_property("identity_input_tag").export_text()
    already_applied = current_ability == expected_ability and current_input == expected_input.export_text()
    unconfigured = current_ability is None and current_input == unreal.GameplayTag().export_text()
    require(already_applied or unconfigured, "Preserving an existing/customized identity or input")
    apply = "-RPGConfigureGreatSwordIdentity" in unreal.SystemLibrary.get_command_line()
    changed = apply and not already_applied
    if changed:
        asset.set_editor_property("identity_ability", expected_ability)
        asset.set_editor_property("identity_input_tag", expected_input)
        require(assets.save_loaded_asset(asset, only_if_is_dirty=False), "Could not save class identity binding")
    require(not changed or (asset.get_editor_property("identity_ability") == expected_ability
                           and asset.get_editor_property("identity_input_tag").export_text() == expected_input.export_text()),
            "Class identity binding differs after apply")
    unreal.log("RPG_GREATSWORD_IDENTITY " + json.dumps({
        "apply": apply, "changed": changed, "already_applied": already_applied,
        "class": CLASS_PATH, "identity": "/Script/Project_RPG.RPGGreatSwordResolveAbility", "input": INPUT,
        "skills_montages_profiles_modified": False,
    }))


main()
