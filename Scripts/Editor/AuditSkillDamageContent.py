"""Read-only audit of migrated skill damage defaults, graphs and montage notifies.

No assets or defaults are changed or saved. Run with the Python commandlet.
"""

import unreal
import json


PREFIX = "RPG_SKILL_DAMAGE_AUDIT"
ROOT = "/Game/Blueprints/Character/Player/Abilities/Skill/"
SKILLS = ("GA_Player_ChargeSlash", "GA_PlayerSkill_AssultBlade",
          "GA_Player_JumpSmash", "GA_Player_WhirlWind")


def property_value(obj, name):
    value = obj.get_editor_property(name)
    text = value.export_text() if hasattr(value, "export_text") else str(value)
    unreal.log(f"{PREFIX} PROPERTY Object={obj.get_path_name()} "
               f"Name={name} Value={text}")
    return value


def audit_graphs(blueprint):
    unreal.log(f"{PREFIX} BLUEPRINT Object={blueprint.get_path_name()} "
               f"Parent={blueprint.get_blueprint_parent_class().get_path_name()}")
    for graph in unreal.BlueprintEditorLibrary.list_graphs(blueprint):
        for node in unreal.BlueprintGraphEditor.get_graph_editor(graph).list_all_nodes():
            unreal.log(f"{PREFIX} NODE Graph={graph.get_name()} "
                       f"Name={node.get_name()} Title={node.get_node_title()}")
            for pin in node.list_all_pins():
                links = [f"{other.get_owning_node().get_name()}:{other.get_pin_name()}"
                         for other in pin.list_connected_pins()]
                if links:
                    unreal.log(f"{PREFIX} PIN Node={node.get_name()} "
                               f"Name={pin.get_pin_name()} Value={pin.get_pin_value()} "
                               f"Links={','.join(links)}")


def main():
    unreal.AssetRegistryHelpers.get_asset_registry().wait_for_completion()
    notify_blueprint = unreal.load_asset(
        "/Game/Blueprints/Character/Player/Anim/AnimNotify/AN_SendGameplayEvent")
    audit_graphs(notify_blueprint)
    for name in SKILLS:
        blueprint = unreal.load_asset(ROOT + name)
        if not isinstance(blueprint, unreal.Blueprint):
            raise RuntimeError(f"Missing skill Blueprint: {name}")
        audit_graphs(blueprint)
        default = unreal.get_default_object(blueprint.generated_class())
        for prop in ("damage_effect_class", "event_tag", "skill_name"):
            property_value(default, prop)
        definition = property_value(default, "skill_definition")
        if definition is None:
            raise RuntimeError(f"Missing definition for {name}")
        handle = property_value(definition, "skill_data_handle")
        table = handle.get_editor_property("data_table")
        row_name = str(handle.get_editor_property("row_name"))
        if table is None:
            raise RuntimeError(f"Missing skill damage table for {name}")
        rows = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
        row = next((row for row in rows if row.get("Name") == row_name), None)
        if row is None:
            raise RuntimeError(f"Missing skill damage row for {name}: {row_name}")
        unreal.log(f"{PREFIX} DAMAGE_ROW Skill={name} Row={row_name} "
                   f"AttackPower={row.get('AttackPower')}")
        for prop in ("security_profile", "targeting_profile", "skill_montage"):
            value = property_value(definition, prop)
            if prop == "skill_montage" and value is not None:
                for event in unreal.AnimationLibrary.get_animation_notify_events(value):
                    unreal.log(f"{PREFIX} NOTIFY Montage={value.get_path_name()} "
                               f"Event={event.export_text()}")
                    for notify_prop in ("notify", "notify_state_class"):
                        notify = event.get_editor_property(notify_prop)
                        if notify is not None:
                            unreal.log(f"{PREFIX} NOTIFY_OBJECT Object={notify.get_path_name()} "
                                       f"Class={notify.get_class().get_path_name()}")
                            for tag_prop in ("event_tag", "EventTag", "Event Tag", "begin_event_tag",
                                             "tick_event_tag", "end_event_tag"):
                                try:
                                    property_value(notify, tag_prop)
                                except Exception as error:
                                    if "Failed to find property" not in str(error):
                                        raise
    unreal.log(f"{PREFIX} COMPLETE NoAssetsSaved=1")


main()
