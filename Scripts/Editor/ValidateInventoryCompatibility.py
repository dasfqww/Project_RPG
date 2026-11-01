"""Compile affected Blueprint references in memory; never save or migrate assets."""

import unreal


ASSETS = (
    "/Game/Blueprints/Component/BP_RPGInventoryComponent",
    "/Game/Blueprints/Controller/BP_PlayerController",
    "/Game/Blueprints/UI/Inventory/WBP_Inven_Spatial",
    "/Game/Blueprints/UI/Inventory/WBP_InventoryGrid",
    "/Game/Blueprints/UI/Inventory/Projection/WBP_InventoryProjection",
    "/Game/Blueprints/UI/Equipment/Projection/WBP_EquipmentProjection",
)


def main():
    editor_assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    for path in ASSETS:
        blueprint = editor_assets.load_asset(path)
        if not isinstance(blueprint, unreal.Blueprint):
            raise RuntimeError("Missing inventory Blueprint: " + path)
        succeeded = unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
        status = blueprint.get_editor_property("status")
        unreal.log(f"INVENTORY_COMPAT {path} status={status}")
        if not succeeded:
            raise RuntimeError(f"Inventory Blueprint did not compile: {path} ({status})")
    unreal.log("INVENTORY_COMPAT COMPLETE NoAssetsSaved=1")


main()
