"""Build and inspect the GreatSword Warrior's two defensive general skills."""

import json
import math
from pathlib import Path
import sys

import unreal

sys.path.insert(0, str(Path(__file__).parent))
from ConfigureGreatSwordQuickSlash import (
    ABILITIES, ANIMS, ASSETS, CLASS_PATH, MONTAGES, PROFILE_PATH, SKILLS, build_skill, check, create, load, prop, rows, tag,
)


SETTINGS = {
    "CounterSlash": dict(label="반격 베기", icon="T_EnhancedWeapon", slot=7, damage=150.0, gauge=12.0,
                         cooldown=11.0, radius=250.0, angle=120.0, targets=3,
                         parent="RPGGreatSwordCounterAbility", strike=0.94,
                         description="짧은 대응 시간에 정면의 근접 공격을 막고 공격자에게 반격합니다."),
    "RetreatSlash": dict(label="후퇴 베기", icon="T_AssultBlade", slot=8, damage=85.0, gauge=6.0,
                         cooldown=9.0, radius=190.0, angle=105.0, targets=2,
                         parent="RPGGameplayAbility_SkillContainer", strike=0.29,
                         description="전방을 베고 뒤로 물러나 거리를 벌립니다."),
}


def copy_sequence(source, path, root_motion):
    animation = ASSETS.duplicate_asset(ANIMS + source, path)
    check(animation is not None, "Could not copy " + source)
    animation.set_editor_property("enable_root_motion", root_motion)
    animation.set_editor_property("force_root_lock", not root_motion)
    unreal.AnimationLibrary.remove_all_animation_notify_tracks(animation)
    return animation


def add_retreat_root(sequence):
    controller = prop(sequence, "controller")
    frames = prop(sequence, "data_model_interface").get_number_of_frames()
    check(frames >= 5, "Retreat sequence has too few frames")
    positions = []
    for index in range(frames + 1):
        progress = min(index / (frames * 0.8), 1.0)
        eased = progress * progress * (3.0 - 2.0 * progress)
        positions.append(unreal.Vector(0.0, -135.0 * eased, 0.0))
    check(controller.set_bone_track_keys("root", positions, [unreal.Quat()] * len(positions),
                                        [unreal.Vector(1.0, 1.0, 1.0)] * len(positions)), "Failed to author retreat root")


def montage_for(first, second, montage_path, counter, hit_time):
    factory = unreal.AnimMontageFactory()
    factory.set_editor_property("source_animation", first)
    factory.set_editor_property("target_skeleton", prop(first, "skeleton"))
    montage = create(montage_path, unreal.AnimMontage, factory)
    tracks = list(prop(montage, "slot_anim_tracks"))
    default_slot = prop(prop(load(MONTAGES + "AM_PlayerSkill"), "slot_anim_tracks")[0], "slot_name")
    tracks[0].set_editor_property("slot_name", default_slot)
    track = prop(tracks[0], "anim_track")
    lead = prop(track, "anim_segments")[0]
    follow = lead.copy()
    lead_source_length = 0.60 if counter else first.get_play_length()
    lead_rate = 1.0 if counter else 1.5
    lead_end = lead_source_length / lead_rate
    lead.set_editor_property("anim_end_time", lead_source_length)
    lead.set_editor_property("anim_play_rate", lead_rate)
    check(follow.import_text(f"(StartPos={lead_end})"), "Could not place second montage segment")
    follow.set_editor_property("anim_reference", second)
    follow.set_editor_property("anim_end_time", second.get_play_length())
    follow.set_editor_property("anim_play_rate", 1.5 if counter else 1.0)
    track.set_editor_property("anim_segments", [lead, follow])
    tracks[0].set_editor_property("anim_track", track)
    montage.set_editor_property("slot_anim_tracks", tracks)
    model = prop(montage, "data_model_interface")
    rate = model.get_frame_rate()
    seconds = lead_end + second.get_play_length() / prop(follow, "anim_play_rate")
    frames = round(seconds * rate.numerator / rate.denominator)
    prop(montage, "controller").set_number_of_frames(unreal.FrameNumber(value=frames))
    unreal.AnimationLibrary.add_animation_notify_track(montage, "Strike")
    cls = unreal.load_class(None, "/Script/Project_RPG.RPGAnimNotify_SendGameplayEvent")
    times = (0.55, hit_time) if counter else (hit_time,)
    for time in times:
        notify = unreal.AnimationLibrary.add_animation_notify_event(montage, "Strike", time, cls)
        check(notify is not None, "Missing montage notify")
        notify.set_editor_property("event_tag", tag("Player.Event.AOE"))
    return montage


