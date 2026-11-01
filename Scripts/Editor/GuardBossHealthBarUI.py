"""Guard the shared boss health-bar ability's presentation path.

Read-only by default. Back up the saved Blueprint before
-RPGApplyBossHealthBarUI. The normal widget path and original End Ability node
must remain wired exactly as before.
"""
import unreal


PATH = "/Game/Blueprints/Character/Shared/GameplayAbility/GA_NPC_DrawBossHpBar_Base"
PREFIX = "RPG_BOSS_HEALTH_BAR_UI"
blueprint = unreal.load_asset(PATH)
if not isinstance(blueprint, unreal.Blueprint):
    raise RuntimeError("Missing shared boss health-bar GameplayAbility")

graphs = unreal.BlueprintEditorLibrary.list_graphs(blueprint)
matches = [graph for graph in graphs if graph.get_name() == "Gameplay Ability Graph"]
if len(matches) != 1:
    raise RuntimeError("Unexpected health-bar ability graph")
editor = unreal.BlueprintGraphEditor.get_graph_editor(matches[0])
nodes = {node.get_name(): node for node in editor.list_all_nodes()}
event = nodes.get("K2Node_Event_0")
create = nodes.get("K2Node_CreateWidget_0")
text_setter = nodes.get("K2Node_CallFunction_0")
register = nodes.get("K2Node_CallFunction_7")
end = nodes.get("K2Node_CallFunction_4")
if not event or not create or not text_setter or not register or not end or \
        "ActivateAbility" not in str(event.get_node_title()).replace(" ", "") or \
        "CreateWidget" not in str(create.get_node_title()).replace(" ", "") or \
        "SetText" not in str(text_setter.get_node_title()).replace(" ", "") or \
        "RegisterNPCDrawnWidget" not in str(register.get_node_title()).replace(" ", "") or \
        "EndAbility" not in str(end.get_node_title()).replace(" ", ""):
    raise RuntimeError("Unexpected health-bar ability nodes")


def linked(left, right):
    return any(pin.is_same_native_pin(right) for pin in left.list_connected_pins())


def connect(left, right):
    if not left.try_create_connection(right):
        raise RuntimeError("Cannot connect health-bar UI guard")


def old_edges():
    return {(f"{node.get_name()}.{pin.get_pin_name()}",
             f"{other.get_owning_node().get_name()}.{other.get_pin_name()}")
            for node in nodes.values() for pin in node.list_all_pins()
            for other in pin.list_connected_pins()
            if other.get_owning_node().get_name() in nodes}


start = event.find_output_pin("then")
create_input = create.find_execute_pin()
create_done = create.find_output_pin("then")
text_input = text_setter.find_execute_pin()
widget = create.find_output_pin("ReturnValue")
end_input = end.find_execute_pin()
if not linked(register.find_output_pin("then"), end_input) or \
        not linked(widget, nodes["K2Node_VariableGet_0"].find_input_pin("self")) or \
        not linked(widget, nodes["K2Node_Knot_1"].find_input_pin("InputPin")):
    raise RuntimeError("Original health-bar widget or ability completion changed")

