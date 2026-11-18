"""Build the Chapter One supply alcove and its one-row world-loot table."""
import hashlib
import json
import os
from pathlib import Path

import unreal


MAP = "/Game/Maps/Progression/L_ProgressionIntro1"
TABLE = "/Game/Maps/Progression/DT_ChapterOneSupplyChest"
TABLE_TEMPLATE = "/Game/Blueprints/GameData/Item/DT_ItemDrop"
ROW = "SupplyChest"
TAG = "ChapterOne.SupplyAlcove"
POTION_TAG = "GameItem.Consume.Potion.Red.Large"
PICKUPS = ["/Game/Blueprints/Item/Consume/BP_PickUp_Red", "/Game/Blueprints/Item/BP_PickUp_Red"]
CHEST_LOCATION = (1730, -1700, 35)
CHEST_SCALE = (0.90, 0.52, 0.70)
WALLS = {
    "SupplyAlcove_West": ((1390, -1700, 125), (0.42, 4.30, 2.50)),
    "SupplyAlcove_Back": ((1725, -1915, 125), (7.00, 0.42, 2.50)),
    "SupplyAlcove_East": ((2070, -1700, 125), (0.42, 4.30, 2.50)),
}
TRIM = {
    "SupplyChest_Lid": ((1730, -1700, 72), (0.98, 0.60, 0.10)),
    "SupplyChest_Latch": ((1730, -1670, 59), (0.13, 0.05, 0.19)),
}


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def digest(package, extension):
    path = Path(unreal.Paths.project_content_dir()) / (package[6:] + extension)
    return hashlib.sha256(path.read_bytes()).hexdigest()


def spawn(actors, actor_class, label, location):
    actor = actors.spawn_actor_from_class(actor_class, unreal.Vector(*location))
    require(actor is not None, "Could not place " + label)
    actor.set_actor_label(label)
    actor.tags = [TAG]
    return actor


def existing_potion():
    for path in PICKUPS:
        pickup = unreal.EditorAssetLibrary.load_blueprint_class(path)
        if pickup is None:
            continue
        defaults = unreal.get_default_object(pickup)
        manifest = defaults.get_editor_property("item_manifest")
        tag = manifest.get_editor_property("item_tag").get_editor_property("tag_name")
        category = manifest.get_editor_property("item_category")
        if str(tag) == POTION_TAG and category != unreal.ItemCategory.NONE:
            return pickup
    raise RuntimeError("No existing pickup registered as the large red potion.")


def ensure_table(pickup, apply):
    assets = unreal.EditorAssetLibrary
    data_tables = unreal.DataTableFunctionLibrary
    table = assets.load_asset(TABLE) if assets.does_asset_exist(TABLE) else None
    created = table is None
    if created:
        require(apply, "The supply chest table is missing.")
        table = assets.duplicate_asset(TABLE_TEMPLATE, TABLE)
        require(table is not None, "Could not create the supply chest table.")
        data = [{"Name": ROW, "DropItemList": [{
            "ItemClass": pickup.get_path_name(), "DropQuantity": 1, "DropChance": 1.0,
        }], "RewardItemList": []}]
        require(data_tables.fill_data_table_from_json_string(table, json.dumps(data)), "Loot row import failed.")
    rows = json.loads(data_tables.export_data_table_to_json_string(table))
    require(len(rows) == 1 and rows[0]["Name"] == ROW and not rows[0]["RewardItemList"],
            "Expected one world-drop row without a settlement reward.")
    drops = rows[0]["DropItemList"]
    require(len(drops) == 1 and pickup.get_path_name() in drops[0]["ItemClass"]
            and drops[0]["DropQuantity"] == 1 and drops[0]["DropChance"] == 1.0,
            "Expected one guaranteed large health potion.")
    if created:
        require(assets.save_loaded_asset(table, only_if_is_dirty=False), "Supply chest table save failed.")
    return table, created


