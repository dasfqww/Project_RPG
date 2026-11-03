"""Split the legacy shared rage graph into a dedicated GreatSword parent.

Run in Unreal Python. Read-only by default; ABP_LAYER_APPLY=True authors only
the three assets below. Apply requires ABP_LAYER_PRIVATE_ROOT to identify an
isolated project with physical (not linked) copies of those assets. Review and
install the resulting packages after releasing shared Content read leases.
ABP_LAYER_PROOF optionally selects the JSON evidence path.

Ordinary child AnimBlueprints inherit their root graph. New weapon families
with different topology should therefore implement ALI_Player in a separate
RPGPlayerLinkedAnimLayer root; data-only variants can inherit that family root.
"""
import copy
import json
import os
from pathlib import Path

import unreal

ROOT = "/Game/Blueprints/Character/Player/Anim/"
COMMON = ROOT + "MasterAnimLayer_Player"
SPECIAL = ROOT + "AnimLayer/MasterAnimLayer_GreatSword"
CHILD = ROOT + "AnimLayer/AnimLayer_GreatSword"
NATIVE = "/Script/Project_RPG.RPGPlayerLinkedAnimLayer"
LAYER = "ArmedLocomotionState"
NORMAL = "Default Locomotion Blend Space"
DIRECTIONAL = "DirectionalLocomotionBlendSpace"
RAGE = "RageLocomotionBlendSpace"
DEFAULTS = {
    NORMAL: ROOT + "BlendSpace/BS_DefaultLocomotion.BS_DefaultLocomotion",
    DIRECTIONAL: ROOT + "BlendSpace/BS_DirectionalLocomotion_GreatSword.BS_DirectionalLocomotion_GreatSword",
    RAGE: ROOT + "BlendSpace/BS_RageLocomotion.BS_RageLocomotion",
}
REMOVED = {
    "AnimGraphNode_BlendListByBool_0", "K2Node_CallFunction_0",
    "AnimGraphNode_BlendSpacePlayer_0", "K2Node_VariableGet_2",
}


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def asset(path):
    value = unreal.load_asset(path)
    require(value is not None, "Missing asset: " + path)
    return value


def layer_editor(bp):
    graphs = [g for g in unreal.BlueprintEditorLibrary.list_graphs(bp) if g.get_name() == LAYER]
    require(len(graphs) == 1, "Expected one owned animation layer: " + bp.get_path_name())
    return unreal.BlueprintGraphEditor.get_graph_editor(graphs[0])


def normalize(value):
    # Duplication remaps self references to the new root class.
    return str(value).replace(SPECIAL + ".MasterAnimLayer_GreatSword", "<ROOT>").replace(
        COMMON + ".MasterAnimLayer_Player", "<ROOT>")


def graph(bp):
    result = {}
    for node in layer_editor(bp).list_all_nodes():
        result[node.get_name()] = {
            "class": node.get_class().get_name(),
            "pins": {str(p.get_pin_name()): {
                "direction": str(p.get_pin_direction()), "value": normalize(p.get_pin_value()),
                "links": sorted(q.get_owning_node().get_name() + ":" + str(q.get_pin_name())
                                for q in p.list_connected_pins()),
            } for p in node.list_all_pins()},
        }
    return result


def defaults(bp, names=DEFAULTS):
    cdo = unreal.get_default_object(bp.generated_class())
    result = {}
    for name in names:
        value = cdo.get_editor_property(name)
        result[name] = value.get_path_name() if value else None
    return result


def compile_bp(bp):
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    status = bp.get_editor_property("status")
    require(status == unreal.BlueprintStatus.BS_UP_TO_DATE,
            "Blueprint compilation failed or warned: " + bp.get_path_name() + " " + str(status))