apply = "-rpgapplybosshealthbarui" in unreal.SystemLibrary.get_command_line().lower()
if linked(start, create_input):
    if not linked(create_done, text_input) or \
            len(create_input.list_connected_pins()) != 1 or \
            len(text_input.list_connected_pins()) != 1 or \
            len(end_input.list_connected_pins()) != 1:
        raise RuntimeError("Unexpected unguarded health-bar execution path")
    if not apply:
        unreal.log(f"{PREFIX} PREFLIGHT DedicatedAndValidityGuardsRequired=1 NoAssetsSaved=1")
    else:
        before = old_edges()
        removed = {(f"{event.get_name()}.then", f"{create.get_name()}.execute"),
                   (f"{create.get_name()}.execute", f"{event.get_name()}.then"),
                   (f"{create.get_name()}.then", f"{text_setter.get_name()}.execute"),
                   (f"{text_setter.get_name()}.execute", f"{create.get_name()}.then")}
        defaults = {f"{node.get_name()}.{pin.get_pin_name()}": str(pin.get_pin_value())
                    for node in nodes.values() for pin in node.list_all_pins()}
        blueprint.modify()
        dedicated = editor.add_call_function_node("/Script/Engine.KismetSystemLibrary.IsDedicatedServer")
        server_branch = editor.add_branch_node()
        valid = editor.add_call_function_node("/Script/Engine.KismetSystemLibrary.IsValid")
        valid_branch = editor.add_branch_node()
        if not dedicated or not server_branch or not valid or not valid_branch:
            raise RuntimeError("Cannot create health-bar UI guard nodes")
        if not start.break_single_pin_link(create_input) or \
                not create_done.break_single_pin_link(text_input):
            raise RuntimeError("Cannot detach only health-bar UI execution edges")
        connect(dedicated.find_output_pin("ReturnValue"), server_branch.find_input_pin("Condition"))
        connect(start, server_branch.find_execute_pin())
        connect(server_branch.find_output_pin("then"), end_input)
        connect(server_branch.find_output_pin("else"), create_input)
        connect(create_done, valid_branch.find_execute_pin())
        connect(widget, valid.find_input_pin("Object"))
        connect(valid.find_output_pin("ReturnValue"), valid_branch.find_input_pin("Condition"))
        connect(valid_branch.find_output_pin("then"), text_input)
        connect(valid_branch.find_output_pin("else"), end_input)
        if old_edges() != before - removed:
            raise RuntimeError("Original health-bar graph edges changed; refusing save")
        for node in nodes.values():
            for pin in node.list_all_pins():
                if defaults[f"{node.get_name()}.{pin.get_pin_name()}"] != str(pin.get_pin_value()):
                    raise RuntimeError("Original health-bar pin default changed; refusing save")
        if not linked(register.find_output_pin("then"), end_input) or \
                len(end_input.list_connected_pins()) != 3:
            raise RuntimeError("Ability completion path changed; refusing save")
        unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
        if "ERROR" in str(blueprint.get_editor_property("status")).upper() or \
                editor.list_nodes_with_errors():
            raise RuntimeError("Health-bar UI guard compile failed; refusing save")
        if not unreal.EditorAssetLibrary.save_loaded_asset(blueprint, only_if_is_dirty=False):
            raise RuntimeError("Health-bar UI guard save failed")
        unreal.log(f"{PREFIX} APPLIED DedicatedAndNullWidgetEndAbility=1 GameplayEdgesAndDefaultsPreserved=1 OnlyBaseAbilitySaved=1")
else:
    incoming = create_input.list_connected_pins()
    following = text_input.list_connected_pins()
    if len(incoming) != 1 or len(following) != 1 or \
            len(end_input.list_connected_pins()) != 3:
        raise RuntimeError("Ambiguous saved health-bar UI guards")
    server_branch = incoming[0].get_owning_node()
    valid_branch = following[0].get_owning_node()
    server_condition = server_branch.find_input_pin("Condition").list_connected_pins()
    valid_condition = valid_branch.find_input_pin("Condition").list_connected_pins()
    if len(server_condition) != 1 or len(valid_condition) != 1:
        raise RuntimeError("Missing saved health-bar UI guard predicates")
    dedicated = server_condition[0].get_owning_node()
    valid = valid_condition[0].get_owning_node()
    if "IsDedicatedServer" not in str(dedicated.get_node_title()).replace(" ", "") or \
            "IsValid" not in str(valid.get_node_title()).replace(" ", "") or \
            not linked(start, server_branch.find_execute_pin()) or \
            not linked(server_branch.find_output_pin("then"), end_input) or \
            not linked(server_branch.find_output_pin("else"), create_input) or \
            not linked(create_done, valid_branch.find_execute_pin()) or \
            not linked(widget, valid.find_input_pin("Object")) or \
            not linked(valid_branch.find_output_pin("then"), text_input) or \
            not linked(valid_branch.find_output_pin("else"), end_input):
        raise RuntimeError("Saved health-bar UI guards can be bypassed")
    unreal.log(f"{PREFIX} VERIFIED DedicatedAndNullWidgetEndAbility=1 NoAssetsSaved=1")
