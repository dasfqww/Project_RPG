"""Author (or inspect) GreatSword Warrior's first additional general skill."""

import json
import unreal


ASSETS = unreal.EditorAssetLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
SKILLS = "/Game/Blueprints/GameData/Skill/"
ABILITIES = "/Game/Blueprints/Character/Player/Abilities/Skill/"
MONTAGES = "/Game/Blueprints/Character/Player/Anim/Montage/"
ANIMS = "/Game/Assets/PlayerAnims/GreatSword/Frank_RPG_GreatSword_"
CLASS_PATH = "/Game/Blueprints/GameData/PlayerClass/DA_Class_GreatSwordWarrior"
PROFILE_PATH = "/Game/Blueprints/GameData/PlayerClass/Development/DA_GreatSword_Development"
TAG_NAME = "Player.Ability.Skill.QuickSlash"
ROW = "QuickSlash"
HIT_TIME = 0.23
COOLDOWN = 5.0
DAMAGE = 100.0
GAUGE_GAIN = 8.0


def check(condition, message):
    if not condition:
        raise RuntimeError(message)


def load(path):
    asset = ASSETS.load_asset(path)
    check(asset is not None, "Missing asset " + path)
    return asset


def create(path, asset_class, factory):
    folder, name = path.rsplit("/", 1)
    result = TOOLS.create_asset(name, folder, asset_class, factory)
    check(result is not None, "Could not create " + path)
    return result


def prop(asset, name):
    return asset.get_editor_property(name)


def tag(name):
    value = unreal.GameplayTag()
    check(value.import_text('(TagName="' + name + '")'), "Invalid tag " + name)
    return value


def build_montage(sequence_path, montage_path):
    source = load(ANIMS + "Combo04_2")
    sequence = ASSETS.duplicate_asset(source.get_path_name().split(".")[0], sequence_path)
    check(sequence is not None, "Could not copy the retargeted sequence")
    sequence.set_editor_property("enable_root_motion", False)
    sequence.set_editor_property("force_root_lock", True)
    unreal.AnimationLibrary.remove_all_animation_notify_tracks(sequence)
    factory = unreal.AnimMontageFactory()
    factory.set_editor_property("source_animation", sequence)
    factory.set_editor_property("target_skeleton", prop(sequence, "skeleton"))
    montage = create(montage_path, unreal.AnimMontage, factory)
    tracks = list(prop(montage, "slot_anim_tracks"))
    slot = prop(prop(load(MONTAGES + "AM_PlayerSkill"), "slot_anim_tracks")[0], "slot_name")
    tracks[0].set_editor_property("slot_name", slot)
    montage.set_editor_property("slot_anim_tracks", tracks)
    montage.set_editor_property("rate_scale", 1.15)
    unreal.AnimationLibrary.add_animation_notify_track(montage, "Strike")
    notify_class = unreal.load_class(None, "/Script/Project_RPG.RPGAnimNotify_SendGameplayEvent")
    check(HIT_TIME < montage.get_play_length() and notify_class, "Invalid strike timing or notify type")
    notify = unreal.AnimationLibrary.add_animation_notify_event(montage, "Strike", HIT_TIME, notify_class)
    check(notify is not None, "Could not author strike")
    notify.set_editor_property("event_tag", tag("Player.Event.AOE"))
    return sequence, montage


