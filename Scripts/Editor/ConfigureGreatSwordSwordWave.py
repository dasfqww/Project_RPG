"""Author or inspect the GreatSword Warrior's Sword Wave content in the UE editor."""

import json
import math

import unreal


SKILLS = "/Game/Blueprints/GameData/Skill/"
ABILITIES = "/Game/Blueprints/Character/Player/Abilities/Skill/"
TABLE = SKILLS + "DT_Skill"
DEFINITION = SKILLS + "DA_Skill_SwordWave"
ABILITY = ABILITIES + "GA_Player_SwordWave"
PROJECTILE = ABILITIES + "BP_SwordWaveProjectile"
WARRIOR = "/Game/Blueprints/GameData/PlayerClass/DA_Class_GreatSwordWarrior"
DEVELOPMENT = "/Game/Blueprints/GameData/PlayerClass/Development/DA_GreatSword_Development"
MONTAGE = "/Game/Blueprints/Character/Player/Anim/Montage/SwordWave/AM_SwordWave"
ICON = "/Game/Assets/Textures/UI/Skills/T_SwordProjectile"
VISUAL = "/Game/MixedVFX/Particles/Slashes/SeparateParts/Slashes/NS_VampireSlash_OnlySlash"
TAG = "Player.Ability.Skill.SwordWave"
ROW = "SwordWave"
COOLDOWN = 10.0
DAMAGE_RATIO = 0.70
COMBAT_TAG_FIELDS = (
    "activation_required_tags", "activation_blocked_tags",
    "block_abilities_with_tag", "cancel_abilities_with_tag",
)


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def native(name):
    cls = unreal.load_class(None, "/Script/Project_RPG." + name)
    require(cls, "Missing native class: " + name)
    return cls


def tag(name):
    result = unreal.GameplayTag()
    require(result.import_text('(TagName="{}")'.format(name)), "Invalid tag: " + name)
    return result


def create(path, asset_class, factory):
    folder, name = path.rsplit("/", 1)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, asset_class, factory)
    require(asset, "Failed to create " + path)
    return asset


def blueprint(path, parent):
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", parent)
    asset = create(path, unreal.Blueprint, factory)
    unreal.BlueprintEditorLibrary.compile_blueprint(asset)
    require(asset.generated_class(), "Blueprint failed to compile: " + path)
    return asset


def defaults(asset):
    return unreal.get_default_object(asset.generated_class())


def export_rows(table):
    return json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))


def skill_tag(definition):
    return definition.get_editor_property("skill_tag").export_text()


