"""Use the game-thread carry snapshot for ABP_Player locomotion selection.

Read-only validation by default. ABP_LAYER_APPLY=True migrates an isolated
ABP_LAYER_PRIVATE_ROOT project; only ABP_Player is saved. ABP_LAYER_PROOF can
select a small JSON report. Run again in a fresh process to verify persistence.
"""
import copy
import json
import os
from pathlib import Path

import unreal

ROOT = "/Game/Blueprints/Character/Player/Anim/"
PATH = ROOT + "ABP_Player"
BLEND = "AnimGraphNode_BlendListByBool_0"
LEGACY = {"K2Node_PropertyAccess_0", "K2Node_CallFunction_0"}
PROPERTY = "bUseArmedLocomotion"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def all_graphs(bp):
    result = {}
    for graph in unreal.BlueprintEditorLibrary.list_graphs(bp):
        editor = unreal.BlueprintGraphEditor.get_graph_editor(graph)
        result[graph.get_path_name().removeprefix(bp.get_path_name())] = {
            node.get_name(): {"class": node.get_class().get_name(), "pins": {
                str(p.get_pin_name()): {"value": p.get_pin_value(), "links": sorted(
                    q.get_owning_node().get_name() + ":" + str(q.get_pin_name())
                    for q in p.list_connected_pins())} for p in node.list_all_pins()}}
            for node in editor.list_all_nodes()}
    return result


def pin(node, name):
    found = [p for p in node.list_all_pins() if str(p.get_pin_name()) == name]
    require(len(found) == 1, "Missing or ambiguous pin: " + name)
    return found[0]


def unchanged_part(graphs):
    result = copy.deepcopy(graphs)
    root = result[":AnimGraph"]
    for name in list(root):
        if name in LEGACY or PROPERTY in root[name]["pins"]:
            del root[name]
    root[BLEND]["pins"]["bActiveValue"]["links"] = []
    return result


def configure():
    bp = unreal.load_asset(PATH)
    require(bp is not None, "Missing ABP_Player")
    before = all_graphs(bp)
    editor = unreal.BlueprintGraphEditor.get_graph_editor_by_name(bp, "AnimGraph")
    nodes = {n.get_name(): n for n in editor.list_all_nodes()}
    saved = False
    if LEGACY & nodes.keys():
        require(os.environ.get("ABP_LAYER_APPLY", "False").lower() == "true",
                "Legacy tag traversal remains; apply in an isolated project")
        root_text = os.environ.get("ABP_LAYER_PRIVATE_ROOT")
        require(root_text, "Apply requires a private project path")
        root = Path(root_text).absolute()
        require(root == Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).absolute(),
                "Private project mismatch")
        target = root / "Content/Blueprints/Character/Player/Anim/ABP_Player.uasset"
        require(target.resolve() == target.absolute(), "Refusing a linked Content path")
        require(not unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages(), "Start with clean packages")
        graph = before[":AnimGraph"]
        require(len(nodes) == 17 and LEGACY <= nodes.keys(), "Unfamiliar legacy topology")
        require(graph[BLEND]["pins"]["bActiveValue"]["links"] == ["K2Node_CallFunction_0:ReturnValue"],
                "Unexpected selection input")
        require(nodes["K2Node_CallFunction_0"].get_node_title() == "IsGameplayTagValid",
                "Unexpected legacy function")
        require(graph["K2Node_PropertyAccess_0"]["pins"]["Value"]["links"] == ["K2Node_CallFunction_0:GameplayTag"],
                "Unexpected legacy traversal consumer")
        replacement = editor.add_get_member_variable_node(PROPERTY)
        require(replacement is not None, "Native snapshot property unavailable")
        replacement.set_node_pos(nodes["K2Node_CallFunction_0"].get_node_pos())
        active = pin(nodes[BLEND], "bActiveValue")
        require(active.break_pin_links(), "Cannot disconnect legacy condition")
        require(pin(replacement, PROPERTY).try_create_connection(active), "Cannot connect snapshot condition")
        editor.remove_nodes([nodes[name] for name in LEGACY])
        saved = True

    compiled = []
    for path in (PATH, ROOT + "ALI_Player", ROOT + "MasterAnimLayer_Player",
                 ROOT + "AnimLayer/MasterAnimLayer_GreatSword", ROOT + "AnimLayer/AnimLayer_GreatSword"):
        item = unreal.load_asset(path)
        require(item is not None, "Missing dependency: " + path)
        unreal.BlueprintEditorLibrary.compile_blueprint(item)
        require(item.get_editor_property("status") == unreal.BlueprintStatus.BS_UP_TO_DATE,
                "Blueprint compile failure/warning: " + path)
        compiled.append(path)
    after = all_graphs(bp)
    graph = after[":AnimGraph"]
    require(not LEGACY & graph.keys() and len(graph) == 16, "Legacy nodes remain or unrelated nodes changed")
    getters = [name for name, node in graph.items() if PROPERTY in node["pins"]]
    require(len(getters) == 1 and graph[getters[0]]["class"] == "K2Node_VariableGet", "Expected a snapshot getter")
    selection = graph[BLEND]["pins"]
    require(selection["bActiveValue"]["links"] == [getters[0] + ":" + PROPERTY], "Wrong locomotion selector")
    require(selection["BlendPose_0"]["links"] == ["AnimGraphNode_LinkedAnimLayer_0:Pose"], "Armed pose changed")
    require(selection["BlendPose_1"]["links"] == ["AnimGraphNode_StateMachine_0:Pose"], "Unarmed pose changed")
    require(float(selection["BlendTime_0"]["value"]) == 0.2 and float(selection["BlendTime_1"]["value"]) == 0.4,
            "Existing transition times changed")
    require(unchanged_part(before) == unchanged_part(after), "Unrelated graph wiring changed")
    cdo = unreal.get_default_object(bp.generated_class())
    require(not cdo.get_editor_property("bHasActiveWeapon") and not cdo.get_editor_property(PROPERTY),
            "Non-neutral animation defaults")
    require(cdo.get_editor_property("WeaponCarryState") == unreal.RPGWeaponCarryState.STOWED,
            "Non-neutral carry default")
    if saved:
        require(unreal.EditorAssetLibrary.save_loaded_asset(bp, only_if_is_dirty=False), "Save failed")
    result = {"saved": [PATH] if saved else [], "compiled": compiled,
              "unrelatedGraphsPreserved": True, "graphs": after}
    if os.environ.get("ABP_LAYER_PROOF"):
        Path(os.environ["ABP_LAYER_PROOF"]).write_text(json.dumps(result, indent=2), encoding="utf-8")
    unreal.log("ABP_LAYER_COMPLETE carry_snapshot compiled=5 saved=" + str(int(saved)))


if __name__ == "__main__":
    configure()
