"""Guard raid death VFX against dedicated servers and failed component creation.

Default is read-only. Back up BP_RaidNPCBase before
-RPGApplyRaidBossDeathPresentation. Ordinary NPC assets are not modified.
"""
import unreal

PATH = "/Game/Blueprints/Character/NPC/BP_RaidNPCBase"
PREFIX = "RPG_RAID_DEATH_PRESENTATION"
blueprint = unreal.load_asset(PATH)
if not isinstance(blueprint, unreal.Blueprint):
    raise RuntimeError("Missing raid NPC base Blueprint")
if blueprint.get_blueprint_parent_class().get_path_name() != "/Script/Project_RPG.RPGBossCharacter":
    raise RuntimeError("Unexpected raid NPC native parent")
editor = unreal.BlueprintGraphEditor.get_graph_editor_by_name(blueprint, "EventGraph")
nodes = {node.get_name(): node for node in editor.list_all_nodes()}
sequence = nodes.get("K2Node_ExecutionSequence_0")
load = nodes.get("K2Node_LoadAsset_0")
if not sequence or not load:
    raise RuntimeError("Unexpected raid death presentation graph")


def linked(left, right):
    return any(pin.is_same_native_pin(right) for pin in left.list_connected_pins())


def connect(left, right):
    if not left.try_create_connection(right):
        raise RuntimeError("Cannot connect raid death VFX guard")


source = sequence.find_output_pin("then_2")
target = load.find_execute_pin()
guards = [node for node in editor.list_all_nodes() if
          str(node.get_node_title()).splitlines()[0].replace(" ", "") == "IsDedicatedServer"]
apply = "-rpgapplyraidbossdeathpresentation" in unreal.SystemLibrary.get_command_line().lower()
if not guards:
    if not linked(source, target) or len(target.list_connected_pins()) != 1:
        raise RuntimeError("Unexpected raid Niagara load edge; refusing rewrite")
    if not apply:
        unreal.log(f"{PREFIX} PREFLIGHT DedicatedGuardRequired=1 NoAssetsSaved=1")
    else:
        blueprint.modify()
        predicate = editor.add_call_function_node("/Script/Engine.KismetSystemLibrary.IsDedicatedServer")
        branch = editor.add_branch_node()
        if not predicate or not branch:
            raise RuntimeError("Cannot create raid dedicated presentation guard")
        connect(predicate.find_output_pin("ReturnValue"), branch.find_input_pin("Condition"))
        if not source.break_single_pin_link(target):
            raise RuntimeError("Cannot detach only the raid VFX load edge")
        connect(source, branch.find_execute_pin())
        connect(branch.find_output_pin("else"), target)
        unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
        if "ERROR" in str(blueprint.get_editor_property("status")).upper() or editor.list_nodes_with_errors():
            raise RuntimeError("Raid presentation compile failed; refusing save")
        if not unreal.EditorAssetLibrary.save_loaded_asset(blueprint, only_if_is_dirty=False):
            raise RuntimeError("Raid presentation guard save failed")
        unreal.log(f"{PREFIX} APPLIED OnlyRaidNPCBaseSaved=1 DedicatedNiagaraSkipped=1 LifecycleEdgesPreserved=1")
else:
    if len(guards) != 1:
        raise RuntimeError("Ambiguous raid dedicated presentation guards")
    links = guards[0].find_output_pin("ReturnValue").list_connected_pins()
    if len(links) != 1:
        raise RuntimeError("Unconnected raid dedicated presentation predicate")
    branch = links[0].get_owning_node()
    if not linked(source, branch.find_execute_pin()) or not linked(branch.find_output_pin("else"), target) or \
            branch.find_output_pin("then").list_connected_pins() or len(target.list_connected_pins()) != 1:
        raise RuntimeError("Dedicated raid Niagara load can bypass guard")
    unreal.log(f"{PREFIX} VERIFIED DedicatedNiagaraSkipped=1 LifecycleEdgesPreserved=1")

