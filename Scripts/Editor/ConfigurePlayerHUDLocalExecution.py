"""Audit or opt-in configure only the existing HUD startup ability's execution policy.

Default execution is read-only. In the editor console, import this module and
call configure_player_hud_local_execution(True) after backing up the saved asset.
This does not grant/activate abilities, create widgets, change graphs or start PIE.
"""
import json
import unreal

ASSET = "/Game/Blueprints/Character/Player/Abilities/GA_Player_DrawHUD"
PREFIX = "[RPG_HUD_LOCAL_EXECUTION]"


def _graph_shape(blueprint):
    shape = []
    for graph in unreal.BlueprintEditorLibrary.list_graphs(blueprint):
        editor = unreal.BlueprintGraphEditor.get_graph_editor(graph)
        for node in editor.list_all_nodes():
            pins = []
            for pin in node.list_all_pins():
                links = sorted((other.get_owning_node().get_name(), str(other.get_pin_name()))
                               for other in pin.list_connected_pins())
                pins.append((str(pin.get_pin_name()), links))
            shape.append((graph.get_name(), node.get_name(), str(node.get_node_title()), sorted(pins)))
    return sorted(shape)


def configure_player_hud_local_execution(apply=False):
    blueprint = unreal.load_asset(ASSET)
    if not isinstance(blueprint, unreal.Blueprint):
        raise RuntimeError("Missing existing HUD ability Blueprint")
    defaults = unreal.get_default_object(blueprint.generated_class())
    current = defaults.get_editor_property("net_execution_policy")
    target = unreal.GameplayAbilityNetExecutionPolicy.LOCAL_ONLY
    unchanged_names = ("ability_activation_policy", "activation_policy", "instancing_policy", "net_security_policy")
    unchanged = {name: str(defaults.get_editor_property(name)) for name in unchanged_names}
    saved = False
    if apply and current != target:
        if current != unreal.GameplayAbilityNetExecutionPolicy.LOCAL_PREDICTED:
            raise RuntimeError("Unexpected execution policy; refusing unrelated overwrite")
        if any(package.get_name() == ASSET for package in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()):
            raise RuntimeError("HUD ability has unsaved edits; preserve them before applying")
        shape = _graph_shape(blueprint)
        defaults.set_editor_property("net_execution_policy", target)
        unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
        defaults = unreal.get_default_object(blueprint.generated_class())
        if blueprint.get_editor_property("status") != unreal.BlueprintStatus.BS_UP_TO_DATE:
            raise RuntimeError("HUD ability compile failed; no asset saved")
        if _graph_shape(blueprint) != shape or any(
                str(defaults.get_editor_property(name)) != value for name, value in unchanged.items()):
            raise RuntimeError("Unrelated graph/default change; no asset saved")
        if defaults.get_editor_property("net_execution_policy") != target:
            raise RuntimeError("LocalOnly policy did not survive compilation")
        if not unreal.EditorAssetLibrary.save_loaded_asset(blueprint, False):
            raise RuntimeError("Could not save only the HUD ability asset")
        saved = True
    result = {"asset": ASSET, "before": str(current),
              "after": str(defaults.get_editor_property("net_execution_policy")),
              "local_only": defaults.get_editor_property("net_execution_policy") == target,
              "asset_saved": saved, "other_defaults": unchanged,
              "scope": "HUD ability policy only; no runtime activation/UI/network verification"}
    unreal.log(PREFIX + " " + json.dumps(result, sort_keys=True))
    return result


if __name__ == "__main__":
    configure_player_hud_local_execution()
