"""Read-only audit of authored player movement graphs; never saves assets."""
import unreal


ROOT = "/Game/Blueprints/Character/Player/Abilities"
ASSETS = (
    ROOT + "/GA_Player_Roll",
    ROOT + "/Skill/GA_Player_JumpSmash",
    ROOT + "/Skill/GA_PlayerSkill_AssultBlade",
    ROOT + "/Skill/GA_Player_ChargeSlash",
)


def main():
    for path in ASSETS:
        blueprint = unreal.EditorAssetLibrary.load_asset(path)
        if not isinstance(blueprint, unreal.Blueprint):
            raise RuntimeError("Missing player ability Blueprint: " + path)
        unreal.log("RPG_MOVEMENT_BP ASSET {} PARENT {}".format(
            path, blueprint.get_blueprint_parent_class().get_path_name()))
        default = unreal.get_default_object(blueprint.generated_class())
        for name in ("skill_definition", "net_execution_policy", "instancing_policy"):
            try:
                unreal.log("RPG_MOVEMENT_BP DEFAULT {}={}".format(
                    name, default.get_editor_property(name)))
            except Exception:
                pass
        for graph in unreal.BlueprintEditorLibrary.list_graphs(blueprint):
            editor = unreal.BlueprintGraphEditor.get_graph_editor(graph)
            if path.endswith("GA_Player_Roll") and editor.find_event_node("K2_ActivateAbility"):
                for action in editor.list_available_nodes([]):
                    if "AuthorizedMovement" in action:
                        unreal.log("RPG_MOVEMENT_BP AVAILABLE " + action)
            for node in editor.list_all_nodes():
                unreal.log("RPG_MOVEMENT_BP NODE {} {} {}".format(
                    graph.get_name(), node.get_name(), node.get_node_title()))
                for pin in node.list_all_pins():
                    links = ["{}:{}".format(other.get_owning_node().get_name(),
                             other.get_pin_name()) for other in pin.list_connected_pins()]
                    unreal.log("RPG_MOVEMENT_BP PIN {} {} {} VALUE={} LINKS={}".format(
                        node.get_name(), pin.get_pin_name(), pin.get_pin_type_display_string(),
                        pin.get_pin_value(), ",".join(links)))
                    if str(pin.get_pin_name()) == "MontageToPlay" and pin.get_pin_value():
                        montage = unreal.load_asset(pin.get_pin_value())
                        unreal.log("RPG_MOVEMENT_BP MONTAGE_LENGTH {}={}".format(
                            montage.get_path_name(),
                            unreal.AnimationLibrary.get_sequence_length(montage)))
    unreal.log("RPG_MOVEMENT_BP INSPECTION_COMPLETE NoAssetsSaved=1")


main()
