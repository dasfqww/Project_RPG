"""Read-only Roll direction audit by default; explicit apply changes only its input getter."""
import unreal


PATH = "/Game/Blueprints/Character/Player/Abilities/GA_Player_Roll"
APPLY = "-RPGApplyRaidPlayerRollDirection"
PREFIX = "RAID_ROLL_DIRECTION"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def linked(left, right):
    return any(pin.is_same_native_pin(right) for pin in left.list_connected_pins())


def main():
    blueprint = unreal.EditorAssetLibrary.load_asset(PATH)
    require(isinstance(blueprint, unreal.Blueprint), "Roll Blueprint missing")
    require(blueprint.get_blueprint_parent_class().get_path_name() ==
            "/Script/Project_RPG.RPGPlayerGameplayAbility", "Unexpected Roll parent")
    default = unreal.get_default_object(blueprint.generated_class())
    unreal.log(f"{PREFIX} AUDIT Instancing={default.get_editor_property('instancing_policy')}")
    editor = unreal.BlueprintGraphEditor.get_graph_editor_by_name(
        blueprint, "ComputeRollDirectionAndDistance")
    require(editor, "Roll calculation graph missing")
    nodes = {node.get_name(): node for node in editor.list_all_nodes()}
    legacy = nodes.get("K2Node_CallFunction_1")
    normalize = nodes.get("K2Node_CallFunction_2")
    distance = nodes.get("K2Node_PromotableOperator_0")
    require(legacy and normalize and distance, "Unexpected Roll graph layout")
    require(str(legacy.get_node_title()).replace(" ", "").splitlines()[0] ==
            "GetLastMovementInputVector", "Unexpected legacy direction getter")
    require(distance.find_input_pin("B").get_pin_value() == "400.000000" and
            not distance.find_input_pin("B").list_connected_pins(), "Roll distance changed")
    corrected = [node for node in nodes.values()
                 if str(node.get_node_title()).replace(" ", "").splitlines()[0] ==
                 "GetAbilityMovementDirection"]
    require(len(corrected) <= 1, "Duplicate direction getters")
    input_pin = normalize.find_input_pin("A")
    if corrected and linked(corrected[0].find_output_pin("ReturnValue"), input_pin):
        require(not linked(legacy.find_output_pin("ReturnValue"), input_pin), "Legacy getter still connected")
        unreal.log(f"{PREFIX} VERIFIED Source=GetAbilityMovementDirection Distance=400 NoAssetsSaved=1")
        return
    require(not corrected and linked(legacy.find_output_pin("ReturnValue"), input_pin),
            "Unknown direction wiring; refusing migration")
    if APPLY.lower() not in unreal.SystemLibrary.get_command_line().lower():
        unreal.log(f"{PREFIX} LEGACY Source=GetLastMovementInputVector Distance=400 NoAssetsSaved=1")
        return
    default = unreal.get_default_object(blueprint.generated_class())
    policies = (default.get_editor_property("net_execution_policy"),
                default.get_editor_property("net_security_policy"))
    profile = default.get_editor_property("RollMovementSecurityProfile").export_text()
    sources = legacy.find_input_pin("self").list_connected_pins()
    require(len(sources) == 1, "Player input getter has no unique owner")
    replacement = editor.add_call_function_node("/Script/Project_RPG.RPGPlayer.GetAbilityMovementDirection")
    require(replacement, "Native direction helper missing; build Editor first")
    require(sources[0].try_create_connection(replacement.find_input_pin("self")), "Cannot connect the existing Player owner")
    require(legacy.find_output_pin("ReturnValue").break_single_pin_link(input_pin), "Cannot detach local-only input getter")
    require(replacement.find_output_pin("ReturnValue").try_create_connection(input_pin), "Cannot connect native direction helper")
    blueprint.modify()
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    require("ERROR" not in str(blueprint.get_editor_property("status")).upper() and
            not editor.list_nodes_with_errors(), "Roll Blueprint compile failed")
    default = unreal.get_default_object(blueprint.generated_class())
    require(policies == (default.get_editor_property("net_execution_policy"),
                         default.get_editor_property("net_security_policy")) and
            profile == default.get_editor_property("RollMovementSecurityProfile").export_text(),
            "Roll execution/security profile changed; refusing save")
    require(unreal.EditorAssetLibrary.save_loaded_asset(blueprint, only_if_is_dirty=False), "Roll save failed")
    unreal.log(f"{PREFIX} APPLIED Source=GetAbilityMovementDirection Distance=400 OnlyRollSaved=1")


main()
