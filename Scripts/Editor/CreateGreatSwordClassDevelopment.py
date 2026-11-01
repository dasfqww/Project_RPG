"""Create the greatsword class and its explicit editor-build development profile.

Requires a normal build containing the PlayerClass native types.
Run with UnrealEditor-Cmd -run=PythonScript -script=<this file>
-RPGCreateGreatSwordClass. Without the switch, only inspect source/existing assets.
Only missing target assets are saved. Existing skill/ability/montage assets are read-only.
"""
import json
import unreal


ROOT = "/Game/Blueprints/GameData/PlayerClass"
CLASS_PATH = ROOT + "/DA_Class_GreatSwordWarrior"
PROFILE_PATH = ROOT + "/Development/DA_GreatSword_Development"
CLASS_ID = "GreatSwordWarrior"
SKILLS = (
    ("Player.Ability.Skill.AssultBlade", "DA_Skill_AssultBlade", "GA_PlayerSkill_AssultBlade"),
    ("Player.Ability.Skill.JumpSmash", "DA_Skill_JumpSmash", "GA_Player_JumpSmash"),
    ("Player.Ability.Skill.Charge", "DA_Skill_ChargeSlash", "GA_Player_ChargeSlash"),
    ("Player.Ability.Skill.WhirlWind", "DA_Skill_WhirlWind", "GA_Player_WhirlWind"),
)


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def make_tag(name):
    value = unreal.GameplayTag()
    require(value.import_text('(TagName="{}")'.format(name)), "Invalid gameplay tag: " + name)
    return value


def tag_values(values):
    # GameplayTag's Python wrapper does not expose native C++ value equality.
    return [value.export_text() for value in values]


def load_sources(assets):
    bindings = []
    for tag_name, definition_name, ability_name in SKILLS:
        definition = assets.load_asset("/Game/Blueprints/GameData/Skill/" + definition_name)
        blueprint = assets.load_asset("/Game/Blueprints/Character/Player/Abilities/Skill/" + ability_name)
        require(isinstance(definition, unreal.RPGSkillDefinition), "Missing skill: " + definition_name)
        require(isinstance(blueprint, unreal.Blueprint), "Missing ability: " + ability_name)
        ability_class = blueprint.generated_class()
        require(ability_class is not None, "Ability has no generated class: " + ability_name)
        defaults = unreal.get_default_object(ability_class)
        require(isinstance(defaults, unreal.RPGGameplayAbility_SkillContainer),
                "Ability must already use SkillContainer: " + ability_name)
        require(defaults.get_editor_property("skill_definition") == definition,
                "Definition/Ability binding differs: " + ability_name)
        tag = make_tag(tag_name)
        actual_tag = definition.get_editor_property("skill_tag")
        require(actual_tag.export_text() == tag.export_text(),
                "Skill tag differs: {} actual={} expected={}".format(definition_name, actual_tag.export_text(), tag.export_text()))
        binding = unreal.RPGPlayerClassSkill(definition=definition, ability_class=ability_class)
        bindings.append(binding)
    return bindings


def check_class(asset, bindings):
    require(isinstance(asset, unreal.RPGPlayerClassDefinition), "Target class has another asset type")
    require(str(asset.get_editor_property("class_id")) == CLASS_ID, "Preserving different ClassId")
    require(list(asset.get_editor_property("allowed_weapon_types")) == [unreal.RPGGladiatorWeaponType.GREAT_SWORD],
            "Preserving different weapon requirements")
    actual = list(asset.get_editor_property("skills"))
    require(len(actual) == len(bindings), "Preserving a customized class catalog")
    for current, expected in zip(actual, bindings):
        for field in ("definition", "ability_class"):
            require(current.get_editor_property(field) == expected.get_editor_property(field),
                    "Preserving a customized class binding")
    require(tag_values(asset.get_editor_property("default_skill_slots")) == tag_values([make_tag(row[0]) for row in SKILLS]),
            "Preserving customized class creation slots")
    identity = asset.get_editor_property("identity_ability")
    identity_type = getattr(unreal, "RPGGreatSwordResolveAbility", None)
    if identity is None:
        require(asset.get_editor_property("identity_input_tag").export_text() == unreal.GameplayTag().export_text(),
                "Preserving a configured identity input")
    else:
        require(identity_type is not None and identity == identity_type.static_class(),
                "Preserving a customized identity ability")
        require(asset.get_editor_property("identity_input_tag").export_text() == make_tag("InputTag.IdentitySkill").export_text(),
                "Preserving a customized identity input")