def generic_graph(original):
    result = copy.deepcopy(original)
    require(len(result) == 14 and REMOVED <= result.keys(), "Unexpected legacy graph shape")
    for name in REMOVED:
        del result[name]
    for node in result.values():
        for pin in node["pins"].values():
            pin["links"] = [link for link in pin["links"] if link.split(":")[0] not in REMOVED]
    result["AnimGraphNode_BlendSpacePlayer_2"]["pins"]["Pose"]["links"] = [
        "AnimGraphNode_BlendListByBool_1:BlendPose_1"]
    result["AnimGraphNode_BlendListByBool_1"]["pins"]["BlendPose_1"]["links"] = [
        "AnimGraphNode_BlendSpacePlayer_2:Pose"]
    return result


def evaluate(nodes, target_lock, rage):
    """Follow actual graph connections to the selected BlendSpace variable."""
    visited = set()

    def source(name, pin):
        links = nodes[name]["pins"][pin]["links"]
        require(len(links) == 1, "Expected a single input: " + name + ":" + pin)
        return links[0].split(":", 1)

    def walk(name):
        require(name not in visited, "Cycle in animation graph")
        visited.add(name)
        node = nodes[name]
        if node["class"] == "AnimGraphNode_Root":
            return walk(source(name, "Result")[0])
        if node["class"] == "AnimGraphNode_BlendListByBool":
            check, _ = source(name, "bActiveValue")
            tag = nodes[check]["pins"]["TagToCheck"]["value"]
            values = {'(TagName="Player.Status.TargetLock")': target_lock,
                      '(TagName="Player.Status.Rage.Active")': rage}
            require(tag in values, "Unexpected branch: " + tag)
            for pin in ("BlendTime_0", "BlendTime_1"):
                require(float(node["pins"][pin]["value"]) == 0.1, "Blend time changed")
            return walk(source(name, "BlendPose_0" if values[tag] else "BlendPose_1")[0])
        require(node["class"] == "AnimGraphNode_BlendSpacePlayer", "Unexpected pose node")
        getter, variable = source(name, "BlendSpace")
        require(nodes[getter]["class"] == "K2Node_VariableGet", "Expected configured BlendSpace")
        return variable

    return walk("AnimGraphNode_Root_0")


def validate():
    common, special, child = [asset(p) for p in (COMMON, SPECIAL, CHILD)]
    for bp in (common, special):
        require(bp.get_blueprint_parent_class().get_path_name() == NATIVE, "Incorrect native parent")
    require(child.get_blueprint_parent_class() == special.generated_class(), "Incorrect GreatSword parent")
    interface = asset(ROOT + "ALI_Player")
    for bp in (common, special, child, interface, asset(ROOT + "ABP_Player")):
        compile_bp(bp)
    for bp in (common, special, child):
        require(unreal.SystemLibrary.does_implement_interface(
            unreal.get_default_object(bp.generated_class()), interface.generated_class()),
            "Missing ALI_Player implementation: " + bp.get_path_name())
    weapon = unreal.get_default_object(asset("/Game/Blueprints/Weapon/BP_PlayerWeapon").generated_class())
    weapon_data = weapon.get_editor_property("player_weapon_data")
    require(weapon_data.get_editor_property("weapon_anim_layer_to_link") == child.generated_class(),
            "Existing weapon no longer resolves the GreatSword layer")
    shared, dedicated = graph(common), graph(special)
    require(shared == generic_graph(dedicated), "Common graph differs beyond the rage removal")
    require(defaults(child) == DEFAULTS, "GreatSword BlendSpace overrides changed")
    require(defaults(common, (NORMAL, DIRECTIONAL)) == {NORMAL: None, DIRECTIONAL: None},
            "Common parent defaults changed")
    cdo = unreal.get_default_object(common.generated_class())
    try:
        cdo.get_editor_property(RAGE)
    except Exception:
        pass
    else:
        raise RuntimeError("Rage property still exists on the common parent")
    require("Rage" not in json.dumps(shared), "Rage reference remains in common graph")
    cases = []
    for lock in (False, True):
        for rage in (False, True):
            chosen = evaluate(dedicated, lock, rage)
            generic = evaluate(shared, lock, rage)
            require(chosen == (DIRECTIONAL if lock else RAGE if rage else NORMAL),
                    "GreatSword branch priority changed")
            require(generic == (DIRECTIONAL if lock else NORMAL), "Common graph is class-specific")
            cases.append({"targetLock": lock, "rage": rage, "greatSword": chosen, "common": generic})
    return {"compiled": 5, "weaponReferencePreserved": True, "interfaceImplementations": 3,
            "commonNodes": len(shared), "greatSwordNodes": len(dedicated),
            "cases": cases, "defaults": defaults(child), "common": shared, "greatSword": dedicated}


