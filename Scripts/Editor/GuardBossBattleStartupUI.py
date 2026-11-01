"""Read-only by default; opt-in guard for only the legacy startup UI edge.

Back up BP_BossBattleGameMode before -RPGApplyBossStartupUI. Never skip the
BeginPlay event, its gameplay continuation, or change defaults/boss/rewards.
"""
import unreal

PATH = "/Game/Blueprints/GameMode/BP_BossBattleGameMode"
PREFIX = "RPG_BOSS_STARTUP_UI"
blueprint = unreal.load_asset(PATH)
if not isinstance(blueprint, unreal.Blueprint):
    raise RuntimeError("Missing boss battle GameMode Blueprint")
editor = unreal.BlueprintGraphEditor.get_graph_editor_by_name(blueprint, "EventGraph")
nodes = {node.get_name(): node for node in editor.list_all_nodes()}
for node in editor.list_all_nodes():
    title = str(node.get_node_title()).replace("\n", " ")
    unreal.log(f"{PREFIX} NODE {node.get_name()} Title={title}")
    for pin in node.list_all_pins():
        links = pin.list_connected_pins()
        if links:
            unreal.log(f"{PREFIX} PIN {node.get_name()}.{pin.get_pin_name()} -> " +
                       ",".join(f"{other.get_owning_node().get_name()}.{other.get_pin_name()}" for other in links))
unreal.log(f"{PREFIX} AUDIT_COMPLETE NoAssetsSaved=1")

source_node = nodes.get("K2Node_VariableSet_0")
create = nodes.get("K2Node_CreateWidget_0")
viewport = nodes.get("K2Node_CallFunction_8")
if not source_node or not create or not viewport or \
        "CurrentBossBattleState" not in str(source_node.get_node_title()) or \
        "AddToViewport" not in str(viewport.get_node_title()).replace(" ", ""):
    raise RuntimeError("Unexpected startup UI graph; refusing rewrite")
source = source_node.find_output_pin("then")
execute = create.find_execute_pin()
created = create.find_output_pin("then")
target = viewport.find_execute_pin()
value = create.find_output_pin("ReturnValue")

def linked(left, right):
    return any(pin.is_same_native_pin(right) for pin in left.list_connected_pins())

def connect(left, right):
    if not left.try_create_connection(right):
        raise RuntimeError("Cannot connect startup UI guard")

def original_edges():
    return {(f"{node.get_name()}.{pin.get_pin_name()}",
             f"{other.get_owning_node().get_name()}.{other.get_pin_name()}")
            for node in editor.list_all_nodes() if node.get_name() in nodes
            for pin in node.list_all_pins() for other in pin.list_connected_pins()
            if other.get_owning_node().get_name() in nodes}

if not linked(value, viewport.find_input_pin("self")) or viewport.find_output_pin("then").list_connected_pins():
    raise RuntimeError("Unexpected widget value/gameplay continuation")
apply = "-rpgapplybossstartupui" in unreal.SystemLibrary.get_command_line().lower()
if linked(source, execute):
    if not linked(created, target) or len(execute.list_connected_pins()) != 1 or len(target.list_connected_pins()) != 1:
        raise RuntimeError("Unexpected unguarded widget edges")
    if not apply:
        unreal.log(f"{PREFIX} PREFLIGHT DedicatedAndValidityGuardsRequired=1 NoAssetsSaved=1")
    else:
        before = original_edges()
        removed = {(f"{source_node.get_name()}.then", f"{create.get_name()}.execute"),
                   (f"{create.get_name()}.execute", f"{source_node.get_name()}.then"),
                   (f"{create.get_name()}.then", f"{viewport.get_name()}.execute"),
                   (f"{viewport.get_name()}.execute", f"{create.get_name()}.then")}
        defaults = {f"{node.get_name()}.{pin.get_pin_name()}": str(pin.get_pin_value())
                    for node in nodes.values() for pin in node.list_all_pins()}
        blueprint.modify()
        dedicated = editor.add_call_function_node("/Script/Engine.KismetSystemLibrary.IsDedicatedServer")
        server_branch = editor.add_branch_node()
        valid = editor.add_call_function_node("/Script/Engine.KismetSystemLibrary.IsValid")
        valid_branch = editor.add_branch_node()
        if not dedicated or not server_branch or not valid or not valid_branch:
            raise RuntimeError("Cannot create startup UI guard nodes")
        if not source.break_single_pin_link(execute) or not created.break_single_pin_link(target):
            raise RuntimeError("Cannot detach only startup UI edges")
        connect(dedicated.find_output_pin("ReturnValue"), server_branch.find_input_pin("Condition"))
        connect(source, server_branch.find_execute_pin())
        connect(server_branch.find_output_pin("else"), execute)
        connect(created, valid_branch.find_execute_pin())
        connect(value, valid.find_input_pin("Object"))
        connect(valid.find_output_pin("ReturnValue"), valid_branch.find_input_pin("Condition"))
        connect(valid_branch.find_output_pin("then"), target)
        if original_edges() != before - removed:
            raise RuntimeError("Non-UI graph edges changed; refusing save")
        for node in nodes.values():
            for pin in node.list_all_pins():
                if defaults[f"{node.get_name()}.{pin.get_pin_name()}"] != str(pin.get_pin_value()):
                    raise RuntimeError("Existing pin default changed; refusing save")
        unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
        if "ERROR" in str(blueprint.get_editor_property("status")).upper() or editor.list_nodes_with_errors():
            raise RuntimeError("Startup UI guard compile failed; refusing save")
        if not unreal.EditorAssetLibrary.save_loaded_asset(blueprint, only_if_is_dirty=False):
            raise RuntimeError("Startup UI guard save failed")
        unreal.log(f"{PREFIX} APPLIED DedicatedSkipped=1 NullWidgetGuarded=1 GameplayEdgesAndDefaultsPreserved=1 OnlyModeSaved=1")
else:
    incoming = execute.list_connected_pins()
    following = target.list_connected_pins()
    if len(incoming) != 1 or len(following) != 1:
        raise RuntimeError("Ambiguous saved startup UI guards")
    server_branch = incoming[0].get_owning_node()
    valid_branch = following[0].get_owning_node()
    server_condition = server_branch.find_input_pin("Condition").list_connected_pins()
    valid_condition = valid_branch.find_input_pin("Condition").list_connected_pins()
    if len(server_condition) != 1 or len(valid_condition) != 1:
        raise RuntimeError("Missing saved UI guard predicates")
    dedicated = server_condition[0].get_owning_node()
    valid = valid_condition[0].get_owning_node()
    if "IsDedicatedServer" not in str(dedicated.get_node_title()).replace(" ", "") or \
            "IsValid" not in str(valid.get_node_title()).replace(" ", "") or \
            not linked(source, server_branch.find_execute_pin()) or \
            not linked(server_branch.find_output_pin("else"), execute) or \
            server_branch.find_output_pin("then").list_connected_pins() or \
            not linked(created, valid_branch.find_execute_pin()) or \
            not linked(value, valid.find_input_pin("Object")) or \
            not linked(valid_branch.find_output_pin("then"), target) or \
            valid_branch.find_output_pin("else").list_connected_pins():
        raise RuntimeError("Saved startup UI guards can be bypassed")
    unreal.log(f"{PREFIX} VERIFIED DedicatedSkipped=1 NullWidgetGuarded=1 NoAssetsSaved=1")
