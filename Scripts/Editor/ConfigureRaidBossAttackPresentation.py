"""Opt-in raid-only animation/Cue wiring; never save source NPC animations.

Back up the two raid ability BPs before -RPGApplyRaidBossAttackPresentation.
Without the switch, validate saved content only. Existing new assets are reused,
not overwritten. Server WindupSeconds/damage/BT/phase data remain unchanged.
"""
import unreal

ROOT = "/Game/Blueprints/Character/NPC/SwordMan"
FOLDER = ROOT + "/Presentation"
CUE = "/Game/GameplayCue/GC_Boss_AttackWarning"
TAG = "GameplayCue.Boss.AttackWarning"
ENTRIES = (
    ("Strike", "Boss_Attack_SwingAndSlam_RM", 1.644185),
    ("HeavyStrike", "Boss_Attack_Uppercut_RM", 2.896176),
)
APPLY = "-rpgapplyraidbossattackpresentation" in unreal.SystemLibrary.get_command_line().lower()


def require(path, kind):
    obj = unreal.load_asset(path)
    if not isinstance(obj, kind):
        raise RuntimeError("Missing/incompatible raid presentation asset: " + path)
    return obj


def save(obj):
    if not unreal.EditorAssetLibrary.save_loaded_asset(obj, only_if_is_dirty=False):
        raise RuntimeError("Could not save scoped raid presentation asset")


def compile_and_save(bp):
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    if "ERROR" in str(bp.get_editor_property("status")).upper():
        raise RuntimeError("Raid presentation BP compile failed; refusing save")
    save(bp)


def verify_animation(montage):
    sequence = montage.get_first_anim_reference()
    if not isinstance(sequence, unreal.AnimSequence) or \
            sequence.get_editor_property("enable_root_motion") or \
            not sequence.get_editor_property("force_root_lock") or \
            unreal.AnimationLibrary.get_animation_notify_events(sequence) or \
            unreal.AnimationLibrary.get_animation_notify_events(montage):
        raise RuntimeError("Raid presentation must be in-place and free of gameplay/movement notifies")


for name, source_name, hit_time in ENTRIES:
    sequence_path = FOLDER + "/A_RaidSwordMan_" + name
    montage_path = FOLDER + "/AM_RaidSwordMan_" + name
    if APPLY and not unreal.EditorAssetLibrary.does_asset_exist(sequence_path):
        source = require("/Game/Assets/BossyEnemy/Animations/RootMotion/Attacks/" + source_name, unreal.AnimSequence)
        sequence = unreal.EditorAssetLibrary.duplicate_asset(source.get_path_name(), sequence_path)
        if not sequence:
            raise RuntimeError("Could not duplicate only the raid attack sequence")
        sequence.modify()
        sequence.set_editor_property("enable_root_motion", False)
        sequence.set_editor_property("force_root_lock", True)
        unreal.AnimationLibrary.remove_all_animation_notify_tracks(sequence)
        save(sequence)
    sequence = require(sequence_path, unreal.AnimSequence)
    if APPLY and not unreal.EditorAssetLibrary.does_asset_exist(montage_path):
        factory = unreal.AnimMontageFactory()
        factory.set_editor_property("source_animation", sequence)
        factory.set_editor_property("target_skeleton", sequence.get_editor_property("skeleton"))
        montage = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "AM_RaidSwordMan_" + name, FOLDER, unreal.AnimMontage, factory)
        if not montage:
            raise RuntimeError("Could not create raid-only montage")
        # Match the authored SwordMan slot, keeping its AnimBP unchanged.
        original = require(ROOT + "/AnimBP/AM_SwordMan_Attack", unreal.AnimMontage)
        original_tracks = original.get_editor_property("slot_anim_tracks")
        tracks = montage.get_editor_property("slot_anim_tracks")
        if len(tracks) != 1 or not original_tracks:
            raise RuntimeError("Unexpected SwordMan animation slot contract")
        tracks[0].set_editor_property("slot_name", original_tracks[0].get_editor_property("slot_name"))
        montage.set_editor_property("slot_anim_tracks", tracks)
        save(montage)
    montage = require(montage_path, unreal.AnimMontage)
    verify_animation(montage)
    if montage.get_first_anim_reference() != sequence:
        raise RuntimeError("Raid montage does not reference its scoped sequence")
    ability = require(ROOT + "/GameplayAbility/GA_RaidSwordMan_" + name, unreal.Blueprint)
    default = unreal.get_default_object(ability.generated_class())
    tag = unreal.GameplayTag()
    if not tag.import_text('(TagName="' + TAG + '")'):
        raise RuntimeError("Native raid warning GameplayCue tag missing")
    play_rate = hit_time / default.get_editor_property("windup_seconds")
    if APPLY:
        ability.modify()
        default.set_editor_property("presentation_montage", montage)
        default.set_editor_property("presentation_play_rate", play_rate)
        default.set_editor_property("windup_cue_tag", tag)
        default.set_editor_property("recovery_seconds", 0.35)
        compile_and_save(ability)
        default = unreal.get_default_object(ability.generated_class())
    if default.get_editor_property("presentation_montage") != montage or \
            abs(default.get_editor_property("presentation_play_rate") - play_rate) > 0.0001 or \
            default.get_editor_property("windup_cue_tag").export_text() != tag.export_text() or \
            abs(default.get_editor_property("recovery_seconds") - 0.35) > 0.0001:
        raise RuntimeError("Raid ability presentation defaults differ from configured content")

warning = require("/Game/Assets/Niagara/Warning/NS_UnbloackableWarning", unreal.NiagaraSystem)
if APPLY and not unreal.EditorAssetLibrary.does_asset_exist(CUE):
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", unreal.RPGBossAttackWarningCue.static_class())
    cue = unreal.AssetToolsHelpers.get_asset_tools().create_asset("GC_Boss_AttackWarning", "/Game/GameplayCue", unreal.Blueprint, factory)
    if not cue:
        raise RuntimeError("Could not create native raid warning cue BP")
    cue_default = unreal.get_default_object(cue.generated_class())
    cue_default.set_editor_property("warning_system", warning)
    compile_and_save(cue)
cue = require(CUE, unreal.Blueprint)
if cue.get_blueprint_parent_class() != unreal.RPGBossAttackWarningCue.static_class() or \
        unreal.get_default_object(cue.generated_class()).get_editor_property("warning_system") != warning:
    raise RuntimeError("Raid warning cue has unexpected parent or effect")
unreal.log("RAID_ATTACK_PRESENTATION_CONFIG COMPLETE RaidOnly=1 InPlace=1 NoGameplayNotifies=1 OriginalNPCAssetsSaved=0 Applied=" + str(int(APPLY)))
