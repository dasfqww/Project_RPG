"""Read-only audit of player weapon defaults and authored equip graphs; no saves."""

import unreal


PREFIX = "PLAYER_WEAPON_AUDIT"
GRAPH_PATHS = (
    "/Game/Blueprints/Character/Player/Abilities/GA_Spawn_GreatSword",
    "/Game/Blueprints/Character/Player/Abilities/GA_Player_EquipGreatSword",
    "/Game/Blueprints/Character/Player/Abilities/GA_Player_UnequipGreatSword",
)


def log_property(default, name):
    try:
        value = default.get_editor_property(name)
    except TypeError as error:
        # Unreal's Python bridge does not expose UMETA(Hidden) sentinel entries.
        sentinel = {"weapon_type": 7, "weapon_hand_type": 3}.get(name)
        if sentinel is None or f"Cannot pythonize '{sentinel}'" not in str(error):
            raise
        unreal.log(f"{PREFIX} DEFAULT {default.get_path_name()} {name}=Count (unassigned)")
        return None
    text = value.export_text() if hasattr(value, "export_text") else str(value)
    unreal.log(f"{PREFIX} DEFAULT {default.get_path_name()} {name}={text}")
    return value


def inspect_graphs(blueprint):
    unreal.log(f"{PREFIX} GRAPH_ASSET {blueprint.get_path_name()} "
               f"PARENT={blueprint.get_blueprint_parent_class().get_path_name()}")
    if blueprint.get_name() in ("GA_Spawn_GreatSword", "GA_SpawnWeapon"):
        default = unreal.get_default_object(blueprint.generated_class())
        for name in ("WeaponClassToSpawn", "WeaponTagToRegister", "Socket Name",
                     "Register as Equipped Weapon"):
            log_property(default, name)
    for graph in unreal.BlueprintEditorLibrary.list_graphs(blueprint):
        editor = unreal.BlueprintGraphEditor.get_graph_editor(graph)
        for node in editor.list_all_nodes():
            unreal.log(f"{PREFIX} NODE {graph.get_name()} {node.get_name()} {node.get_node_title()}")
            for pin in node.list_all_pins():
                links = [f"{other.get_owning_node().get_name()}:{other.get_pin_name()}"
                         for other in pin.list_connected_pins()]
                unreal.log(f"{PREFIX} PIN {node.get_name()} {pin.get_pin_name()} "
                           f"VALUE={pin.get_pin_value()} LINKS={','.join(links)}")


def main():
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.wait_for_completion()
    for root in ("/Game/Blueprints/Character/Player", "/Game/Blueprints/Weapon"):
        for data in sorted(registry.get_assets_by_path(root, recursive=True),
                           key=lambda entry: str(entry.package_name)):
            # Only inspect the player character and weapon Blueprint defaults.
            if str(data.asset_name) not in ("BP_Player", "BP_PlayerWeapon"):
                continue
            blueprint = data.get_asset()
            if not isinstance(blueprint, unreal.Blueprint):
                raise RuntimeError(f"Expected Blueprint: {data.package_name}")
            default = unreal.get_default_object(blueprint.generated_class())
            unreal.log(f"{PREFIX} BLUEPRINT {data.package_name} "
                       f"PARENT={blueprint.get_blueprint_parent_class().get_path_name()}")
            if isinstance(default, unreal.RPGPlayerWeapon):
                for name in ("player_weapon_data", "weapon_type", "weapon_hand_type"):
                    log_property(default, name)
            elif isinstance(default, unreal.RPGPlayer):
                startup = log_property(default, "character_start_up_data")
                if startup:
                    if not isinstance(startup, unreal.Object):
                        startup = unreal.load_asset(str(startup))
                    if startup is None:
                        raise RuntimeError("Cannot load authored player startup data")
                    for name in ("activate_on_given_abilities", "reactive_abilities",
                                 "player_start_up_ability_sets", "start_up_gameplay_effects"):
                        log_property(startup, name)

    inspected = set()
    for path in GRAPH_PATHS:
        blueprint = unreal.EditorAssetLibrary.load_asset(path)
        if not isinstance(blueprint, unreal.Blueprint):
            raise RuntimeError(f"Expected ability Blueprint: {path}")
        while blueprint is not None and blueprint.get_path_name() not in inspected:
            inspected.add(blueprint.get_path_name())
            inspect_graphs(blueprint)
            parent_path = blueprint.get_blueprint_parent_class().get_path_name()
            blueprint = (unreal.load_asset(parent_path.rsplit(".", 1)[0])
                         if parent_path.startswith("/Game/") else None)
    unreal.log(f"{PREFIX} COMPLETE NoAssetsSaved=1")


main()