def build_skill(table, montage, definition_path, ability_path, skill_tag, icon, *,
                row=ROW, label="빠른 횡베기", cooldown=COOLDOWN, radius=205.0,
                angle=115.0, targets=3, parent="RPGGameplayAbility_SkillContainer"):
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.RPGSkillDefinition)
    definition = create(definition_path, unreal.RPGSkillDefinition, factory)
    security = prop(definition, "security_profile")
    security.set_editor_property("maximum_server_hit_distance", radius + 95.0)
    security.set_editor_property("maximum_targets_per_query", targets)
    security.set_editor_property("maximum_hits_per_activation", targets)
    targeting = prop(definition, "targeting_profile")
    shape = prop(targeting, "shape")
    shape.set_editor_property("radius", radius)
    shape.set_editor_property("angle_degrees", angle)
    shape.set_editor_property("half_height", 85.0)
    targeting.set_editor_property("shape", shape)
    hit_filter = prop(targeting, "filter")
    hit_filter.set_editor_property("require_line_of_sight", True)
    hit_filter.set_editor_property("max_results", targets)
    targeting.set_editor_property("filter", hit_filter)
    settings = {
        "skill_name": label, "skill_tag": skill_tag, "skill_icon": icon,
        "skill_montage": montage, "base_cooldown": cooldown,
        "default_execution_policy_class": unreal.load_class(None, "/Script/Project_RPG.RPGSkillExecutionPolicy_Instant"),
        "skill_data_handle": unreal.DataTableRowHandle(data_table=table, row_name=row),
        "security_profile": security, "targeting_profile": targeting, "max_skill_level": 7,
    }
    for key, value in settings.items():
        definition.set_editor_property(key, value)

    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", unreal.load_class(None, "/Script/Project_RPG." + parent))
    ability = create(ability_path, unreal.Blueprint, factory)
    unreal.BlueprintEditorLibrary.compile_blueprint(ability)
    default = unreal.get_default_object(ability.generated_class())
    template = unreal.get_default_object(load(ABILITIES + "GA_PlayerSkill_AssultBlade").generated_class())
    for name in ("activation_required_tags", "activation_blocked_tags", "block_abilities_with_tag",
                 "cancel_abilities_with_tag", "damage_effect_class", "hit_react_tag"):
        default.set_editor_property(name, prop(template, name))
    tags = unreal.GameplayTagContainer()
    check(tags.import_text("(GameplayTags=(" + skill_tag.export_text() + "))"), "Invalid ability tags")
    default.set_editor_property("ability_tags", tags)
    default.set_editor_property("event_tag", tag("Player.Event.AOE"))
    default.set_editor_property("skill_definition", definition)
    return definition, ability


def rows(table):
    return json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))


