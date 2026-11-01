"""Read-only snapshots for this task's skill/inventory coexistence PIE session."""

import json
import __main__
import time
import unreal


_export_name = "_rpg_window_coexist_capture_20261016"
_cleanup_name = "_rpg_window_coexist_cleanup_20261016"
if hasattr(__main__, _export_name) or hasattr(__main__, _cleanup_name):
    raise RuntimeError("This task's inspection callback already exists; do not overwrite it")

_world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
if _world is None:
    raise RuntimeError("Start this task's PIE session before installing its inspector")
_game_instance_path = unreal.GameplayStatics.get_game_instance(_world).get_path_name()


def capture_window_coexist(stage, emit=True):
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    if world is None:
        raise RuntimeError("The inspected PIE session has ended")
    game_instance = unreal.GameplayStatics.get_game_instance(world).get_path_name()
    if game_instance != _game_instance_path:
        raise RuntimeError("PIE identity changed; refusing to inspect another session")
    controller = unreal.GameplayStatics.get_player_controller(world, 0)
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
    component_class = unreal.load_class(None, "/Script/Project_RPG.RPGSkillWindowComponent")
    component = controller.get_component_by_class(component_class)
    skill_class = unreal.load_class(None, "/Script/Project_RPG.RPGSkillWindow")
    inventory_class = unreal.load_class(None, "/Script/Project_RPG.RPGInventoryProjectionWidget")

    def windows_of(widget_class):
        return [
            {"path": widget.get_path_name(), "visible": widget.is_visible(),
             "active": widget.is_activated(),
             "focused": widget.has_user_focus(controller),
             "focused_descendants": widget.has_user_focused_descendants(controller)}
            for widget in unreal.WidgetLibrary.get_all_widgets_of_class(world, widget_class, False)
            if widget.is_in_viewport()
        ]

    result = {
        "stage": stage,
        "game_instance": game_instance,
        "controller": controller.get_path_name(),
        "pawn": pawn.get_path_name() if pawn else None,
        "skill_open": component.is_window_open() if component else None,
        "cursor": controller.get_editor_property("show_mouse_cursor"),
        "move_locked": controller.is_move_input_ignored(),
        "look_locked": controller.is_look_input_ignored(),
        "skill_windows": windows_of(skill_class),
        "inventory_windows": windows_of(inventory_class),
        "toggle_keys": {},
    }
    try:
        config = controller.get_editor_property("InputConfigDataAsset")
        for row in config.get_editor_property("NativeInputActions"):
            tag = row.get_editor_property("InputTag")
            name = str(unreal.GameplayTagLibrary.get_tag_name(tag))
            if name in ("InputTag.ShowEquipmentWidget", "InputTag.ShowSkillWindow"):
                key = controller.get_current_key_for_tag(tag)
                result["toggle_keys"][name] = str(key.get_editor_property("KeyName"))
    except Exception as error:
        result["key_inspection_error"] = str(error)
    if emit:
        unreal.log("[RPG_WINDOW_COEXIST] " + json.dumps(result, sort_keys=True))
    return result


_baseline = capture_window_coexist("baseline")
_observed_fields = ("skill_open", "cursor", "move_locked", "look_locked",
                    "skill_windows", "inventory_windows")
_previous_state = json.dumps({key: _baseline[key] for key in _observed_fields}, sort_keys=True)
_next_sample = 0.0
_sample_index = 0
_tick_handle = None


def cleanup_window_coexist():
    global _tick_handle
    if _tick_handle is not None:
        unreal.unregister_slate_post_tick_callback(_tick_handle)
        _tick_handle = None
    if getattr(__main__, _export_name, None) is capture_window_coexist:
        delattr(__main__, _export_name)
    if getattr(__main__, _cleanup_name, None) is cleanup_window_coexist:
        delattr(__main__, _cleanup_name)


def sample_window_coexist(_delta):
    global _next_sample, _previous_state, _sample_index
    if time.monotonic() < _next_sample:
        return
    _next_sample = time.monotonic() + 0.15
    try:
        result = capture_window_coexist("observed", emit=False)
    except RuntimeError as error:
        # Never follow a replacement PIE session, or leave this task's callback behind.
        unreal.log("[RPG_WINDOW_COEXIST_END] " + str(error))
        cleanup_window_coexist()
        return
    signature = json.dumps({key: result[key] for key in _observed_fields}, sort_keys=True)
    if signature != _previous_state:
        _previous_state = signature
        _sample_index += 1
        result["stage"] = "observed_%02d" % _sample_index
        unreal.log("[RPG_WINDOW_COEXIST] " + json.dumps(result, sort_keys=True))


setattr(__main__, _export_name, capture_window_coexist)
setattr(__main__, _cleanup_name, cleanup_window_coexist)
_tick_handle = unreal.register_slate_post_tick_callback(sample_window_coexist)
