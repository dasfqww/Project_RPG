"""Read-only ChargeSlash input/activation audit; never edits or saves assets."""
import unreal


def show(obj, name):
    try:
        value = obj.get_editor_property(name)
    except Exception as error:
        if "Failed to find property" in str(error):
            return None
        raise
    text = value.export_text() if hasattr(value, "export_text") else str(value)
    unreal.log(f"RAID_CHARGE_INPUT_AUDIT Object={obj.get_path_name()} Property={name} Value={text}")
    return value


unreal.AssetRegistryHelpers.get_asset_registry().wait_for_completion()
config = unreal.load_asset("/Game/Blueprints/Character/Player/Input/DA_InputConfig")
for entry in config.get_editor_property("ability_input_actions"):
    action = entry.get_editor_property("input_action")
    tag = entry.get_editor_property("input_tag")
    unreal.log(f"RAID_CHARGE_INPUT_AUDIT Binding Tag={tag.export_text()} Action={action}")
    if action and ("Skill" in action.get_name() or "Charge" in action.get_name()):
        for prop in ("triggers", "modifiers", "value_type", "consume_input"):
            value = show(action, prop)
            if prop == "triggers" and value:
                for trigger in value:
                    unreal.log(f"RAID_CHARGE_INPUT_AUDIT Trigger={trigger.get_class().get_path_name()}")
                    for setting in ("actuation_threshold", "hold_time_threshold", "chord_action"):
                        show(trigger, setting)
for name in ("GA_Player_ChargeSlash", "GA_PlayerSkill_AssultBlade"):
    blueprint = unreal.load_asset("/Game/Blueprints/Character/Player/Abilities/Skill/" + name)
    default = unreal.get_default_object(blueprint.generated_class())
    for prop in ("net_execution_policy", "net_security_policy", "instancing_policy",
                 "activation_required_tags", "activation_blocked_tags", "cost_gameplay_effect_class",
                 "cooldown_gameplay_effect_class", "ability_activation_policy", "activation_policy",
                 "use_legacy_manual_mana_cost", "ability_tags", "asset_tags"):
        show(default, prop)
unreal.log("RAID_CHARGE_INPUT_AUDIT COMPLETE NoAssetsSaved=1")
