"""Only opt the existing raid HeavyStrike into participant AOE; keep all attack tuning unchanged."""
import unreal

ROOT = "/Game/Blueprints/Character/NPC/SwordMan/GameplayAbility/GA_RaidSwordMan_"
APPLY = "-rpgapplyraidbossareastrike" in unreal.SystemLibrary.get_command_line().lower()


def load_attack(name):
    bp = unreal.load_asset(ROOT + name)
    if not isinstance(bp, unreal.Blueprint):
        raise RuntimeError("Missing raid attack BP: " + name)
    default = unreal.get_default_object(bp.generated_class())
    if not isinstance(default, unreal.RPGBossTelegraphedStrikeAbility):
        raise RuntimeError("Raid attack is not the native timed strike: " + name)
    return bp, default


_, strike = load_attack("Strike")
if strike.get_editor_property("bHitAllThreatParticipants"):
    raise RuntimeError("Standard strike was customized; refusing to change it")
heavy_bp, heavy = load_attack("HeavyStrike")
if APPLY and not heavy.get_editor_property("bHitAllThreatParticipants"):
    heavy_bp.modify()
    heavy.set_editor_property("bHitAllThreatParticipants", True)
    unreal.BlueprintEditorLibrary.compile_blueprint(heavy_bp)
    if "ERROR" in str(heavy_bp.get_editor_property("status")).upper():
        raise RuntimeError("HeavyStrike compile failed; refusing save")
    if not unreal.EditorAssetLibrary.save_loaded_asset(heavy_bp, only_if_is_dirty=False):
        raise RuntimeError("Could not save only the raid HeavyStrike")
    heavy = unreal.get_default_object(heavy_bp.generated_class())
if not heavy.get_editor_property("bHitAllThreatParticipants"):
    raise RuntimeError("HeavyStrike participant AOE not configured")
unreal.log("RAID_AREA_STRIKE_CONFIG COMPLETE HeavyAOE=1 StandardTargeted=1 OnlyHeavyStrikeSaved=1 Applied=" + str(int(APPLY)))
