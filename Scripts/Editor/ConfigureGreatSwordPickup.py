"""Read-only preflight; -RPGApplyGreatSwordPickup creates one pickup and links its definition.

No map placement, startup grants, drop-table edits, gameplay-stat edits, or DB writes.
Back up DA_Item_GreatSword before applying. Existing mismatched content is never overwritten.
"""

import unreal


PREFIX = "GREAT_SWORD_PICKUP"
DEFINITION = "/Game/Blueprints/GameData/Item/Weapons/DA_Item_GreatSword"
WEAPON = "/Game/Blueprints/Weapon/BP_PlayerWeapon"
PICKUP = "/Game/Blueprints/Item/Equipment/BP_Pickup_GreatSword"
ITEM_TAG = "GameItem.Equipment.Weapon.GreatSword"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def path_of(value):
    return value.get_path_name() if isinstance(value, unreal.Object) else str(value)


def is_empty_reference(value):
    return value is None or str(value) in ("None", "")


def check_pickup(blueprint, definition, mesh, scale):
    require(isinstance(blueprint, unreal.Blueprint), "Target must be a Blueprint")
    require(blueprint.get_blueprint_parent_class() == unreal.RPGPickUpBase.static_class(),
            "Unexpected pickup parent; refusing overwrite")
    default = unreal.get_default_object(blueprint.generated_class())
    manifest = default.get_editor_property("item_manifest")
    require(manifest.get_editor_property("item_tag").export_text() ==
            definition.get_editor_property("item_tag").export_text(), "Pickup identity differs")
    require(manifest.get_editor_property("item_category") == unreal.ItemCategory.EQUIP,
            "Pickup category differs")
    require(not manifest.get_editor_property("fragments"),
            "Pickup must not copy legacy stats or create a second item model")
    pickup_mesh = default.get_editor_property("pickup_mesh")
    require(pickup_mesh.get_editor_property("static_mesh") == mesh, "Pickup mesh differs")
    require(pickup_mesh.get_editor_property("relative_scale3d") == scale, "Pickup scale differs")
    require(pickup_mesh.get_collision_enabled() == unreal.CollisionEnabled.QUERY_ONLY,
            "Pickup must use query-only collision")
    require(pickup_mesh.get_collision_object_type() == unreal.CollisionChannel.ECC_WORLD_DYNAMIC,
            "Pickup must be visible to the existing WorldDynamic interaction query")
    require(not pickup_mesh.is_simulating_physics(), "Pickup must not simulate physics")
    require(default.get_component_by_class(unreal.RPGPickupGrantComponent) is not None,
            "Authoritative pickup-grant component is missing")
    require(default.get_editor_property("replicates"), "Pickup must replicate")
    interaction = default.get_editor_property("instance_interactable_data")
    require(str(interaction.get_editor_property("name")) == str(definition.get_editor_property("display_name"))
            and interaction.get_editor_property("quantity") == 1
            and interaction.get_editor_property("interaction_duration") == 0.0,
            "Pickup interaction presentation differs")


def validate_definition(definition, registry):
    settings = unreal.ValidateAssetsSettings()
    settings.set_editor_property("validation_usecase", unreal.DataValidationUsecase.SCRIPT)
    settings.set_editor_property("show_if_no_failures", False)
    info = registry.get_asset_by_object_path(definition.get_path_name())
    problems, results = unreal.get_editor_subsystem(unreal.EditorValidatorSubsystem).validate_assets_with_settings(
        [info], settings)
    require(problems == 0 and results.get_editor_property("num_valid") == 1,
            "GreatSword definition failed engine data validation")


