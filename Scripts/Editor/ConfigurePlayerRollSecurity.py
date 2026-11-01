"""Attach the real Roll BP montage to a bounded movement AbilityTask.

Default: read-only preflight/verification. Add -RPGApplyPlayerRollSecurity to
apply. Only GA_Player_Roll is saved; unexpected graph layouts are rejected.
Back up that asset before applying with the editor closed.
"""
import math
import unreal


PATH = "/Game/Blueprints/Character/Player/Abilities/GA_Player_Roll"
MONTAGE = "/Game/Blueprints/Character/Player/Anim/Montage/AM_Roll.AM_Roll"
ACTION = "RPG|AbilityTasks|Security|AuthorizedMovementWindow"
REASON = "PlayerRoll"
EXTRA_DISTANCE = 550.0
MARGIN_SECONDS = 0.15
APPLY = "-RPGApplyPlayerRollSecurity"
PROFILE_VARIABLE = "RollMovementSecurityProfile"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def linked(left, right):
    return any(other.is_same_native_pin(right) for other in left.list_connected_pins())


def connect(left, right):
    require(left.try_create_connection(right), "Cannot connect {} -> {}".format(
        left.get_pin_name(), right.get_pin_name()))


def main():
    blueprint = unreal.EditorAssetLibrary.load_asset(PATH)
    require(isinstance(blueprint, unreal.Blueprint), "Missing Roll Blueprint")
    require(blueprint.get_blueprint_parent_class().get_path_name() ==
            "/Script/Project_RPG.RPGPlayerGameplayAbility", "Unexpected Roll parent")
    editors = [unreal.BlueprintGraphEditor.get_graph_editor(graph)
               for graph in unreal.BlueprintEditorLibrary.list_graphs(blueprint)]
    graphs = [editor for editor in editors if editor.find_event_node("K2_ActivateAbility")]
    require(len(graphs) == 1, "Expected one Roll activation graph")
    editor = graphs[0]
    direction = unreal.BlueprintGraphEditor.get_graph_editor_by_name(
        blueprint, "ComputeRollDirectionAndDistance")
    require(direction, "Missing Roll target calculation")
    distance_nodes = [node for node in direction.list_all_nodes()
                      if node.get_name() == "K2Node_PromotableOperator_0"]
    require(len(distance_nodes) == 1 and
            distance_nodes[0].find_input_pin("B").get_pin_value() == "400.000000" and
            not distance_nodes[0].find_input_pin("B").list_connected_pins(),
            "Roll target distance changed; review its security budget")
    nodes = {node.get_name(): node for node in editor.list_all_nodes()}
    montage = nodes.get("K2Node_LatentAbilityCall_0")
    compute = nodes.get("K2Node_CallFunction_2")
    end = nodes.get("K2Node_CallFunction_1")
    require(montage and compute and end, "Unexpected original Roll graph nodes")
    require(montage.find_input_pin("MontageToPlay").get_pin_value() == MONTAGE,
            "Roll montage changed; review its security budget")
    require(montage.find_input_pin("Rate").get_pin_value() == "1.000000",
            "Roll montage rate changed; review its security window")
    require(montage.find_input_pin("bStopWhenAbilityEnds").get_pin_value() == "true",
            "Roll montage must stop on ability cancellation")
    length = unreal.AnimationLibrary.get_sequence_length(unreal.load_asset(MONTAGE))
    require(math.isfinite(length) and 0.1 <= length <= 2.0, "Unexpected Roll montage length")
    duration = length + MARGIN_SECONDS
    windows = [node for node in editor.list_all_nodes()
               if any(str(pin.get_pin_name()) == "MovementProfile"
                      for pin in node.list_all_pins())]
    require(len(windows) <= 1, "Multiple movement windows in Roll graph")
    changed = False
    if not windows:
        require(linked(compute.find_then_pin(), montage.find_execute_pin()),
                "Roll montage has an unexpected execution source")
        if APPLY.lower() not in unreal.SystemLibrary.get_command_line().lower():
            unreal.log("RPG_ROLL_SECURITY PREFLIGHT Duration={:.6f} ExtraDistance={} NoAssetsSaved=1".format(
                duration, EXTRA_DISTANCE))
            return
        window = editor.create_node_from_name(ACTION, unreal.Vector2D(450, -200), [])
        require(window, "Cannot create movement AbilityTask node")
        value = '(bEnabled=True,DurationSeconds={:.6f},ExtraDistance={:.1f},Reason="{}",bResetBaselineWhenFinished=False)'.format(
            duration, EXTRA_DISTANCE, REASON)
        require(editor.add_member_variable(PROFILE_VARIABLE,
                    window.find_input_pin("MovementProfile").get_pin_type(), value),
                "Cannot create Roll security profile variable")
        profile_node = editor.add_get_member_variable_node(PROFILE_VARIABLE)
        require(profile_node, "Cannot read Roll security profile variable")
        connect(profile_node.find_output_pin(PROFILE_VARIABLE),
                window.find_input_pin("MovementProfile"))
        require(compute.find_then_pin().break_single_pin_link(montage.find_execute_pin()),
                "Cannot detach original montage execution edge")
        connect(compute.find_then_pin(), window.find_execute_pin())
        connect(window.find_output_pin("OnAuthorized"), montage.find_execute_pin())
        connect(window.find_output_pin("OnRejected"), end.find_execute_pin())
        comment = editor.add_comment_to_nodes(
            "RPG.Security.PlayerRoll.v1: server 550cm finite budget; start montage only on approval; GAS revokes on end/cancel.",
            [window, profile_node], 40)
        comment.set_editor_property("node_guid", unreal.GuidLibrary.new_guid())
        blueprint.modify()
        changed = True
    else:
        window = windows[0]
        # UE repairs missing legacy NodeGuids during PostLoad, without marking
        # the package dirty. An explicit apply persists those repaired values.
        changed = APPLY.lower() in unreal.SystemLibrary.get_command_line().lower()
    require(linked(compute.find_then_pin(), window.find_execute_pin()), "Roll approval is not reached")
    require(linked(window.find_output_pin("OnAuthorized"), montage.find_execute_pin()),
            "Roll montage does not await approval")
    require(len(montage.find_execute_pin().list_connected_pins()) == 1,
            "Roll montage can bypass approval")
    require(linked(window.find_output_pin("OnRejected"), end.find_execute_pin()),
            "Rejected Roll does not end")
    interruptions = [node for node in editor.list_all_nodes()
                     if str(node.get_node_title()).splitlines()[0].replace(" ", "") == "EndInterruptedMovementAbility"]
    require(len(interruptions) <= 1, "Multiple interrupted movement end nodes")
    if not interruptions:
        for event in ("OnInterrupted", "OnCancelled"):
            require(linked(montage.find_output_pin(event), end.find_execute_pin()),
                    "Unexpected original Roll interruption flow")
        if APPLY.lower() not in unreal.SystemLibrary.get_command_line().lower():
            unreal.log("RPG_ROLL_SECURITY PREFLIGHT NeedsInterruptedEndMigration=1 NoAssetsSaved=1")
            return
        interrupted = editor.add_call_function_node(
            "/Script/Project_RPG.RPGGameplayAbility.EndInterruptedMovementAbility")
        require(interrupted, "Cannot create interrupted movement end node")
        interrupted.set_node_pos(unreal.IntPoint(850, 400))
        for event in ("OnInterrupted", "OnCancelled"):
            require(montage.find_output_pin(event).break_single_pin_link(end.find_execute_pin()),
                    "Cannot detach normal end from interruption")
            connect(montage.find_output_pin(event), interrupted.find_execute_pin())
        blueprint.modify()
        changed = True
    else:
        interrupted = interruptions[0]
    for event in ("OnInterrupted", "OnCancelled"):
        require(linked(montage.find_output_pin(event), interrupted.find_execute_pin()) and
                len(montage.find_output_pin(event).list_connected_pins()) == 1,
                "Roll interruption must end with cancelled status")
    for event in ("OnCompleted", "OnBlendOut"):
        require(linked(montage.find_output_pin(event), end.find_execute_pin()),
                "Roll normal completion flow changed")
    profile_links = window.find_input_pin("MovementProfile").list_connected_pins()
    require(len(profile_links) == 1 and str(profile_links[0].get_pin_name()) == PROFILE_VARIABLE,
            "Roll task does not read the authored security profile")
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    require("ERROR" not in str(blueprint.get_editor_property("status")).upper() and
            not editor.list_nodes_with_errors(), "Roll Blueprint compile failed")
    default = unreal.get_default_object(blueprint.generated_class())
    value = default.get_editor_property(PROFILE_VARIABLE)
    for field, expected in (("DurationSeconds", duration), ("ExtraDistance", EXTRA_DISTANCE)):
        require(abs(value.get_editor_property(field) - expected) < 0.00001,
                "Unexpected Roll security profile field: " + field)
    require(value.get_editor_property("bEnabled") and
            str(value.get_editor_property("Reason")) == REASON and
            not value.get_editor_property("bResetBaselineWhenFinished"),
            "Unsafe Roll security profile")
    require(default.get_editor_property("net_execution_policy") ==
            unreal.GameplayAbilityNetExecutionPolicy.LOCAL_PREDICTED,
            "Roll must retain local prediction")
    policy = default.get_editor_property("net_security_policy")
    server_termination = unreal.GameplayAbilityNetSecurityPolicy.SERVER_ONLY_TERMINATION
    require(policy in (unreal.GameplayAbilityNetSecurityPolicy.CLIENT_OR_SERVER, server_termination),
            "Unexpected Roll network security policy")
    if policy != server_termination:
        if APPLY.lower() not in unreal.SystemLibrary.get_command_line().lower():
            unreal.log("RPG_ROLL_SECURITY PREFLIGHT NeedsServerTermination=1 NoAssetsSaved=1")
            return
        # A delayed client EndAbility must not terminate the server before its
        # latent montage starts. Prediction still finishes locally; GAS refuses
        # client termination requests and the server owns its bounded lifetime.
        default.modify()
        default.set_editor_property("net_security_policy", server_termination)
        blueprint.modify()
        unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
        require("ERROR" not in str(blueprint.get_editor_property("status")).upper() and
                not editor.list_nodes_with_errors(), "Roll policy compile failed")
        default = unreal.get_default_object(blueprint.generated_class())
        changed = True
    require(default.get_editor_property("net_security_policy") == server_termination,
            "Roll server termination policy did not persist")
    if changed:
        require(unreal.EditorAssetLibrary.save_loaded_asset(blueprint, only_if_is_dirty=False),
                "Cannot save Roll Blueprint")
    unreal.log("RPG_ROLL_SECURITY VERIFIED Duration={:.6f} ExtraDistance={} ServerOnlyTermination=1 AssetSaved={}".format(
        duration, EXTRA_DISTANCE, int(changed)))


main()
