"""Read-only checks for the existing inventory/equipment windows in PIE.

Run after opening the bag with I or opening other independent item windows:
    py "C:/UE5/Project_RPG/Scripts/Editor/VerifyInventoryWindowPIE.py"

For checks with an expected state, import the module into the console first:
    py import sys; sys.path.append("C:/UE5/Project_RPG/Scripts/Editor"); import VerifyInventoryWindowPIE as inv_ui
    py inv_ui.verify_inventory_window_pie("inventory")
The other expected states are "equipment", "mail", and "closed". Multiple
windows may coexist; an expected kind selects that window, not the only window.
Check rendered window
bounds with MCP screenshots; UE 5.8 Python geometry reads returned zero sizes
for both the visible window and viewport in the observed PIE session.

For a drag, capture a baseline before sending any mouse input:
    py before_drag = inv_ui.capture_inventory_window_drag_pie("inventory")
After actually dragging the title with the left mouse button:
    py inv_ui.verify_inventory_window_drag_pie(before_drag, True)
After release, capture another baseline, move the mouse without holding a
button, then verify with False. The same check can verify unchanged translation
and window identity after closing/reopening. An optional expected_delta=(x, y)
is in layout units, not screen pixels. These checks never simulate a drag and
do not establish title-bar screen bounds or mouse routing by themselves.
This script does not open windows, inject input, issue item commands, create
fixtures, compile Blueprints, or save assets. Uninitialized is NOT loaded/empty.
"""

import math

import unreal


def _require(condition, message):
    if not condition:
        raise AssertionError(message)


def _inventory_named_widget(owner, name):
    for tree in ("WidgetTree", "WidgetTree_0", "WidgetTree_1"):
        widget = unreal.find_object(None, owner.get_path_name() + "." + tree + "." + name)
        if widget:
            return widget
    return None


def verify_inventory_window_pie(expected_kind=None):
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    _require(world is not None, "Start PIE first")
    controller = unreal.GameplayStatics.get_player_controller(world, 0)
    _require(controller is not None, "PIE has no local player controller")
    projection = controller.get_component_by_class(unreal.RPGInventoryProjectionComponent)
    _require(projection is not None, "Controller has no owner inventory projection")
    windows = [window for window in unreal.WidgetLibrary.get_all_widgets_of_class(
        world, unreal.RPGInventoryProjectionWidget, False)
        if window.get_owning_player() == controller and window.is_in_viewport()]
    active = [window for window in windows if window.is_activated()]
    for window in windows:
        if window not in active:
            _require(window.get_visibility() == unreal.SlateVisibility.COLLAPSED,
                     "Inactive item window still occupies the screen")

    result = {"active": "closed", "instances": len(windows),
              "world_path": world.get_path_name(),
              "controller_path": controller.get_path_name(),
              "load_state": str(projection.get_load_state()),
              "projected_items": len(projection.get_projected_items()),
              "mail_items": len(projection.get_mail_items())}
    by_kind = {}
    shared_model = None
    for window in active:
        grid = _inventory_named_widget(window, "InventoryGrid")
        left = _inventory_named_widget(window, "EquipmentLeftSlots")
        right = _inventory_named_widget(window, "EquipmentRightSlots")
        kind = "inventory" if grid else "equipment" if left and right else "mail"
        _require(kind not in by_kind, "Duplicate active {} windows".format(kind))
        current = {"active": kind, "window_path": window.get_path_name()}
        by_kind[kind] = current
        translation = window.get_editor_property("render_transform").translation
        current["render_translation"] = (float(translation.x), float(translation.y))
        _require(window.get_visibility() == unreal.SlateVisibility.VISIBLE,
                 "Active window is hidden")
        model = window.get_inventory_view_model()
        _require(model is not None, "Window has no inventory ViewModel")
        _require(shared_model is None or model == shared_model,
                 "Independent item windows do not share the owner ViewModel")
        shared_model = model
        _require(model.get_editor_property("load_state") == projection.get_load_state(),
                 "ViewModel load state differs from the owner projection")
        inventory_count = len(model.get_editor_property("inventory_items"))
        equipment_count = len(model.get_editor_property("equipment_items"))
        _require(inventory_count + equipment_count == result["projected_items"],
                 "ViewModel inventory/equipment counts differ from the projection")
        _require(len(model.get_editor_property("mail_items")) == result["mail_items"],
                 "ViewModel mail count differs from the projection")
        current["has_last_command_result"] = model.get_editor_property("has_last_command_result")

        anchors = window.get_anchors_in_viewport()
        alignment = window.get_alignment_in_viewport()
        expected_x = {"inventory": 1.0, "equipment": 0.0, "mail": 0.5}[kind]
        _require(all(abs(value - expected_x) < 0.001 for value in (
            anchors.minimum.x, anchors.maximum.x, alignment.x)) and
            all(abs(value - 0.5) < 0.001 for value in (
                anchors.minimum.y, anchors.maximum.y, alignment.y)),
            "Window lost its responsive viewport anchors/alignment")
        current["horizontal_anchor"] = expected_x
        _require(_inventory_named_widget(window, "CloseWindowButton") is not None,
                 "Item window has no individual close button")

        if kind == "inventory":
            # Initial bag policy is 80 slots, arranged in 10 columns.
            _require(grid.get_children_count() == 80, "Inventory needs all 80 slots")
            for index, child in enumerate(grid.get_all_children()):
                slot = child.get_editor_property("slot")
                _require(slot.get_editor_property("row") == index // 10 and
                         slot.get_editor_property("column") == index % 10,
                         "Inventory grid slot is in the wrong row/column")
            equipment_column = _inventory_named_widget(window, "EquipmentColumn")
            _require(equipment_column is None or
                     equipment_column.get_visibility() == unreal.SlateVisibility.COLLAPSED,
                     "Bag window must not also display the equipment layout")
            current["bag_slots"] = grid.get_children_count()
        elif kind == "equipment":
            _require(left.get_children_count() == 7 and right.get_children_count() == 7,
                     "Equipment needs separate left/right groups of seven slots")
            current["equipment_slots"] = left.get_children_count() + right.get_children_count()

    result["active_kinds"] = sorted(by_kind)
    result["windows"] = by_kind
    if by_kind:
        selected = expected_kind if expected_kind in by_kind else sorted(by_kind)[0]
        result.update(by_kind[selected])

    _require(expected_kind is None or result["active"] == expected_kind,
             "Expected {} but found {}".format(expected_kind, result["active"]))
    unreal.log("[RPG_INVENTORY_UI] PASS " + str(result))
    return result