def main():
    proof = Path(os.environ["RPG_CACHE_REPORT"])
    require(proof.is_absolute(), "An absolute report path is required.")
    result = {"passed": False, "saved": False, "table_saved": False}
    try:
        subsystem = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
        require(subsystem.get_game_world() is None, "Preserving active play session.")
        dirty = unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
        require(not any(package.get_name() == MAP for package in dirty), "Preserving unsaved Intro1 map.")
        apply = "-rpgcreatechapteronesupplychest" in unreal.SystemLibrary.get_command_line().lower().split()
        before = digest(MAP, ".umap")
        pickup = existing_potion()
        table, result["table_saved"] = ensure_table(pickup, apply)
        world = unreal.EditorLoadingAndSavingUtils.load_map(MAP)
        require(world is not None, "The existing Intro1 map is required.")
        mode = world.get_world_settings().get_editor_property("default_game_mode")
        require(mode is not None and mode.get_name() == "BP_Intro1GameMode_C", "Unexpected Intro1 mode.")
        actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        existing = list(actors.get_all_level_actors())
        require({"S01_Glove", "S01_Nameplate", "S01_Yuna"}.issubset(a.get_actor_label() for a in existing),
                "Expected the published S01 placements.")
        owned = [actor for actor in existing if TAG in [str(tag) for tag in actor.tags]]
        new = not owned and apply
        if new:
            cube = unreal.load_asset("/Engine/BasicShapes/Cube")
            wood = unreal.load_asset("/Game/Assets/Arena/Materials/MI_WoodAndMetal")
            metal = unreal.load_asset("/Game/Assets/Arena/Materials/MI_Treasures")
            require(cube and wood and metal, "Supply chest materials are missing.")
            for label, (location, scale) in (WALLS | TRIM).items():
                block = spawn(actors, unreal.StaticMeshActor, label, location)
                block.set_actor_scale3d(unreal.Vector(*scale))
                mesh = block.static_mesh_component
                mesh.set_static_mesh(cube)
                mesh.set_material(0, wood if label in WALLS else metal)
                mesh.set_collision_profile_name("BlockAll" if label in WALLS else "NoCollision")
            chest = spawn(actors, unreal.RPGTreasureChest, "SupplyChest", CHEST_LOCATION)
            chest.set_actor_scale3d(unreal.Vector(*CHEST_SCALE))
            mesh = chest.get_editor_property("chest_mesh")
            mesh.set_static_mesh(cube)
            mesh.set_material(0, wood)
            mesh.set_collision_profile_name("BlockAllDynamic")
            source = chest.get_editor_property("loot_source")
            source.set_editor_property("drop_table", table)
            source.set_editor_property("drop_row", ROW)
            light = spawn(actors, unreal.PointLight, "SupplyAlcove_Light", (1730, -1620, 170))
            component = light.get_editor_property("light_component")
            component.set_editor_property("intensity", 320.0)
            component.set_editor_property("attenuation_radius", 310.0)
            component.set_editor_property("light_color", unreal.Color(255, 186, 112, 255))
            all_actors = list(actors.get_all_level_actors())
            require(set(existing).issubset(all_actors), "An existing Intro1 actor was removed.")
            owned = [actor for actor in all_actors if TAG in [str(tag) for tag in actor.tags]]
        labels = {a.get_actor_label(): a for a in owned}
        require(len(owned) == len(labels) and labels.keys() == (set(WALLS) | set(TRIM) | {"SupplyChest", "SupplyAlcove_Light"}),
                "Expected one chest, one light and the alcove props.")
        for label, (location, _) in (WALLS | TRIM).items():
            require(labels[label].get_actor_location() == unreal.Vector(*location), "Alcove prop moved: " + label)
        chest = labels["SupplyChest"]
        require(chest.get_class() == unreal.RPGTreasureChest.static_class()
                and chest.get_actor_location() == unreal.Vector(*CHEST_LOCATION), "Wrong supply chest placement.")
        mesh = chest.get_editor_property("chest_mesh")
        source = chest.get_editor_property("loot_source")
        require(str(mesh.get_collision_profile_name()) == "BlockAllDynamic" and mesh.static_mesh is not None,
                "Chest must be discoverable by the normal interaction query.")
        require(source.get_editor_property("trigger") == unreal.RPGLootSourceTrigger.OPEN
                and source.get_editor_property("maximum_events") == 1
                and source.get_editor_property("drop_table") == table
                and str(source.get_editor_property("drop_row")) == ROW, "Wrong chest loot binding.")
        if new:
            require(digest(MAP, ".umap") == before, "The source map changed during authoring.")
            require(unreal.EditorLoadingAndSavingUtils.save_map(world, MAP), "Intro1 map save failed.")
            result["saved"] = True
        result.update(passed=True, potion=POTION_TAG, pickup=pickup.get_path_name(),
                      map_sha256=digest(MAP, ".umap"), table_sha256=digest(TABLE, ".uasset"))
    finally:
        proof.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")


main()