def main():
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.wait_for_completion()
    definition = unreal.load_asset(DEFINITION)
    require(isinstance(definition, unreal.RPGItemDefinition), "Native GreatSword definition is required")
    require(definition.get_editor_property("item_tag").export_text() == f'(TagName="{ITEM_TAG}")'
            and definition.get_editor_property("item_category") == unreal.ItemCategory.EQUIP
            and definition.get_editor_property("max_stack_size") == 1,
            "Unexpected GreatSword definition identity or stack size")
    weapon = unreal.load_asset(WEAPON)
    require(isinstance(weapon, unreal.Blueprint), "Authored player weapon is required")
    weapon_default = unreal.get_default_object(weapon.generated_class())
    component = weapon_default.get_editor_property("weapon_mesh")
    mesh = component.get_editor_property("static_mesh")
    require(isinstance(mesh, unreal.StaticMesh), "Authored GreatSword static mesh is missing")
    scale = component.get_editor_property("relative_scale3d")
    # Commandlets do not initialize the StaticMeshEditor subsystem. Inspect the
    # authored body directly, including convex shapes omitted by that API's count.
    body = mesh.get_editor_property("body_setup")
    geometry = body.get_editor_property("agg_geom") if body else None
    collisions = sum(len(geometry.get_editor_property(name)) for name in
                     ("box_elems", "sphere_elems", "sphyl_elems", "convex_elems")) if geometry else 0
    unreal.log(f"{PREFIX} SOURCE Mesh={mesh.get_path_name()} Scale={scale} SimpleCollision={collisions}")
    require(collisions > 0, "Mesh has no simple collision; interaction shape needs review before authoring")
    fragments = definition.get_editor_property("fragments")
    require(len(fragments) == 1 and isinstance(fragments[0], unreal.RPGItemEquipmentDefinitionFragment)
            and path_of(fragments[0].get_editor_property("equipped_actor_class")) == weapon.generated_class().get_path_name(),
            "Definition must retain the authored GreatSword equipment actor")
    existing = unreal.load_asset(PICKUP) if unreal.EditorAssetLibrary.does_asset_exist(PICKUP) else None
    if existing is not None:
        check_pickup(existing, definition, mesh, scale)
        require(unreal.BlueprintEditorLibrary.compile_blueprint(existing), "Existing pickup Blueprint failed compilation")
        check_pickup(existing, definition, mesh, scale)
        unreal.log(f"{PREFIX} BLUEPRINT_VALID Status={existing.get_editor_property('status')} NoAssetsSaved=1")
    for name, expected in (("pickup_mesh", mesh.get_path_name()),
                           ("pickup_actor_class", PICKUP + ".BP_Pickup_GreatSword_C")):
        reference = definition.get_editor_property(name)
        require(is_empty_reference(reference) or path_of(reference) == expected,
                "Definition already has another " + name + "; refusing overwrite")
    validate_definition(definition, registry)
    unreal.log(f"{PREFIX} PREFLIGHT_OK ExistingPickup={int(existing is not None)} NoAssetsSaved=1")
    if "-RPGApplyGreatSwordPickup" not in unreal.SystemLibrary.get_command_line():
        return

    blueprint = existing
    created = blueprint is None
    if created:
        factory = unreal.BlueprintFactory()
        factory.set_editor_property("parent_class", unreal.RPGPickUpBase.static_class())
        folder, name = PICKUP.rsplit("/", 1)
        blueprint = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.Blueprint, factory)
        require(isinstance(blueprint, unreal.Blueprint), "Could not create pickup Blueprint")
        default = unreal.get_default_object(blueprint.generated_class())
        manifest = default.get_editor_property("item_manifest")
        manifest.set_editor_property("item_tag", definition.get_editor_property("item_tag"))
        manifest.set_editor_property("item_category", unreal.ItemCategory.EQUIP)
        default.set_editor_property("item_manifest", manifest)
        pickup_mesh = default.get_editor_property("pickup_mesh")
        pickup_mesh.set_simulate_physics(False)
        pickup_mesh.set_static_mesh(mesh)
        pickup_mesh.set_editor_property("relative_scale3d", scale)
        pickup_mesh.set_collision_profile_name("OverlapAllDynamic")
        pickup_mesh.set_collision_enabled(unreal.CollisionEnabled.QUERY_ONLY)
        interaction = default.get_editor_property("instance_interactable_data")
        interaction.set_editor_property("name", definition.get_editor_property("display_name"))
        interaction.set_editor_property("action", unreal.Text("Pick up"))
        interaction.set_editor_property("quantity", 1)
        default.set_editor_property("instance_interactable_data", interaction)
    require(unreal.BlueprintEditorLibrary.compile_blueprint(blueprint), "Pickup Blueprint failed compilation")
    check_pickup(blueprint, definition, mesh, scale)
    changed = [blueprint] if created else []
    for name, value in (("pickup_mesh", mesh), ("pickup_actor_class", blueprint.generated_class())):
        if path_of(definition.get_editor_property(name)) != value.get_path_name():
            definition.set_editor_property(name, value)
            if definition not in changed:
                changed.append(definition)
    validate_definition(definition, registry)
    for asset in changed:
        require(unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False),
                "Save failed; inspect scoped backup before retry: " + asset.get_path_name())
        unreal.log(f"{PREFIX} SAVED {asset.get_path_name()}")
    unreal.log(f"{PREFIX} COMPLETE SavedAssets={len(changed)} MapPlacements=0 StartupGrants=0 DatabaseWrites=0")


main()