def main():
    apply = "-RPGAuthorQuickSlash" in unreal.SystemLibrary.get_command_line()
    folder = MONTAGES + "GreatSwordExpansion/"
    sequence_path, montage_path = folder + "A_QuickSlash", folder + "AM_QuickSlash"
    definition_path, ability_path = SKILLS + "DA_Skill_QuickSlash", ABILITIES + "GA_Player_QuickSlash"
    targets = (sequence_path, montage_path, definition_path, ability_path)
    table_path = SKILLS + "DT_GreatSwordExpansion"
    warrior, profile = (load(path) for path in (CLASS_PATH, PROFILE_PATH))
    skill_tag = tag(TAG_NAME)
    slots = list(prop(profile, "skill_slots"))
    creation = [t.export_text() for t in prop(warrior, "default_skill_slots")]
    if apply:
        loaded = [ASSETS.does_asset_exist(path) for path in targets]
        check(all(loaded) or not any(loaded), "Incomplete Quick Slash packages")
        check(not any(prop(entry, "skill_tag").export_text() == skill_tag.export_text()
                      for entry in prop(profile, "acquired_skills")), "Quick Slash is already registered")
        check(len(prop(warrior, "skills")) < 8 and len(slots) <= 8, "GreatSword has no space")
        check(len(slots) <= 5 or slots[5].export_text() == unreal.GameplayTag().export_text(), "Slot six is already occupied")
        source_table = load(SKILLS + "DT_Skill")
        table_exists = ASSETS.does_asset_exist(table_path)
        table = load(table_path) if table_exists else ASSETS.duplicate_asset(source_table.get_path_name().split(".")[0], table_path)
        check(table is not None, "Could not create expansion table")
        existing = rows(table) if table_exists else []
        check(all(row["Name"] != ROW for row in existing), "Quick Slash table row already exists")
        reference = next(row for row in rows(source_table) if row["Name"] == "SwordWave")
        icon = load("/Game/Assets/Textures/UI/Skills/T_RageSlash")
        if all(loaded):
            sequence, montage, definition, ability = (load(path) for path in targets)
            check(prop(definition, "skill_tag").export_text() == skill_tag.export_text() and
                  prop(definition, "skill_montage") == montage, "Unexpected existing Quick Slash packages")
            definition.set_editor_property("skill_data_handle", unreal.DataTableRowHandle(data_table=table, row_name=ROW))
        else:
            sequence, montage = build_montage(sequence_path, montage_path)
            definition, ability = build_skill(table, montage, definition_path, ability_path, skill_tag, icon)
        new_row = dict(reference, Name=ROW, SkillName=ROW, AttackPower=DAMAGE, IdentityGainAmount=GAUGE_GAIN,
                       SkillIcon=icon.get_path_name(), ToolTip="짧게 횡으로 베어 전방의 적에게 피해를 줍니다.")
        check(unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(
            table, json.dumps(existing + [new_row], ensure_ascii=False)), "Could not add Quick Slash table row")
        check([row for row in rows(table) if row["Name"] != ROW] == existing, "Existing skill rows changed")
        warrior.set_editor_property("skills", list(prop(warrior, "skills")) + [
            unreal.RPGPlayerClassSkill(definition=definition, ability_class=ability.generated_class())])
        profile.set_editor_property("acquired_skills", list(prop(profile, "acquired_skills")) + [
            unreal.RPGPlayerClassDevelopmentSkill(skill_tag=skill_tag,
                save_data=unreal.RPGSkillSaveData(skill_level=1, selected_refinement_indices=[]))])
        slots += [unreal.GameplayTag()] * max(0, 6 - len(slots))
        slots[5] = skill_tag
        profile.set_editor_property("skill_slots", slots)
    else:
        table = load(table_path)
        sequence, montage, definition, ability = (load(path) for path in targets)

    expected = skill_tag.export_text()
    check(prop(definition, "skill_tag").export_text() == expected and not prop(definition, "great_sword_rage_skill"),
          "Quick Slash role or tag differs")
    check(prop(definition, "skill_montage") == montage and montage.get_first_anim_reference() == sequence,
          "Quick Slash animation binding differs")
    strikes = list(unreal.AnimationLibrary.get_animation_notify_events(montage))
    check(len(strikes) == 1 and abs(unreal.AnimationLibrary.get_anim_notify_event_trigger_time(strikes[0]) - HIT_TIME) < 0.01,
          "Expected a single strike marker")
    generated = ability.generated_class()
    check(prop(unreal.get_default_object(generated), "skill_definition") == definition,
          "Quick Slash ability has the wrong definition")
    check(any(prop(binding, "definition") == definition and prop(binding, "ability_class") == generated
              for binding in prop(warrior, "skills")), "Quick Slash class binding differs")
    check([t.export_text() for t in prop(warrior, "default_skill_slots")] == creation
          and prop(profile, "skill_slots")[5].export_text() == expected, "Quick Slash slots differ")
    check(any(prop(entry, "skill_tag").export_text() == expected for entry in prop(profile, "acquired_skills")),
          "Development profile is missing Quick Slash")
    row = next(row for row in rows(table) if row["Name"] == ROW)
    check(prop(prop(definition, "skill_data_handle"), "data_table") == table, "Quick Slash data table differs")
    check(row["AttackPower"] == DAMAGE and row["IdentityGainAmount"] == GAUGE_GAIN and row["ManaCost"] == 0,
          "Quick Slash stats differ")
    if apply:
        authored = (definition, table, warrior, profile) if all(loaded) else (
            sequence, montage, definition, ability, table, warrior, profile)
        for asset in authored:
            check(ASSETS.save_loaded_asset(asset, only_if_is_dirty=False), "Could not save " + asset.get_path_name())
    unreal.log("QUICK_SLASH_CONTENT " + json.dumps({
        "saved": len(authored) if apply else 0, "duration": montage.get_play_length(),
        "strike": HIT_TIME, "developmentSlot": 6, "skills": len(prop(warrior, "skills")),
    }))


if __name__ == "__main__":
    main()
