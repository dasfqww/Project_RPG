"""Check native window placement in a disposable, non-rendering editor only.

Requires -RPGSkillWindowPlacementProbe and testmap. Starts/stops only its own
PIE, never saves assets or issues item commands. Direct UI method calls and
translation assignment are NOT keyboard/mouse/drag or rendered-screen tests.
"""

import json
import time
import unreal

if "-RPGSkillWindowPlacementProbe" not in unreal.SystemLibrary.get_command_line():
    raise RuntimeError("This probe requires its own disposable editor process")

_state = {"phase": "startup", "deadline": time.monotonic() + 120.0,
          "handle": None, "owns_pie": False, "step": 0, "samples": []}


def _check(condition, message):
    if not condition:
        raise AssertionError(message)


def _pair(vector):
    return [float(vector.x), float(vector.y)]


def _finish():
    unreal.unregister_slate_post_tick_callback(_state["handle"])
    result = {"success": "failure" not in _state, "samples": _state["samples"],
              "message": _state.get("failure", "Native opening placement verified"),
              "rendered_screen_verified": False, "routed_input_verified": False}
    (unreal.log if result["success"] else unreal.log_error)(
        "[RPG_SKILL_WINDOW_PLACEMENT] " + json.dumps(result, sort_keys=True))
    unreal.SystemLibrary.quit_editor()


def _capture(world, controller, label, expected_skill=True, translation=None):
    viewport = unreal.get_engine_subsystem(unreal.GameViewportSubsystem)
    result = {"stage": label, "windows": []}
    viewport_size = unreal.WidgetLayoutLibrary.get_viewport_size(controller)
    scale = unreal.WidgetLayoutLibrary.get_viewport_scale(controller)
    result.update(viewport_pixels=_pair(viewport_size), dpi=float(scale))
    rectangles = []
    for class_name, side, order in (("RPGSkillWindow", 0.0, 40),
                                     ("RPGInventoryProjectionWidget", 1.0, 20)):
        widget_class = unreal.load_class(None, "/Script/Project_RPG." + class_name)
        windows = [w for w in unreal.WidgetLibrary.get_all_widgets_of_class(world, widget_class, False)
                   if w.get_owning_player() == controller and w.is_in_viewport() and w.is_activated()]
        _check(len(windows) == (1 if side or expected_skill else 0), class_name + " active count")
        for window in windows:
            window.force_layout_prepass()
            slot = viewport.get_widget_slot(window)
            size = window.get_desired_size()
            anchors = slot.get_editor_property("anchors")
            alignment = slot.get_editor_property("alignment")
            offset = slot.get_editor_property("offsets")
            actual_translation = window.get_editor_property("render_transform").translation
            _check(_pair(anchors.minimum) == [side, 0.5] and
                   _pair(anchors.maximum) == [side, 0.5], class_name + " anchor")
            _check(_pair(alignment) == [side, 0.5], class_name + " alignment")
            _check(offset.right == 0 and offset.bottom == 0, class_name + " natural autosizing")
            _check(slot.get_editor_property("z_order") == order, class_name + " Z-order")
            _check(size.x > 0 and size.y > 0, class_name + " missing Desired size")
            if side == 0:
                _check(abs(size.x - 960) < 0.1 and abs(size.y - 640) < 0.1, "Authored skill size changed")
            elif translation is not None:
                _check(_pair(actual_translation) == translation, "Inventory translation lost on reopening")
            result["windows"].append({"class": window.get_class().get_name(), "path": window.get_path_name(),
                                      "desired_size": _pair(size), "translation": _pair(actual_translation),
                                      "anchor": _pair(anchors.minimum), "alignment": _pair(alignment),
                                      "offset": [float(offset.left), float(offset.top)]})
            if viewport_size.x > 0 and scale > 0:
                left = side * (viewport_size.x / scale - size.x) + offset.left
                rectangles.append((left, left + size.x))
    if len(rectangles) == 2:
        result["computed_horizontal_gap"] = float(rectangles[1][0] - rectangles[0][1])
        # Narrow viewports may legitimately overlap; preserve authored sizes.
        result["computed_non_overlap"] = rectangles[0][1] <= rectangles[1][0]
    _state["samples"].append(result)


def _tick(_delta):
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    try:
        _check(time.monotonic() < _state["deadline"], "Probe timed out: " + _state["phase"])
        if _state["phase"] == "startup":
            if unreal.AssetRegistryHelpers.get_asset_registry().is_loading_assets():
                return
            _check(not level.is_in_play_in_editor(), "Refusing to use an existing PIE")
            _check(editor.get_editor_world().get_name() == "testmap", "Expected testmap")
            level.editor_request_begin_play()
            _state.update(phase="starting", owns_pie=True)
            return
        if _state["phase"] == "ending":
            if not level.is_in_play_in_editor() and editor.get_game_world() is None:
                _finish()
            return
        world = editor.get_game_world()
        if _state["phase"] == "starting":
            if world is None or unreal.GameplayStatics.get_player_pawn(world, 0) is None:
                return
            _state.update(phase="running", game_instance=unreal.GameplayStatics.get_game_instance(world).get_path_name())
        _check(world is not None and unreal.GameplayStatics.get_game_instance(world).get_path_name() == _state["game_instance"],
               "PIE session changed")
        if time.monotonic() < _state.get("settle_at", 0):
            return
        controller = unreal.GameplayStatics.get_player_controller(world, 0)
        component = controller.get_component_by_class(unreal.load_class(None, "/Script/Project_RPG.RPGSkillWindowComponent"))
        step = _state["step"]
        if step == 0:
            _check(component.show_window(), "Skill window could not open")
        elif step == 1:
            controller.show_inventory_projection()
        elif step == 2:
            _capture(world, controller, "skill_then_inventory")
            inventory = [w for w in unreal.WidgetLibrary.get_all_widgets_of_class(world, unreal.RPGInventoryProjectionWidget, False)
                         if w.get_owning_player() == controller and w.is_activated()][0]
            _state["inventory_path"] = inventory.get_path_name()
            inventory.set_render_translation(unreal.Vector2D(15, 25))
            controller.toggle_inventory()
        elif step == 3:
            controller.show_inventory_projection()
        elif step == 4:
            _capture(world, controller, "inventory_reopened", translation=[15.0, 25.0])
            controller.show_equipment_projection()
        elif step == 5:
            _capture(world, controller, "equipment_tab")
            controller.show_reward_mail()
        elif step == 6:
            _capture(world, controller, "mail_tab")
            controller.toggle_inventory()
            component.close_window()
        elif step == 7:
            controller.show_inventory_projection()
        elif step == 8:
            _check(component.show_window(), "Skill window could not reopen")
        elif step == 9:
            _capture(world, controller, "inventory_then_skill", translation=[15.0, 25.0])
            inventory = unreal.find_object(None, _state["inventory_path"])
            _check(inventory is not None and inventory.is_activated(), "Inventory instance not reused")
            controller.toggle_inventory()
            component.close_window()
            level.editor_request_end_play()
            _state["phase"] = "ending"
        _state.update(step=step + 1, settle_at=time.monotonic() + 0.2)
    except Exception as error:
        _state["failure"] = str(error)
        if _state["owns_pie"] and level.is_in_play_in_editor():
            level.editor_request_end_play()
            _state["phase"] = "ending"
        else:
            _finish()


_state["handle"] = unreal.register_slate_post_tick_callback(_tick)
