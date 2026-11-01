"""Audit raid-only sight config; apply an explicit loss-radius correction.

Headless UnrealEditor-Cmd -run=pythonscript -script=... -NullRHI.
Without -RPGApplyRaidSwordManPerception this is read-only (and rejects a bad
loss radius). Never saves the original SwordMan controller or native defaults.
"""
import math
import unreal

ROOT = "/Game/Blueprints/Character/NPC/SwordMan"
RAID = ROOT + "/AIC_RaidSwordMan"
ORIGINAL = ROOT + "/AIC_SwordMan"
APPLY = "-RPGApplyRaidSwordManPerception"


def require(path):
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if not isinstance(asset, unreal.Blueprint):
        raise RuntimeError("Missing controller Blueprint: " + path)
    return asset


def sight_for(default):
    perception = default.get_editor_property("NPCPerceptionComponent")
    if perception is None:
        raise RuntimeError("Missing native perception component")
    configs = list(perception.get_editor_property("senses_config"))
    sights = [config for config in configs if isinstance(config, unreal.AISenseConfig_Sight)]
    if len(sights) != 1:
        raise RuntimeError("Expected exactly one configured sight sense: " + str(configs))
    # The inherited auxiliary AISenseConfig_Sight pointer can be null in saved
    # BP defaults. The perception component's instanced config is what UE uses.
    return sights[0]


def values(sight):
    return tuple(sight.get_editor_property(name) for name in (
        "sight_radius", "lose_sight_radius", "peripheral_vision_angle_degrees"))


def main():
    raid_bp = require(RAID)
    default = unreal.get_default_object(raid_bp.generated_class())
    sight = sight_for(default)
    original = sight_for(unreal.get_default_object(require(ORIGINAL).generated_class()))
    original_values = values(original)
    before = values(sight)
    unreal.log_warning("RAIDPERCEPTION audit Raid={} Outer={} Sight={} LoseSight={} HalfAngle={} Original={}".format(
        sight.get_path_name(), sight.get_outer().get_path_name(), *before, original_values))
    if APPLY.lower() in unreal.SystemLibrary.get_command_line().lower():
        if not sight.get_path_name().startswith(default.get_path_name() + ":"):
            raise RuntimeError("Sight is not a raid-owned default subobject; refusing to edit an inherited template")
        radius = before[0]
        if not math.isfinite(radius) or radius <= 0.0:
            raise RuntimeError("Invalid authored sight radius")
        # Once seen, UE sight uses LoseSightRadius rather than SightRadius.
        # Zero forces a visible non-coincident target to be lost on next update.
        if not math.isfinite(before[1]) or before[1] < radius:
            sight.modify()
            sight.set_editor_property("lose_sight_radius", radius + 500.0)
            default.modify()
            raid_bp.modify()
            unreal.BlueprintEditorLibrary.compile_blueprint(raid_bp)
            if "ERROR" in str(raid_bp.get_editor_property("status")).upper():
                raise RuntimeError("Raid controller Blueprint compile failed")
            if not unreal.EditorAssetLibrary.save_loaded_asset(raid_bp, only_if_is_dirty=False):
                raise RuntimeError("Cannot save raid controller")
            sight = sight_for(unreal.get_default_object(raid_bp.generated_class()))
    after = values(sight)
    if values(original) != original_values:
        raise RuntimeError("Original SwordMan sight changed")
    if not all(math.isfinite(value) for value in after) or after[0] <= 0.0 or after[1] < after[0]:
        raise RuntimeError("Raid loss radius must be at least the acquisition radius")
    unreal.log_warning("RAIDPERCEPTION verified Sight={} LoseSight={} OriginalUnchanged=1".format(after[0], after[1]))


main()
