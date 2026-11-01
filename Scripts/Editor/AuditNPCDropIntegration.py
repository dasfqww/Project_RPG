"""Read-only audit of the saved Guardian drop ability and its table/pickups."""
import json
import unreal

prefix = "RPG_NPC_DROP_AUDIT"
path = "/Game/Blueprints/Character/NPC/Gruntling/Guardian/BP_Guardian_DropItemAbility"
ability_class = unreal.load_class(None, path + ".BP_Guardian_DropItemAbility_C")
if not ability_class:
    raise RuntimeError("Saved Guardian drop ability is missing")
cdo = unreal.get_default_object(ability_class)
table = cdo.get_editor_property("item_drop_table")
row = str(cdo.get_editor_property("drop_row_name"))
unreal.log(f"{prefix} Ability={ability_class.get_path_name()} Table={table} Row={row}")
unreal.log(f"{prefix} Policy={cdo.get_editor_property('net_execution_policy')} Security={cdo.get_editor_property('net_security_policy')} Instancing={cdo.get_editor_property('instancing_policy')}")
if table:
    rows = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
    selected = [entry for entry in rows if str(entry.get("Name")) == row]
    unreal.log(f"{prefix} SelectedRows={json.dumps(selected)}")
blueprint = unreal.load_asset("/Game/Blueprints/Character/NPC/BP_RPGNPCBase")
editor = unreal.BlueprintGraphEditor.get_graph_editor_by_name(blueprint, "EventGraph")
for node in editor.list_all_nodes():
    title = str(node.get_node_title())
    if "abilit" in title.lower() or "dead" in title.lower():
        unreal.log(f"{prefix} Node={node.get_name()} Title={title.replace(chr(10), ' ')}")
        for pin in node.list_all_pins():
            if pin.get_pin_name() in ("GameplayTagContainer", "GameplayTag", "GameplayAbilityClass", "Tags", "AbilityClass", "AbilityTagToActivate"):
                unreal.log(f"{prefix} Pin={pin.get_pin_name()} Default={pin.get_pin_value()}")
for asset_path in (
        "/Game/Blueprints/Character/NPC/BP_RPGNPCBase",
        "/Game/Blueprints/Character/Shared/GameplayAbility/GA_NPC_Death_Base",
        "/Game/Blueprints/Character/NPC/Gruntling/GameplayAbility/GA_Guardian_Death",
        path):
    asset = unreal.load_asset(asset_path)
    if not isinstance(asset, unreal.Blueprint):
        continue
    default_class = unreal.load_class(None, asset_path + "." + asset_path.rsplit("/", 1)[1] + "_C")
    if default_class:
        default = unreal.get_default_object(default_class)
        for property_name in ("activation_blocked_tags", "activation_required_tags", "ability_tags"):
            try:
                unreal.log(f"{prefix} Tags Asset={asset_path} Property={property_name} Value={default.get_editor_property(property_name)}")
            except Exception:
                pass
    for graph in unreal.BlueprintEditorLibrary.list_graphs(asset):
        graph_editor = unreal.BlueprintGraphEditor.get_graph_editor(graph)
        for node in graph_editor.list_all_nodes():
            unreal.log(f"{prefix} Graph Asset={asset_path} Node={node.get_name()} Title={str(node.get_node_title()).replace(chr(10), ' ')}")
            for pin in node.list_all_pins():
                links = ",".join(f"{other.get_owning_node().get_name()}:{other.get_pin_name()}" for other in pin.list_connected_pins())
                if links or pin.get_pin_value():
                    unreal.log(f"{prefix} Edge Node={node.get_name()} Pin={pin.get_pin_name()} Value={pin.get_pin_value()} Links={links}")
unreal.log(f"{prefix} COMPLETE NoAssetsSaved=1")