def require_private_copy():
    root = Path(os.environ.get("ABP_LAYER_PRIVATE_ROOT", "")).absolute()
    project = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).absolute()
    require(os.environ.get("ABP_LAYER_PRIVATE_ROOT") and root == project,
            "Apply requires an explicitly selected private project")
    for path in (COMMON, SPECIAL, CHILD):
        file = root / "Content" / (path.removeprefix("/Game/") + ".uasset")
        # resolve() detects Windows junctions as well as symlinks, including ancestors.
        require(file.resolve() == file.absolute(), "Refusing to save through a linked Content path")
    dirty = list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    dirty += list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    require(not dirty, "Start with clean packages")


def configure():
    apply = os.environ.get("ABP_LAYER_APPLY", "False").lower() == "true"
    if unreal.EditorAssetLibrary.does_asset_exist(SPECIAL):
        return {"saved": [], "validation": validate()}
    require(apply, "Legacy graph detected; apply in an isolated project to migrate")
    require_private_copy()
    common, child = asset(COMMON), asset(CHILD)
    require(child.get_blueprint_parent_class() == common.generated_class(), "Unexpected original parent")
    before = graph(common)
    generic_graph(before)  # Refuse an unfamiliar topology before any mutation.
    child_defaults = defaults(child)
    require(child_defaults == DEFAULTS, "Unexpected original GreatSword overrides")
    special = unreal.EditorAssetLibrary.duplicate_asset(COMMON, SPECIAL)
    require(special is not None, "Failed to duplicate dedicated root")
    compile_bp(special)
    require(graph(special) == before, "Duplicated graph changed")
    unreal.BlueprintEditorLibrary.reparent_blueprint(child, special.generated_class())
    compile_bp(child)
    cdo = unreal.get_default_object(child.generated_class())
    for name, path in child_defaults.items():
        cdo.set_editor_property(name, asset(path))
    compile_bp(child)
    editor = layer_editor(common)
    nodes = {n.get_name(): n for n in editor.list_all_nodes()}

    def pin(node, name):
        matches = [p for p in nodes[node].list_all_pins() if str(p.get_pin_name()) == name]
        require(len(matches) == 1, "Missing graph pin")
        return matches[0]

    destination = pin("AnimGraphNode_BlendListByBool_1", "BlendPose_1")
    require(destination.break_pin_links(), "Failed to disconnect rage branch")
    require(pin("AnimGraphNode_BlendSpacePlayer_2", "Pose").try_create_connection(destination),
            "Failed to connect ordinary movement")
    editor.remove_nodes([nodes[name] for name in sorted(REMOVED)])
    require(editor.remove_member_variable(RAGE), "Failed to remove shared rage setting")
    result = validate()
    require(result["greatSword"] == before, "Dedicated graph no longer matches original")
    for bp in (special, child, common):
        require(unreal.EditorAssetLibrary.save_loaded_asset(bp, only_if_is_dirty=False),
                "Failed to save " + bp.get_path_name())
    return {"saved": [SPECIAL, CHILD, COMMON], "baseline": before, "validation": result}


def report(result):
    if os.environ.get("ABP_LAYER_PROOF"):
        Path(os.environ["ABP_LAYER_PROOF"]).write_text(json.dumps(result, indent=2), encoding="utf-8")
    unreal.log("ABP_LAYER_COMPLETE " + json.dumps({k: v for k, v in result.items() if k != "baseline" and k != "validation"}))


if __name__ == "__main__":
    report(configure())