def check_profile(asset, class_asset):
    require(isinstance(asset, unreal.RPGPlayerClassDevelopmentProfile), "Target profile has another asset type")
    require(asset.get_editor_property("class_definition") == class_asset, "Preserving different profile class")
    require(asset.get_editor_property("data_version") == 1 and asset.get_editor_property("total_sp") == 50,
            "Preserving different profile version/SP")
    entries = list(asset.get_editor_property("acquired_skills"))
    require(len(entries) == len(SKILLS), "Preserving customized explicit acquisition records")
    for entry, row in zip(entries, SKILLS):
        save = entry.get_editor_property("save_data")
        require(entry.get_editor_property("skill_tag").export_text() == make_tag(row[0]).export_text()
                and save.get_editor_property("skill_level") == 1,
                "Preserving customized profile acquisition/levels")
        require(all(value == -1 for value in save.get_editor_property("selected_tripod_indices")),
                "Preserving customized profile tripods")
    require(tag_values(asset.get_editor_property("skill_slots")) == tag_values([make_tag(row[0]) for row in SKILLS]),
            "Preserving customized development slots")


def create_missing(assets, path, asset_class):
    require(not assets.does_asset_exist(path), "Refusing to replace existing asset: " + path)
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", asset_class)
    folder, name = path.rsplit("/", 1)
    result = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, asset_class, factory)
    require(result is not None, "Could not create: " + path)
    return result


def main():
    require(hasattr(unreal, "RPGPlayerClassDefinition") and hasattr(unreal, "RPGPlayerClassDevelopmentProfile"),
            "Normal Editor module build is required before creating class assets")
    assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    targets = {CLASS_PATH, PROFILE_PATH}
    dirty = {str(package.get_name()) for package in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    require(not targets.intersection(dirty), "A target has unsaved changes; preserving it")
    bindings = load_sources(assets)
    class_asset = assets.load_asset(CLASS_PATH) if assets.does_asset_exist(CLASS_PATH) else None
    profile = assets.load_asset(PROFILE_PATH) if assets.does_asset_exist(PROFILE_PATH) else None
    if class_asset is not None:
        check_class(class_asset, bindings)
    if profile is not None:
        require(class_asset is not None, "Existing profile has no matching class asset; preserving it")
        check_profile(profile, class_asset)
    apply = "-RPGCreateGreatSwordClass" in unreal.SystemLibrary.get_command_line()
    created = []
    if apply and class_asset is None:
        class_asset = create_missing(assets, CLASS_PATH, unreal.RPGPlayerClassDefinition)
        class_asset.set_editor_property("class_id", CLASS_ID)
        class_asset.set_editor_property("skills", bindings)
        class_asset.set_editor_property("allowed_weapon_types", [unreal.RPGGladiatorWeaponType.GREAT_SWORD])
        class_asset.set_editor_property("default_skill_slots", [make_tag(row[0]) for row in SKILLS])
        check_class(class_asset, bindings)
        require(assets.save_loaded_asset(class_asset, only_if_is_dirty=False), "Could not save class asset")
        created.append(CLASS_PATH)
    if apply and profile is None:
        profile = create_missing(assets, PROFILE_PATH, unreal.RPGPlayerClassDevelopmentProfile)
        profile.set_editor_property("class_definition", class_asset)
        profile.set_editor_property("data_version", 1)
        profile.set_editor_property("total_sp", 50)
        entries = []
        for tag_name, _, _ in SKILLS:
            save = unreal.RPGSkillSaveData(skill_level=1, selected_tripod_indices=[-1, -1, -1])
            entry = unreal.RPGPlayerClassDevelopmentSkill(skill_tag=make_tag(tag_name), save_data=save)
            entries.append(entry)
        profile.set_editor_property("acquired_skills", entries)
        profile.set_editor_property("skill_slots", [make_tag(row[0]) for row in SKILLS])
        check_profile(profile, class_asset)
        require(assets.save_loaded_asset(profile, only_if_is_dirty=False), "Could not save development profile")
        created.append(PROFILE_PATH)
    unreal.log("RPG_GREATSWORD_CLASS_ASSETS " + json.dumps({
        "apply": apply, "class": CLASS_PATH, "profile": PROFILE_PATH,
        "created": created, "source_skills_modified": False,
    }))


main()
