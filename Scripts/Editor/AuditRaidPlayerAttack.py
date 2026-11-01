"""Read-only authored default attack, weapon/input and montage-notify audit."""
import json
import unreal


def main():
    root = "/Game/Blueprints/Character/Player"
    settings = unreal.GameMapsSettings.get_game_maps_settings()
    unreal.log("RAID_ATTACK_AUDIT CONFIG_GAME_INSTANCE " + settings.get_editor_property("game_instance_class").export_text())
    instance_bp = unreal.load_asset("/Game/Generic/BP_RPGGameInstance")
    instance_default = unreal.get_default_object(instance_bp.generated_class())
    unreal.log("RAID_ATTACK_AUDIT AUTHORED_GAME_INSTANCE SkillTable=" +
               str(instance_default.get_editor_property("skill_data_table")))
    for graph in unreal.BlueprintEditorLibrary.list_graphs(instance_bp):
        editor = unreal.BlueprintGraphEditor.get_graph_editor(graph)
        for node in editor.list_all_nodes():
            unreal.log(f"RAID_ATTACK_AUDIT INSTANCE_NODE {graph.get_name()} {node.get_node_title()}")
    weapon = unreal.load_asset("/Game/Blueprints/Weapon/BP_PlayerWeapon")
    default = unreal.get_default_object(weapon.generated_class())
    data = default.get_editor_property("player_weapon_data")
    unreal.log("RAID_ATTACK_AUDIT WEAPON " + data.export_text())
    for box in default.get_components_by_class(unreal.BoxComponent):
        for name in ("relative_location", "relative_rotation", "relative_scale3d", "box_extent", "generate_overlap_events"):
            unreal.log(f"RAID_ATTACK_AUDIT BOX {name}={box.get_editor_property(name)}")
        unreal.log(f"RAID_ATTACK_AUDIT BOX CollisionProfile={box.get_collision_profile_name()} PawnResponse={box.get_collision_response_to_channel(unreal.CollisionChannel.ECC_PAWN)}")
        boss_bp = unreal.load_asset("/Game/Blueprints/Character/NPC/SwordMan/BP_RaidSwordMan")
        boss = unreal.get_default_object(boss_bp.generated_class())
        for component in boss.get_components_by_class(unreal.PrimitiveComponent):
            unreal.log(f"RAID_ATTACK_AUDIT BOSS_COLLISION Component={component.get_name()} Profile={component.get_collision_profile_name()} Enabled={component.get_collision_enabled()} Overlaps={component.get_editor_property('generate_overlap_events')} WeaponResponse={component.get_collision_response_to_channel(box.get_collision_object_type())}")
    definition = unreal.load_asset("/Game/Blueprints/GameData/Item/Weapons/DA_Item_GreatSword")
    for fragment in definition.get_editor_property("fragments"):
        if isinstance(fragment, unreal.RPGItemEquipmentDefinitionFragment):
            for name in ("attach_socket", "equipped_actor_relative_transform"):
                unreal.log(f"RAID_ATTACK_AUDIT FRAGMENT {name}={fragment.get_editor_property(name)}")
    path = root + "/Abilities/DefaultAttack/GA_Player_DefaultAttack"
    attack = unreal.load_asset(path)
    unreal.log("RAID_ATTACK_AUDIT PARENT " + attack.get_blueprint_parent_class().get_path_name())
    default = unreal.get_default_object(attack.generated_class())
    for name in ("net_execution_policy", "net_security_policy", "instancing_policy",
                 "damage_effect_class", "skill_name", "event_tag",
                 "default_attack_section_data", "default_attack_section_data_rage"):
        value = default.get_editor_property(name)
        unreal.log(f"RAID_ATTACK_AUDIT DEFAULT {name}=" +
                   (value.export_text() if hasattr(value, "export_text") else str(value)))
    damage = unreal.get_default_object(default.get_editor_property("damage_effect_class"))
    for name in ("duration_policy", "modifiers", "executions", "ge_components"):
        value = damage.get_editor_property(name)
        unreal.log(f"RAID_ATTACK_AUDIT DAMAGE {name}={value}")
    for execution in damage.get_editor_property("executions"):
        unreal.log("RAID_ATTACK_AUDIT DAMAGE_EXECUTION " + execution.export_text())
    for graph in unreal.BlueprintEditorLibrary.list_graphs(attack):
        editor = unreal.BlueprintGraphEditor.get_graph_editor(graph)
        for node in editor.list_all_nodes():
            unreal.log(f"RAID_ATTACK_AUDIT NODE {graph.get_name()} {node.get_node_title()}")
    collision = unreal.load_asset(root + "/Anim/AnimNotifyState/ANS_ToggleWeaponCollision")
    for graph in unreal.BlueprintEditorLibrary.list_graphs(collision):
        editor = unreal.BlueprintGraphEditor.get_graph_editor(graph)
        for node in editor.list_all_nodes():
            unreal.log(f"RAID_ATTACK_AUDIT COLLISION_NODE {graph.get_name()} {node.get_name()} {node.get_node_title()}")
            for pin in node.list_all_pins():
                links = [f"{other.get_owning_node().get_name()}:{other.get_pin_name()}"
                         for other in pin.list_connected_pins()]
                unreal.log(f"RAID_ATTACK_AUDIT COLLISION_PIN {node.get_name()} {pin.get_pin_name()} Value={pin.get_pin_value()} Links={links}")
    montage = unreal.load_asset(root + "/Anim/Montage/AM_GreatSword_DefaultAttack")
    for event in unreal.AnimationLibrary.get_animation_notify_events(montage):
        unreal.log("RAID_ATTACK_AUDIT NOTIFY " + event.export_text())
        state = event.get_editor_property("notify_state_class")
        if state:
            unreal.log("RAID_ATTACK_AUDIT STATE " + state.get_class().get_path_name())
        if isinstance(state, unreal.RPGAnimNotifyState_PerformTrace):
            for name in ("event_tag", "weapon_hand_type", "execute_net_role"):
                try:
                    unreal.log(f"RAID_ATTACK_AUDIT TRACE {name}={state.get_editor_property(name)}")
                except Exception as error:
                    unreal.log(f"RAID_ATTACK_AUDIT TRACE {name} unavailable: {error}")
    config = unreal.load_asset(root + "/Input/DA_InputConfig")
    unreal.log("RAID_ATTACK_AUDIT INPUT " +
               str(config.get_editor_property("ability_input_actions")))
    table = unreal.load_asset("/Game/Blueprints/GameData/Skill/DT_Skill")
    rows = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
    unreal.log("RAID_ATTACK_AUDIT DEFAULT_ATTACK_ROW " +
               str([row for row in rows if row.get("Name") == "DefaultAttack"]))
    unreal.log("RAID_ATTACK_AUDIT COMPLETE NoAssetsSaved=1")


main()