# Do not skip the dissolve timeline when NullRHI/platform limitations cause
# SpawnSystemAttached to return null. Guard only the component setter and
# reconverge on its original continuation in both cases.
spawn = nodes.get("K2Node_CallFunction_6")
setter = nodes.get("K2Node_CallFunction_9")
resume = nodes.get("K2Node_CallFunction_2")
if not spawn or not setter or not resume or \
        "SpawnSystemAttached" not in str(spawn.get_node_title()).replace(" ", "") or \
        "Set Niagara Variable" not in str(setter.get_node_title()) or \
        "SetPlayRate" not in str(resume.get_node_title()).replace(" ", ""):
    raise RuntimeError("Unexpected raid death VFX setter/continuation")
value = spawn.find_output_pin("ReturnValue")
execute = setter.find_execute_pin()
following = resume.find_execute_pin()
if not linked(setter.find_output_pin("then"), following):
    raise RuntimeError("Raid dissolve continuation changed; refusing rewrite")
validity = [node for node in editor.list_all_nodes() if
            str(node.get_node_title()).splitlines()[0].replace(" ", "") == "IsValid" and
            node.find_input_pin("Object") and linked(value, node.find_input_pin("Object"))]
if not validity:
    incoming = execute.list_connected_pins()
    if len(incoming) != 1 or incoming[0].get_owning_node().get_name() != "K2Node_CallFunction_12" or \
            len(following.list_connected_pins()) != 1:
        raise RuntimeError("Unexpected raid VFX setter execution edges")
    if not apply:
        unreal.log(f"{PREFIX} PREFLIGHT ComponentValidityGuardRequired=1 NoAssetsSaved=1")
    else:
        blueprint.modify()
        predicate = editor.add_call_function_node("/Script/Engine.KismetSystemLibrary.IsValid")
        branch = editor.add_branch_node()
        if not predicate or not branch:
            raise RuntimeError("Cannot create raid VFX validity guard")
        connect(value, predicate.find_input_pin("Object"))
        connect(predicate.find_output_pin("ReturnValue"), branch.find_input_pin("Condition"))
        if not incoming[0].break_single_pin_link(execute):
            raise RuntimeError("Cannot detach only the raid VFX setter execution edge")
        connect(incoming[0], branch.find_execute_pin())
        connect(branch.find_output_pin("then"), execute)
        connect(branch.find_output_pin("else"), following)
        unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
        if "ERROR" in str(blueprint.get_editor_property("status")).upper() or editor.list_nodes_with_errors():
            raise RuntimeError("Raid VFX validity compile failed; refusing save")
        if not unreal.EditorAssetLibrary.save_loaded_asset(blueprint, only_if_is_dirty=False):
            raise RuntimeError("Raid VFX validity guard save failed")
        unreal.log(f"{PREFIX} APPLIED ComponentValidityGuard=1 DissolveContinuationPreserved=1 OnlyRaidNPCBaseSaved=1")
else:
    if len(validity) != 1:
        raise RuntimeError("Ambiguous raid VFX validity guards")
    outputs = validity[0].find_output_pin("ReturnValue").list_connected_pins()
    if len(outputs) != 1:
        raise RuntimeError("Unconnected raid VFX validity predicate")
    branch = outputs[0].get_owning_node()
    if not linked(branch.find_output_pin("then"), execute) or \
            not linked(branch.find_output_pin("else"), following) or \
            len(execute.list_connected_pins()) != 1 or len(following.list_connected_pins()) != 2:
        raise RuntimeError("Raid VFX setter can bypass validity guard")
    unreal.log(f"{PREFIX} VERIFIED ComponentValidityGuard=1 DissolveContinuationPreserved=1 NoAssetsSaved=1")

if "-rpginspectraidbossdeathvfx" in unreal.SystemLibrary.get_command_line().lower():
    for node in editor.list_all_nodes():
        title = str(node.get_node_title()).replace(" ", "").replace("\n", "")
        if "SpawnSystemAttached" not in title:
            continue
        unreal.log(f"{PREFIX} SPAWN Node={node.get_name()} Title={node.get_node_title()}")
        for name in ("ReturnValue", "then"):
            pin = node.find_output_pin(name)
            for other in pin.list_connected_pins():
                owner = other.get_owning_node()
                unreal.log(f"{PREFIX} EDGE Pin={name} Node={owner.get_name()} Title={owner.get_node_title()}")
                continuation = owner.find_output_pin("then")
                if continuation:
                    for following in continuation.list_connected_pins():
                        unreal.log(f"{PREFIX} NEXT Node={following.get_owning_node().get_name()} Title={following.get_owning_node().get_node_title()}")