def main():
    _, _, params = unreal.SystemLibrary.parse_command_line(unreal.SystemLibrary.get_command_line())
    name = params["RPGDefenseSkill"]
    config = SETTINGS[name]
    apply = "-RPGAuthorDefenseSkill" in unreal.SystemLibrary.get_command_line()
    warrior, profile, table = (load(path) for path in (CLASS_PATH, PROFILE_PATH, SKILLS + "DT_GreatSwordExpansion"))
    folder = MONTAGES + "GreatSwordExpansion/"
    paths = (folder + "A_" + name, folder + "A_" + name + "_Follow", folder + "AM_" + name,
             SKILLS + "DA_Skill_" + name, ABILITIES + "GA_Player_" + name)
    skill_tag = tag("Player.Ability.Skill." + name)
    pos = config["slot"] - 1
    is_counter = name == "CounterSlash"
    if apply:
        check(not any(ASSETS.does_asset_exist(path) for path in paths), "Defensive skill packages already exist")
        check(unreal.load_class(None, "/Script/Project_RPG." + config["parent"]), "Missing defensive ability")
        slots = list(prop(profile, "skill_slots"))
        check(len(slots) < config["slot"] or slots[pos].export_text() == unreal.GameplayTag().export_text(),
              "Development slot is occupied")
        previous = rows(table)
        check(not any(row["Name"] == name for row in previous), "Defensive skill row is occupied")
        first = copy_sequence("Guard" if is_counter else "Combo02_2", paths[0], False)
        follow = copy_sequence("Combo03_1" if is_counter else "Step_B", paths[1], not is_counter)
        if not is_counter:
            add_retreat_root(follow)
        montage = montage_for(first, follow, paths[2], is_counter, config["strike"])
        icon = load("/Game/Assets/Textures/UI/Skills/" + config["icon"])
        settings = {key: config[key] for key in ("label", "cooldown", "radius", "angle", "targets", "parent")}
        definition, ability = build_skill(table, montage, paths[3], paths[4], skill_tag, icon, row=name, **settings)
        reference = next(row for row in previous if row["Name"] == "QuickSlash")
        new_row = dict(reference, Name=name, SkillName=name, AttackPower=config["damage"], IdentityGainAmount=config["gauge"],
                       SkillIcon=icon.get_path_name(), ToolTip=config["description"])
        check(unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(
            table, json.dumps(previous + [new_row], ensure_ascii=False)), "Could not add defensive skill row")
        check([row for row in rows(table) if row["Name"] != name] == previous, "Existing expansion skills changed")
        warrior.set_editor_property("skills", list(prop(warrior, "skills")) + [
            unreal.RPGPlayerClassSkill(definition=definition, ability_class=ability.generated_class())])
        profile.set_editor_property("acquired_skills", list(prop(profile, "acquired_skills")) + [
            unreal.RPGPlayerClassDevelopmentSkill(skill_tag=skill_tag,
                save_data=unreal.RPGSkillSaveData(skill_level=1, selected_refinement_indices=[]))])
        slots += [unreal.GameplayTag()] * max(0, config["slot"] - len(slots))
        slots[pos] = skill_tag
        profile.set_editor_property("skill_slots", slots)
    else:
        first, follow, montage, definition, ability = (load(path) for path in paths)

    check(prop(profile, "skill_slots")[pos].export_text() == skill_tag.export_text(), "Incorrect development slot")
    check(montage.get_first_anim_reference() == first and prop(definition, "skill_montage") == montage, "Incorrect montage")
    check(prop(unreal.get_default_object(ability.generated_class()), "skill_definition") == definition, "Incorrect ability binding")
    if is_counter:
        markers = list(unreal.AnimationLibrary.get_animation_notify_events(montage))
        times = [unreal.AnimationLibrary.get_anim_notify_event_trigger_time(marker) for marker in markers]
        check(len(times) == 2 and abs(times[0] - 0.55) < 0.01 and abs(times[1] - config["strike"]) < 0.01,
              "Incorrect counter montage timing")
    else:
        endpoint = unreal.AnimationLibrary.get_bone_pose_for_time(follow, "root", follow.get_play_length(), False)
        check(abs(endpoint.translation.y + 135) < 0.01 and prop(follow, "enable_root_motion"), "Incorrect retreat root motion")
    if apply:
        for asset in (first, follow, montage, definition, ability, table, warrior, profile):
            check(ASSETS.save_loaded_asset(asset, only_if_is_dirty=False), "Could not save " + asset.get_path_name())
    unreal.log("GREATSWORD_DEFENSE_SKILL " + json.dumps({"skill": name, "saved": 8 if apply else 0,
        "duration": montage.get_play_length(), "slot": config["slot"], "skills": len(prop(warrior, "skills"))}))


main()