def _projection_drag_entries(entries):
    # Compare identity/revision/quantity/location, not just the entry count.
    # str(Guid) contains the Python wrapper address, not the GUID value in UE 5.8.
    return tuple(sorted((
        unreal.GuidLibrary.conv_guid_to_string(entry.get_editor_property("item_id")),
        int(entry.get_editor_property("revision")),
        int(entry.get_editor_property("quantity")),
        str(entry.get_editor_property("container_type")),
        int(entry.get_editor_property("slot_index"))) for entry in entries))


def _command_result_fingerprint(command):
    return (
        unreal.GuidLibrary.conv_guid_to_string(command.get_editor_property("request_id")),
        str(command.get_editor_property("operation")),
        str(command.get_editor_property("result")))


def capture_inventory_window_drag_pie(expected_kind):
    _require(expected_kind in ("inventory", "equipment", "mail"),
             "Drag checks require an inventory, equipment or mail window")
    state = verify_inventory_window_pie(expected_kind)
    controller = unreal.find_object(None, state["controller_path"])
    window = unreal.find_object(None, state["window_path"])
    _require(controller is not None and window is not None, "PIE objects changed during capture")
    projection = controller.get_component_by_class(unreal.RPGInventoryProjectionComponent)
    state["item_entries"] = _projection_drag_entries(projection.get_projected_items())
    state["mail_entries"] = _projection_drag_entries(projection.get_mail_items())
    model = window.get_inventory_view_model()
    state["command_result"] = None
    if state["has_last_command_result"]:
        command = model.get_editor_property("last_command_result")
        state["command_result"] = _command_result_fingerprint(command)
    return state


def _compare_inventory_window_drag(before, after, expect_moved, expected_delta=None, tolerance=0.01):
    _require(isinstance(expect_moved, bool), "Specify whether the window should have moved")
    _require(math.isfinite(tolerance) and tolerance >= 0, "Invalid comparison tolerance")
    _require(before["active"] in ("inventory", "equipment", "mail"), "Invalid drag baseline")
    for name in ("active", "world_path", "controller_path", "window_path", "load_state",
                 "projected_items", "mail_items", "item_entries", "mail_entries",
                 "has_last_command_result", "command_result"):
        _require(before[name] == after[name], "Drag changed {}".format(name))
    start, end = before["render_translation"], after["render_translation"]
    _require(len(start) == 2 and len(end) == 2 and
             all(math.isfinite(value) for value in (*start, *end)), "Invalid window translation")
    delta = tuple(end[index] - start[index] for index in (0, 1))
    moved = any(abs(value) > tolerance for value in delta)
    _require(moved == expect_moved,
             "Expected moved={} but displacement was {}".format(expect_moved, delta))
    if expected_delta is not None:
        _require(len(expected_delta) == 2 and all(math.isfinite(value) for value in expected_delta),
                 "Invalid expected displacement")
        _require(all(abs(actual - expected) <= tolerance
                     for actual, expected in zip(delta, expected_delta)),
                 "Expected displacement {} but found {}".format(expected_delta, delta))
    return {"active": after["active"], "moved": moved, "layout_delta": delta,
            "same_window": True, "projection_entries_unchanged": True,
            "command_result_unchanged": True}


def verify_inventory_window_drag_pie(before, expect_moved, expected_delta=None):
    after = capture_inventory_window_drag_pie(before["active"])
    result = _compare_inventory_window_drag(before, after, expect_moved, expected_delta)
    unreal.log("[RPG_INVENTORY_UI] DRAG PASS " + str(result))
    return result


if __name__ == "__main__":
    verify_inventory_window_pie()
