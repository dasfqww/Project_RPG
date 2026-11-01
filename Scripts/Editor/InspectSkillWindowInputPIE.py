"""Read-only snapshots for real keyboard/mouse skill-window PIE verification.

Run after starting the dedicated skill UI PIE session:
    py "C:/UE5/Project_RPG/Scripts/Editor/InspectSkillWindowInputPIE.py"
Then capture after real input events through the exported console callback:
    py import __main__; __main__._rpg_skill_window_input_capture("stage")
Does not open windows, invoke click handlers, save assets, or change skill state.
"""
import json
import __main__
import unreal


_rpg_skill_input_game_instance = None


def capture_skill_window_input(stage):
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    if world is None:
        raise RuntimeError("Skill input inspection requires PIE")
    game_instance = unreal.GameplayStatics.get_game_instance(world).get_path_name()
    if game_instance != _rpg_skill_input_game_instance:
        raise RuntimeError("PIE session changed; refusing to inspect another session")
    controller = unreal.GameplayStatics.get_player_controller(world, 0)
    component_class = unreal.load_class(None, "/Script/Project_RPG.RPGSkillWindowComponent")
    component = controller.get_component_by_class(component_class)
    if component is None:
        raise RuntimeError("Player controller has no skill-window component")
    window_class = unreal.load_class(None, "/Game/Blueprints/UI/Skill/WBP_SkillWindow.WBP_SkillWindow_C")
    windows = [window for window in unreal.WidgetLibrary.get_all_widgets_of_class(world, window_class, False)
               if window.is_in_viewport()]
    result = {
        "stage": stage,
        "game_instance": game_instance,
        "open": component.is_window_open(),
        "windows": len(windows),
        "cursor": controller.get_editor_property("show_mouse_cursor"),
        "move_locked": controller.is_move_input_ignored(),
        "look_locked": controller.is_look_input_ignored(),
    }
    if windows:
        if len(windows) != 1:
            raise RuntimeError("Multiple skill windows in one viewport")
        window = windows[0]
        geometry = window.get_cached_geometry()
        size = unreal.SlateLibrary.get_local_size(geometry)
        desired = window.get_desired_size()
        slot = unreal.get_engine_subsystem(unreal.GameViewportSubsystem).get_widget_slot(window)
        result.update(
            window=window.get_path_name(),
            size=[size.x, size.y], desired=[desired.x, desired.y],
            anchor_min=[slot.anchors.minimum.x, slot.anchors.minimum.y],
            anchor_max=[slot.anchors.maximum.x, slot.anchors.maximum.y],
            alignment=[slot.alignment.x, slot.alignment.y],
            offsets=[slot.offsets.left, slot.offsets.top, slot.offsets.right, slot.offsets.bottom],
        )
    unreal.log("[RPG_SKILL_INPUT] " + json.dumps(result, sort_keys=True))
    return result


_rpg_skill_input_world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
if _rpg_skill_input_world is None:
    raise RuntimeError("Start the skill UI PIE session before running this script")
_rpg_skill_input_game_instance = unreal.GameplayStatics.get_game_instance(_rpg_skill_input_world).get_path_name()
capture_skill_window_input("baseline")
# Console-executed files use a private scope. Export only this task's callback.
__main__._rpg_skill_window_input_capture = capture_skill_window_input
