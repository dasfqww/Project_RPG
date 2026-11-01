"""Read-only cold-load/compile check of the authored quick-slot Blueprint.

Run with UnrealEditor-Cmd -ExecutePythonScript=<this file>. No PIE, item
fixtures, backend commands, asset saves, or other Blueprint compilation.
"""

import unreal


ASSET = "/Game/Blueprints/UI/QuickSlot/WBP_QuickSlot"
REMOVED_VISUAL = "/Game/Blueprints/UI/Inventory/WBP_ItemDragVisual"


def verify_quick_slot_asset():
    assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    blueprint = assets.load_asset(ASSET)
    if not isinstance(blueprint, unreal.Blueprint):
        raise AssertionError("Quick-slot Blueprint is missing")
    # UE 5.8 compile_blueprint returns None; inspect the status, not truthiness.
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    if blueprint.get_editor_property("status") != unreal.BlueprintStatus.BS_UP_TO_DATE:
        raise AssertionError("Quick-slot Blueprint did not compile without warnings")
    defaults = unreal.get_default_object(blueprint.generated_class())
    if not isinstance(defaults, unreal.RPGQuickSlotWidget):
        raise AssertionError("Quick-slot Blueprint lost its native parent")
    if defaults.get_editor_property("drag_visual_class") is not None:
        raise AssertionError("Quick slots still depend on a legacy drag visual")
    if defaults.get_editor_property("can_drag"):
        raise AssertionError("A quick-slot cell must not drag the entire window")

    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.scan_paths_synchronous(["/Game/Blueprints/UI/QuickSlot"], True)
    dependencies = [str(path) for path in registry.get_dependencies(
        ASSET, unreal.AssetRegistryDependencyOptions())]
    if REMOVED_VISUAL in dependencies:
        raise AssertionError("Saved Blueprint still imports the missing drag visual")
    if "/Script/Project_RPG" not in dependencies:
        raise AssertionError("Quick-slot native dependency was lost")

    unreal.log("QUICKSLOT_ASSET PASS NoAssetsSaved=1 dependency_count=" + str(len(dependencies)))


if __name__ == "__main__":
    verify_quick_slot_asset()
