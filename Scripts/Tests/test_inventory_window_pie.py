"""Offline checks of the PIE verifier, not Unreal/mouse integration evidence.

Run: D:/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe
     Scripts/Tests/test_inventory_window_pie.py
"""

import copy
import importlib.util
from pathlib import Path
import sys
import types
import unittest
from unittest.mock import patch


script = Path(__file__).resolve().parents[1] / "Editor" / "VerifyInventoryWindowPIE.py"
spec = importlib.util.spec_from_file_location("inventory_pie_verifier_test", script)
verifier = importlib.util.module_from_spec(spec)
# Import must be read-only and must not call any Unreal API automatically.
with patch.dict(sys.modules, {"unreal": types.ModuleType("unreal")}):
    spec.loader.exec_module(verifier)


class InventoryWindowDragVerifierTests(unittest.TestCase):
    def setUp(self):
        self.before = {
            "active": "inventory", "world_path": "PIE_0.World",
            "controller_path": "PIE_0.Controller", "window_path": "PIE_0.Inventory",
            "load_state": "Ready", "projected_items": 1, "mail_items": 0,
            "item_entries": (("item-a", 2, 3, "Inventory", 0),), "mail_entries": (),
            "has_last_command_result": False, "command_result": None,
            "render_translation": (10.0, 20.0),
        }
        self.after = copy.deepcopy(self.before)

    def test_drag_displacement(self):
        for kind in ("inventory", "equipment"):
            with self.subTest(kind=kind):
                self.before["active"] = self.after["active"] = kind
                self.after["render_translation"] = (50.0, 45.0)
                result = verifier._compare_inventory_window_drag(self.before, self.after, True, (40.0, 25.0))
                self.assertEqual(result["layout_delta"], (40.0, 25.0))

    def test_release_or_reopen_stays_in_place(self):
        result = verifier._compare_inventory_window_drag(self.before, self.after, False, (0, 0))
        self.assertTrue(result["same_window"])

    def test_no_movement_cannot_pass_a_drag(self):
        with self.assertRaises(AssertionError):
            verifier._compare_inventory_window_drag(self.before, self.after, True)

    def test_movement_after_release_is_rejected(self):
        self.after["render_translation"] = (11.0, 20.0)
        with self.assertRaises(AssertionError):
            verifier._compare_inventory_window_drag(self.before, self.after, False)

    def test_changed_item_at_same_count_is_rejected(self):
        for entry in (("item-b", 2, 3, "Inventory", 0), ("item-a", 3, 3, "Inventory", 0),
                      ("item-a", 2, 2, "Inventory", 0), ("item-a", 2, 3, "Equipment", 0)):
            with self.subTest(entry=entry):
                self.after["item_entries"] = (entry,)
                with self.assertRaises(AssertionError):
                    verifier._compare_inventory_window_drag(self.before, self.after, False)

    def test_changed_mail_or_command_is_rejected(self):
        for field, value in (("mail_entries", (("mail-a", 1, 1, "Mail", 0),)),
                             ("has_last_command_result", True),
                             ("command_result", ("request-a", "Item.Equip", "Success"))):
            with self.subTest(field=field):
                self.after = copy.deepcopy(self.before)
                self.after[field] = value
                with self.assertRaises(AssertionError):
                    verifier._compare_inventory_window_drag(self.before, self.after, False)

    def test_changed_world_controller_or_window_is_rejected(self):
        for field in ("world_path", "controller_path", "window_path", "active", "load_state"):
            with self.subTest(field=field):
                self.after = copy.deepcopy(self.before)
                self.after[field] = "different"
                with self.assertRaises(AssertionError):
                    verifier._compare_inventory_window_drag(self.before, self.after, False)

    def test_wrong_expected_delta_is_rejected(self):
        self.after["render_translation"] = (50.0, 45.0)
        with self.assertRaises(AssertionError):
            verifier._compare_inventory_window_drag(self.before, self.after, True, (80, 50))

    def test_invalid_comparison_values_are_rejected(self):
        for bad in (float("nan"), float("inf")):
            with self.subTest(bad=bad):
                self.after["render_translation"] = (bad, 20.0)
                with self.assertRaises(AssertionError):
                    verifier._compare_inventory_window_drag(self.before, self.after, False)
        self.after = copy.deepcopy(self.before)
        with self.assertRaises(AssertionError):
            verifier._compare_inventory_window_drag(self.before, self.after, False, (float("nan"), 0))
        with self.assertRaises(AssertionError):
            verifier._compare_inventory_window_drag(self.before, self.after, "False")
        for tolerance in (-1, float("nan"), float("inf")):
            with self.subTest(tolerance=tolerance):
                with self.assertRaises(AssertionError):
                    verifier._compare_inventory_window_drag(self.before, self.after, False, tolerance=tolerance)

    def test_projection_entry_order_is_not_a_change(self):
        class Entry:
            def __init__(self, item_id):
                self.values = dict(item_id=item_id, revision=2, quantity=3,
                                   container_type="Inventory", slot_index=0)

            def get_editor_property(self, name):
                return self.values[name]

        a, b = Entry("a"), Entry("b")
        self.assertEqual(verifier._projection_drag_entries([a, b]),
                         verifier._projection_drag_entries([b, a]))


if __name__ == "__main__":
    unittest.main(verbosity=2)
