"""Create the optional Experience Ready component Blueprint example.

Run with UnrealEditor-Cmd -run=PythonScript -Script=<this file>.
Requires UE 5.8 EditorToolset. Creates one missing asset; existing assets are
checked and never re-saved. Does not load maps or start/stop PIE.
"""
import json
import unreal
import toolset_registry
from editor_toolset.toolsets.actor import ActorTools
from editor_toolset.toolsets.blueprint import BlueprintTools

ROOT = "/Game/System/Experiences/Examples"
NAME = "BP_ExperienceReadyComponentExample"
PATH = ROOT + "/" + NAME
COMPONENT = "ExperienceReady"
MARKER = "ReadyComponent/v1"
EVENTS = {
    "OnReady": "HandleExperienceReady",
    "OnFailed": "HandleExperienceFailed",
    "OnCancelled": "HandleExperienceCancelled",
}
INPUT = unreal.EdGraphPinDirection.EGPD_INPUT
OUTPUT = unreal.EdGraphPinDirection.EGPD_OUTPUT


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def load_class(path):
    result = unreal.load_class(None, path)
    require(result is not None, "Missing class: " + path)
    return result


def pin(node, name, direction):
    matches = [p for p in node.list_all_pins()
               if str(p.get_pin_name()) == name and p.get_pin_direction() == direction]
    require(len(matches) == 1, "Missing/ambiguous pin " + name + " on " + node.get_node_title())
    return matches[0]


def connect(source, source_name, target, target_name):
    require(pin(source, source_name, OUTPUT).try_create_connection(pin(target, target_name, INPUT)),
            "Could not connect " + source_name + " to " + target_name)


def create_example(ready_class, definition_class):
    blueprint = BlueprintTools.create(ROOT, NAME, unreal.Actor.static_class())
    graph = unreal.BlueprintEditorLibrary.find_event_graph(blueprint)
    for node in list(BlueprintTools.find_nodes(graph)):
        BlueprintTools.delete_node(node)
    component = ActorTools.add_component(blueprint, ready_class, COMPONENT)
    component.set_editor_property("auto_wait_on_begin_play", False)
    component.set_editor_property("wait_timeout_seconds", 65.0)

    for handler_name in EVENTS.values():
        signature = BlueprintTools.add_function_graph(blueprint, handler_name)
        BlueprintTools.add_object_function_param(signature, "Experience", definition_class, True)
        BlueprintTools.add_function_param(signature, "Reason", "string", True)
        handler_editor = unreal.BlueprintGraphEditor.get_graph_editor(signature)
        endpoint = handler_editor.add_return_node()
        endpoint.set_node_pos(unreal.IntPoint(450, 0))
        require(handler_editor.find_graph_entry_pin().try_create_connection(endpoint.find_execute_pin()),
                "Could not create the handler endpoint")
        handler_editor.add_comment_node(
            "Implement feature " + handler_name + " here. The component owns waiting and cleanup.",
            unreal.Vector2D(-30, -130), unreal.Vector2D(750, 80))
    BlueprintTools.compile_blueprint(blueprint, True)

    begin = BlueprintTools.add_event(blueprint, "ReceiveBeginPlay", unreal.IntPoint(0, 0))
    editor = unreal.BlueprintGraphEditor.get_graph_editor(graph)
    getter = editor.add_get_member_variable_node(unreal.Name(COMPONENT))
    start = editor.add_call_function_node("/Script/Project_RPG.RPGExperienceReadyComponent.StartWaiting")
    require(getter is not None and start is not None, "Could not create native component access nodes")
    getter.set_node_pos(unreal.IntPoint(0, 130))
    start.set_node_pos(unreal.IntPoint(350, 0))
    connect(begin, "then", start, "execute")
    connect(getter, COMPONENT, start, "self")

    for index, (event_name, handler_name) in enumerate(EVENTS.items()):
        row = 350 + index * 300
        event = BlueprintTools.add_component_bound_event(component, event_name, graph)
        event.set_node_pos(unreal.IntPoint(0, row))
        call = editor.add_call_function_node(handler_name)
        require(call is not None, "Could not create handler call: " + handler_name)
        call.set_node_pos(unreal.IntPoint(450, row))
        connect(event, "then", call, "execute")
        connect(event, "Experience", call, "Experience")
        connect(event, "Reason", call, "Reason")

    BlueprintTools.compile_blueprint(blueprint, True)
    defaults = BlueprintTools.get_default_object(blueprint)
    tick = defaults.get_editor_property("primary_actor_tick")
    # The compiler inherits Actor's disabled Tick when there is no ReceiveTick event.
    tick.set_editor_property("bStartWithTickEnabled", False)
    defaults.set_editor_property("primary_actor_tick", tick)
    defaults.set_editor_property("replicates", False)
    unreal.EditorAssetLibrary.set_metadata_tag(blueprint, "RPGExperienceExample", MARKER)
    return blueprint


