"""Read-only audit of authored pickup manifests and native item definitions."""

import unreal


def main():
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.wait_for_completion()
    editor_assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    for data in sorted(
        registry.get_assets_by_path("/Game/Blueprints/Item", recursive=True),
        key=lambda entry: str(entry.package_name),
    ):
        asset = data.get_asset()
        if not isinstance(asset, unreal.Blueprint):
            continue
        generated = asset.generated_class()
        if generated is None:
            continue
        default = unreal.get_default_object(generated)
        if not isinstance(default, unreal.RPGPickUpBase):
            continue
        manifest = default.get_editor_property("item_manifest")
        unreal.log(f"ITEM_AUDIT PICKUP {data.package_name} {manifest.export_text()}")

    for path in sorted(editor_assets.list_assets(
        "/Game/Blueprints/GameData/Item", recursive=True, include_folder=False
    )):
        asset = editor_assets.load_asset(path)
        if isinstance(asset, unreal.RPGItemDefinition):
            unreal.log(
                f"ITEM_AUDIT NATIVE {path} "
                f"tag={asset.get_editor_property('item_tag')} "
                f"category={asset.get_editor_property('item_category')} "
                f"fragments={asset.get_editor_property('fragments')}"
            )
        elif isinstance(asset, unreal.DataTable):
            unreal.log(
                f"ITEM_AUDIT TABLE {path} "
                f"{unreal.DataTableFunctionLibrary.export_data_table_to_json_string(asset)}"
            )


main()
