"""Guard the NPC death Niagara load, preserving the server lifecycle; opt-in apply.

Back up BP_RPGNPCBase before -RPGApplyNPCDeathPresentation. Default is read-only.
Collision, dissolve timeline, weapon cleanup, drop activation and destruction stay intact.
Dedicated servers must still start the same dissolve timeline without Niagara.
"""
import unreal

PATH = "/Game/Blueprints/Character/NPC/BP_RPGNPCBase"
PREFIX = "RPG_NPC_PRESENTATION"
blueprint = unreal.load_asset(PATH)
if not isinstance(blueprint, unreal.Blueprint):
    raise RuntimeError("Missing ordinary NPC base Blueprint")
editor = unreal.BlueprintGraphEditor.get_graph_editor_by_name(blueprint, "EventGraph")
nodes = {node.get_name(): node for node in editor.list_all_nodes()}
sequence = nodes.get("K2Node_ExecutionSequence_0")
load = nodes.get("K2Node_LoadAsset_0")
lifecycle = nodes.get("K2Node_CallFunction_2")
niagara_setup = nodes.get("K2Node_CallFunction_9")
timeline = nodes.get("K2Node_Timeline_1")
if not sequence or not load or not lifecycle or not niagara_setup or not timeline or \
        "SetPlayRate" not in str(lifecycle.get_node_title()).replace(" ", ""):
    raise RuntimeError("Unexpected ordinary NPC death graph")

def linked(left, right):
    return any(pin.is_same_native_pin(right) for pin in left.list_connected_pins())

def connect(left, right):
    if not left.try_create_connection(right):
        raise RuntimeError("Could not connect scoped NPC presentation guard")

def verify_original_lifecycle():
    if not linked(niagara_setup.find_output_pin("then"), lifecycle.find_execute_pin()) or \
            not linked(lifecycle.find_output_pin("then"), timeline.find_input_pin("PlayFromStart")):
        raise RuntimeError("Original client/timeline edge changed; refusing save")

verify_original_lifecycle()
source = sequence.find_output_pin("then_2")
target = load.find_execute_pin()
guards = [node for node in editor.list_all_nodes() if
          str(node.get_node_title()).splitlines()[0].replace(" ", "") == "IsDedicatedServer"]
apply = "-rpgapplynpcdeathpresentation" in unreal.SystemLibrary.get_command_line().lower()
if not guards:
    if not linked(source, target) or len(target.list_connected_pins()) != 1:
        raise RuntimeError("Unexpected Niagara load execution source; refusing rewrite")
    if not apply:
        unreal.log(f"{PREFIX} PREFLIGHT DedicatedGuardRequired=1 NoAssetsSaved=1")
    else:
        predicate = editor.add_call_function_node("/Script/Engine.KismetSystemLibrary.IsDedicatedServer")
        branch = editor.add_branch_node()
        if not predicate or not branch:
            raise RuntimeError("Cannot create dedicated presentation guard nodes")
        connect(predicate.find_output_pin("ReturnValue"), branch.find_input_pin("Condition"))
        if not source.break_single_pin_link(target):
            raise RuntimeError("Cannot detach only the Niagara load edge")
        connect(source, branch.find_execute_pin())
        connect(branch.find_output_pin("else"), target)
        connect(branch.find_output_pin("then"), lifecycle.find_execute_pin())
        verify_original_lifecycle()
        blueprint.modify()
        unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
        if "ERROR" in str(blueprint.get_editor_property("status")).upper() or editor.list_nodes_with_errors():
            raise RuntimeError("NPC presentation guard compile failed; refusing save")
        if not unreal.EditorAssetLibrary.save_loaded_asset(blueprint, only_if_is_dirty=False):
            raise RuntimeError("NPC presentation guard save failed")
        unreal.log(f"{PREFIX} APPLIED OnlyNPCBaseSaved=1 DedicatedNiagaraSkipped=1 LifecycleEdgesPreserved=1")
else:
    if len(guards) != 1:
        raise RuntimeError("Ambiguous NPC dedicated presentation guards")
    links = guards[0].find_output_pin("ReturnValue").list_connected_pins()
    if len(links) != 1:
        raise RuntimeError("Unconnected dedicated presentation predicate")
    branch = links[0].get_owning_node()
    if not linked(source, branch.find_execute_pin()) or not linked(branch.find_output_pin("else"), target) or \
            len(target.list_connected_pins()) != 1:
        raise RuntimeError("Dedicated NPC Niagara load can bypass guard")
    server_output = branch.find_output_pin("then")
    if not linked(server_output, lifecycle.find_execute_pin()):
        if server_output.list_connected_pins():
            raise RuntimeError("Unexpected dedicated lifecycle edge; refusing rewrite")
        if not apply:
            unreal.log(f"{PREFIX} PREFLIGHT DedicatedLifecycleDisconnected=1 NoAssetsSaved=1")
        else:
            blueprint.modify()
            connect(server_output, lifecycle.find_execute_pin())
            verify_original_lifecycle()
            unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
            if "ERROR" in str(blueprint.get_editor_property("status")).upper() or editor.list_nodes_with_errors():
                raise RuntimeError("NPC lifecycle repair compile failed; refusing save")
            if not unreal.EditorAssetLibrary.save_loaded_asset(blueprint, only_if_is_dirty=False):
                raise RuntimeError("NPC lifecycle repair save failed")
            unreal.log(f"{PREFIX} APPLIED OnlyNPCBaseSaved=1 DedicatedNiagaraSkipped=1 DedicatedTimelineStarted=1 PostDissolveDropPreserved=1")
    else:
        unreal.log(f"{PREFIX} VERIFIED DedicatedNiagaraSkipped=1 DedicatedTimelineStarted=1 LifecycleEdgesPreserved=1 NoAssetsSaved=1")