def main():
    apply = "-RPGAuthorSwordWaveContent" in unreal.SystemLibrary.get_command_line()
    assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    targets = (TABLE, DEFINITION, ABILITY, PROJECTILE, WARRIOR, DEVELOPMENT)
    dirty = {str(package.get_name()) for package in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    require(not dirty.intersection(targets), "A target package has unsaved changes")
    table, warrior, profile, montage, icon, visual = (
        assets.load_asset(path) for path in (TABLE, WARRIOR, DEVELOPMENT, MONTAGE, ICON, VISUAL)
    )
    require(all((table, warrior, profile, montage, icon, visual)), "Missing existing content dependency")
    require(profile.get_editor_property("class_definition") == warrior, "Unexpected development class")
    wave_parent = native("RPGSwordWaveAbility")
    projectile_parent = native("RPGSwordWaveProjectile")
    instant_policy = native("RPGSkillExecutionPolicy_Instant")
    wave_tag = tag(TAG)
    wave_text = wave_tag.export_text()
    empty = unreal.GameplayTag().export_text()
    rows = export_rows(table)
    bindings = list(warrior.get_editor_property("skills"))
    acquisitions = list(profile.get_editor_property("acquired_skills"))
    slots = list(profile.get_editor_property("skill_slots"))
    creation_slots = [slot.export_text() for slot in warrior.get_editor_property("default_skill_slots")]
    charge = assets.load_asset(SKILLS + "DA_Skill_ChargeSlash")
    charge_handle = charge.get_editor_property("skill_data_handle")
    require(charge_handle.get_editor_property("data_table") == table, "Charge data table differs")
    charge_row = next(row for row in rows if row["Name"] == str(charge_handle.get_editor_property("row_name")))
    damage = round(charge_row["AttackPower"] * DAMAGE_RATIO, 2)
    require(0 < damage < charge_row["AttackPower"], "Invalid trial damage")
    template = defaults(assets.load_asset(ABILITIES + "GA_PlayerSkill_AssultBlade"))
    damage_effect = template.get_editor_property("damage_effect_class")
    require(damage_effect, "Existing approved damage effect is missing")

    if apply:
        require(not any(assets.does_asset_exist(path) for path in (DEFINITION, ABILITY, PROJECTILE)),
                "A Sword Wave package already exists. Inspect it without the write flag.")
        require(not any(row["Name"] == ROW for row in rows), "Sword Wave row already exists")
        require(len(bindings) < 8 and all(skill_tag(binding.get_editor_property("definition")) != wave_text for binding in bindings),
                "Class catalog cannot accept a new skill")
        require(all(entry.get_editor_property("skill_tag").export_text() != wave_text for entry in acquisitions),
                "Development profile already grants Sword Wave")
        free_slot = next((i for i, slot in enumerate(slots) if slot.export_text() == empty), len(slots))
        require(free_slot < 8, "Development profile has no free combat slot")

        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", unreal.RPGSkillDefinition)
        definition = create(DEFINITION, unreal.RPGSkillDefinition, factory)
        security = definition.get_editor_property("security_profile")
        security.set_editor_property("maximum_server_hit_distance", 700.0)
        security.set_editor_property("maximum_targets_per_query", 1)
        security.set_editor_property("maximum_hits_per_activation", 1)
        for key, value in {
            "skill_name": "검풍", "skill_tag": wave_tag, "skill_icon": icon,
            "skill_montage": montage, "base_cooldown": COOLDOWN,
            "great_sword_rage_skill": True, "great_sword_rage_cost": 50.0,
            "great_sword_rage_damage_multiplier": 1.15,
            "default_execution_policy_class": instant_policy,
            "skill_data_handle": unreal.DataTableRowHandle(data_table=table, row_name=ROW),
            "security_profile": security,
        }.items():
            definition.set_editor_property(key, value)

        projectile = blueprint(PROJECTILE, projectile_parent)
        component = defaults(projectile).get_editor_property("projectile_niagara_component")
        component.set_asset(visual)
        ability = blueprint(ABILITY, wave_parent)
        ability_defaults = defaults(ability)
        ability_tags = unreal.GameplayTagContainer()
        require(ability_tags.import_text('(GameplayTags=((TagName="{}")))'.format(TAG)), "Invalid ability tags")
        ability_defaults.set_editor_property("ability_tags", ability_tags)
        ability_defaults.set_editor_property("skill_definition", definition)
        ability_defaults.set_editor_property("projectile_class", projectile.generated_class())
        ability_defaults.set_editor_property("damage_effect_class", damage_effect)
        for field in COMBAT_TAG_FIELDS:
            ability_defaults.set_editor_property(field, template.get_editor_property(field))

        new_row = dict(charge_row, Name=ROW, SkillName=ROW, AttackPower=damage,
                       ManaCost=0.0, IdentityGainAmount=20.0, SkillIcon=icon.get_path_name(),
                       ToolTip="전방으로 검풍을 날려 처음 적중한 적에게 피해를 줍니다.")
        require(unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(table, json.dumps(rows + [new_row], ensure_ascii=False)),
                "Failed to import Sword Wave damage row")
        after = export_rows(table)
        require([row for row in after if row["Name"] != ROW] == rows, "Existing table rows changed")

        warrior.set_editor_property("skills", bindings + [
            unreal.RPGPlayerClassSkill(definition=definition, ability_class=ability.generated_class())
        ])
        profile.set_editor_property("acquired_skills", acquisitions + [
            unreal.RPGPlayerClassDevelopmentSkill(
                skill_tag=wave_tag,
                save_data=unreal.RPGSkillSaveData(skill_level=1, selected_refinement_indices=[-1, -1, -1]),
            )
        ])
        if free_slot == len(slots):
            slots.append(wave_tag)
        else:
            slots[free_slot] = wave_tag
        profile.set_editor_property("skill_slots", slots)
    else:
        definition, ability, projectile = (assets.load_asset(path) for path in (DEFINITION, ABILITY, PROJECTILE))
        require(all((definition, ability, projectile)), "Sword Wave has not been authored")

    wave = [row for row in export_rows(table) if row["Name"] == ROW]
    require(len(wave) == 1 and math.isclose(wave[0]["AttackPower"], damage, rel_tol=1e-6)
            and wave[0]["ManaCost"] == 0, "Sword Wave damage row differs")
    handle = definition.get_editor_property("skill_data_handle")
    require(handle.get_editor_property("data_table") == table and str(handle.get_editor_property("row_name")) == ROW,
            "Sword Wave damage handle differs")
    require(skill_tag(definition) == wave_text and definition.get_editor_property("great_sword_rage_skill")
            and definition.get_editor_property("great_sword_rage_cost") == 50.0
            and math.isclose(definition.get_editor_property("great_sword_rage_damage_multiplier"), 1.15, rel_tol=1e-6)
            and definition.get_editor_property("base_cooldown") == COOLDOWN
            and definition.get_editor_property("skill_montage") == montage
            and definition.get_editor_property("default_execution_policy_class") == instant_policy,
            "Sword Wave definition differs")
    ability_defaults = defaults(ability)
    require(isinstance(ability_defaults, unreal.RPGSwordWaveAbility)
            and ability_defaults.get_editor_property("skill_definition") == definition
            and ability_defaults.get_editor_property("projectile_class") == projectile.generated_class()
            and ability_defaults.get_editor_property("damage_effect_class") == damage_effect
            and TAG in ability_defaults.get_editor_property("ability_tags").export_text(),
            "Sword Wave ability defaults differ")
    component = defaults(projectile).get_editor_property("projectile_niagara_component")
    require(isinstance(defaults(projectile), unreal.RPGSwordWaveProjectile) and component.get_asset() == visual,
            "Sword Wave projectile visual differs")
    matches = [binding for binding in warrior.get_editor_property("skills") if binding.get_editor_property("definition") == definition]
    require(len(matches) == 1 and matches[0].get_editor_property("ability_class") == ability.generated_class(),
            "Sword Wave class binding differs")
    require([slot.export_text() for slot in warrior.get_editor_property("default_skill_slots")] == creation_slots
            and wave_text not in creation_slots, "Production creation slots changed")
    acquired_wave = [entry for entry in profile.get_editor_property("acquired_skills")
                     if entry.get_editor_property("skill_tag").export_text() == wave_text]
    active_slots = [slot.export_text() for slot in profile.get_editor_property("skill_slots")]
    require(len(acquired_wave) == 1 and active_slots.count(wave_text) == 1, "Development grant or slot differs")
    if apply:
        for asset in (table, definition, projectile, ability, warrior, profile):
            require(assets.save_loaded_asset(asset, only_if_is_dirty=False), "Failed to save " + asset.get_path_name())
    unreal.log("SWORDWAVE_CONTENT " + json.dumps({
        "saved": len(targets) if apply else 0, "damage": damage,
        "charge_reference": charge_row["AttackPower"], "cooldown": COOLDOWN,
        "rage_cost": 50, "rage_multiplier": 1.15, "development_slot": active_slots.index(wave_text) + 1,
    }))


main()