def describe(blueprint):
    defaults = BlueprintTools.get_default_object(blueprint)
    graph = unreal.BlueprintEditorLibrary.find_event_graph(blueprint)
    nodes = list(BlueprintTools.find_nodes(graph))
    edges = []
    descriptions = []
    for node in nodes:
        title = str(node.get_node_title())
        descriptions.append({"title": title, "class": node.get_class().get_name()})
        for output in node.list_all_pins():
            if output.get_pin_direction() != OUTPUT:
                continue
            for target in output.list_connected_pins():
                edges.append({
                    "from": title, "output": str(output.get_pin_name()),
                    "to": str(target.get_owning_node().get_node_title()),
                    "input": str(target.get_pin_name()),
                })
    components = ActorTools.get_components(defaults)
    ready = [c for c in components
             if c.get_class() == load_class("/Script/Project_RPG.RPGExperienceReadyComponent")]
    require(len(ready) == 1, "Example must contain one native Experience Ready component")
    require(unreal.EditorAssetLibrary.get_metadata_tag(blueprint, "RPGExperienceExample") == MARKER,
            "Existing example has different ownership/version metadata; preserve it")
    require(BlueprintTools.get_parent(blueprint) == unreal.Actor.static_class(), "Expected Actor parent")
    require(ready[0].get_name() == COMPONENT + "_GEN_VARIABLE", "Unexpected component name")
    require(not ready[0].get_editor_property("auto_wait_on_begin_play"), "Auto wait must be disabled")
    require(abs(ready[0].get_editor_property("wait_timeout_seconds") - 65.0) < 0.001,
            "Unexpected wait timeout")
    require(not defaults.get_editor_property("primary_actor_tick").get_editor_property("bStartWithTickEnabled"), "Actor tick must be disabled")
    require(not defaults.get_editor_property("replicates"), "Example actor must not replicate")
    handlers = [g.get_name() for g in BlueprintTools.list_graphs(blueprint)
                if g.get_name() in EVENTS.values()]
    require(set(handlers) == set(EVENTS.values()), "Missing feature handlers")
    require(len(nodes) == 9 and len(edges) == 11, "Unexpected graph structure; preserve existing asset")
    require(sum(n["class"] == "K2Node_ComponentBoundEvent" for n in descriptions) == 3,
            "Expected three native component event bindings")
    require(len(BlueprintTools.list_variables(blueprint)) == 0, "Result state belongs to the component")
    return {
        "asset": PATH, "parent": "/Script/Engine.Actor", "native_component": COMPONENT,
        "auto_wait": False, "wait_timeout_seconds": 65.0, "actor_tick": False,
        "replicates": False, "handlers": handlers,
        "nodes": descriptions, "edges": edges,
    }


def main():
    ready_class = load_class("/Script/Project_RPG.RPGExperienceReadyComponent")
    definition_class = load_class("/Script/Project_RPG.RPGExperienceDefinition")
    existing = unreal.EditorAssetLibrary.does_asset_exist(PATH)
    if existing:
        blueprint = unreal.load_asset(PATH)
    else:
        blueprint = create_example(ready_class, definition_class)
    require(isinstance(blueprint, unreal.Blueprint), "Could not create/load the example")
    BlueprintTools.compile_blueprint(blueprint, True)
    report = describe(blueprint)
    if not existing:
        require(unreal.EditorAssetLibrary.save_loaded_asset(blueprint, False), "Could not save new example")
    report["created"] = not existing
    report["compile_status"] = str(blueprint.get_editor_property("status"))
    report["runtime_executed"] = False
    unreal.log("RPG_EXPERIENCE_COMPONENT_EXAMPLE ASSET_READY " + json.dumps(report, sort_keys=True))


if __name__ == "__main__":
    with toolset_registry.tool_raising_exceptions():
        main()

