"""Read-only WhirlWind holding policy, montage sections and AOE notify audit."""
import unreal

unreal.AssetRegistryHelpers.get_asset_registry().wait_for_completion()
bp = unreal.load_asset("/Game/Blueprints/Character/Player/Abilities/Skill/GA_Player_WhirlWind")
default = unreal.get_default_object(bp.generated_class())
definition = default.get_editor_property("skill_definition")
for obj, properties in ((default, ("event_tag", "ability_tags", "activation_required_tags")),
    (definition, ("default_execution_policy_class", "default_execution_config", "skill_montage"))):
    for name in properties:
        value = obj.get_editor_property(name)
        text = value.export_text() if hasattr(value, "export_text") else str(value)
        unreal.log(f"RAID_WHIRLWIND_AUDIT Object={obj.get_path_name()} Property={name} Value={text}")
montage = definition.get_editor_property("skill_montage")
# Section metadata is not an editor property; use the exposed native getters.
for index in range(montage.get_num_sections()):
    unreal.log(f"RAID_WHIRLWIND_AUDIT SECTION Index={index} Name={montage.get_section_name(index)}")
for event in unreal.AnimationLibrary.get_animation_notify_events(montage):
    unreal.log(f"RAID_WHIRLWIND_AUDIT NOTIFY {event.export_text()}")
unreal.log("RAID_WHIRLWIND_AUDIT COMPLETE NoAssetsSaved=1")
