"""Preflight by default; -RPGApplyGreatSwordMigration saves only 2 BPs and 1 definition.

No startup item grant, gameplay stat edits, database writes, or asset deletion.
Back up the two Blueprint packages before using the explicit apply switch.
"""

import unreal


ROOT = "/Game/Blueprints/Character/Player"
WEAPON = "/Game/Blueprints/Weapon/BP_PlayerWeapon"
DEFINITION = "/Game/Blueprints/GameData/Item/Weapons/DA_Item_GreatSword"
ITEM_TAG = "GameItem.Equipment.Weapon.GreatSword"
PARENT = "/Script/Project_RPG.RPGGameplayAbility_WeaponStance"
ABILITIES = (
    (ROOT + "/Abilities/GA_Player_EquipGreatSword", "WEAPON_SET_PRIMARY"),
    (ROOT + "/Abilities/GA_Player_UnequipGreatSword", "NONE"),
)
PREFIX = "GREAT_SWORD_LOADOUT"


def require(path, kind):
    asset = unreal.load_asset(path)
    if not isinstance(asset, kind):
        raise RuntimeError(f"Expected {kind.__name__}: {path}")
    return asset


def tag(name):
    value = unreal.GameplayTag()
    if not value.import_text(f'(TagName="{name}")'):
        raise RuntimeError("Missing gameplay tag: " + name)
    return value


def same_tag(left, right):
    # The Python wrappers do not implement GameplayTag's native value equality.
    return left.export_text() == right.export_text()


def check_definition(asset, actor_class):
    if not isinstance(asset, unreal.RPGItemDefinition):
        raise RuntimeError("Existing target is not an item definition")
    if (not same_tag(asset.get_editor_property("item_tag"), tag(ITEM_TAG))
            or asset.get_editor_property("definition_version") != 1
            or asset.get_editor_property("max_stack_size") != 1
            or asset.get_editor_property("item_category") != unreal.ItemCategory.EQUIP):
        raise RuntimeError("Existing definition identity differs; refusing overwrite")
    fragments = asset.get_editor_property("fragments")
    if len(fragments) != 1 or not isinstance(fragments[0], unreal.RPGItemEquipmentDefinitionFragment):
        raise RuntimeError("Existing definition fragments differ; refusing overwrite")
    fragment = fragments[0]
    actor_ref = fragment.get_editor_property("equipped_actor_class")
    actor_path = actor_ref.get_path_name() if isinstance(actor_ref, unreal.Object) else str(actor_ref)
    expected_slots = {unreal.EquipmentSlotType.WEAPON_PRIMARY_R, unreal.EquipmentSlotType.WEAPON_SECONDARY_R}
    if (set(fragment.get_editor_property("compatible_slots")) != expected_slots
            or str(fragment.get_editor_property("attach_socket")) != "WeaponHandSocket"
            or actor_path != actor_class.get_path_name()
            or fragment.get_editor_property("weapon_type") != unreal.RPGGladiatorWeaponType.GREAT_SWORD
            or len(fragment.get_editor_property("granted_ability_sets")) != 0):
        raise RuntimeError("Existing equipment definition differs; refusing overwrite")
    # Count is hidden from Python. Preserve the actor's unauthored hand policy.
    try:
        fragment.get_editor_property("weapon_hand_type")
    except TypeError as error:
        if "Cannot pythonize '3'" not in str(error):
            raise
    else:
        raise RuntimeError("Existing hand policy differs; refusing overwrite")


def preflight():
    parent = unreal.load_class(None, PARENT)
    if parent is None:
        raise RuntimeError("Build and load the native weapon-stance class first")
    player = require(ROOT + "/BP_Player", unreal.Blueprint)
    default = unreal.get_default_object(player.generated_class())
    if default.get_component_by_class(unreal.RPGPlayerWeaponLoadoutComponent) is None:
        raise RuntimeError("Player Blueprint is missing its native weapon-loadout component")
    startup_ref = default.get_editor_property("character_start_up_data")
    startup_path = startup_ref.get_path_name() if isinstance(startup_ref, unreal.Object) else str(startup_ref)
    startup = require(startup_path, unreal.DataAsset)
    entries = startup.get_editor_property("player_start_up_ability_sets")
    for entry in entries:
        unreal.log(f"{PREFIX} STARTUP {entry.export_text()}")
    equip = [entry for entry in entries if same_tag(entry.get_editor_property("input_tag"), tag("InputTag.EquipWeapon"))]
    if len(equip) != 1 or equip[0].get_editor_property("ability_to_grant").get_path_name() != ABILITIES[0][0] + ".GA_Player_EquipGreatSword_C":
        raise RuntimeError("Unique authored startup equip ability is required")
    unreal.log(f"{PREFIX} LEVEL_SOURCE {equip[0].export_text()}")

    weapon = require(WEAPON, unreal.Blueprint)
    actor_class = weapon.generated_class()
    weapon_default = unreal.get_default_object(actor_class)
    data = weapon_default.get_editor_property("player_weapon_data")
    seen = set()
    for name in ("default_weapon_abilities", "special_weapon_abilities"):
        for entry in data.get_editor_property(name):
            ability = entry.get_editor_property("ability_to_grant")
            input_tag = entry.get_editor_property("input_tag")
            if ability is None:
                continue  # Preserve the unauthored heavy-attack entry.
            input_text = input_tag.export_text()
            if same_tag(input_tag, tag("InputTag.EquipWeapon")) or input_text in seen:
                raise RuntimeError("Duplicate/conflicting authored weapon input: " + input_text)
            seen.add(input_text)
    if len(seen) != 9:
        raise RuntimeError("Authored GreatSword ability roster changed; re-audit first")

    mesh = default.get_editor_property("mesh")
    socket_bone = mesh.get_socket_bone_name("WeaponHandSocket")
    if str(socket_bone).lower() != "hand_r":
        raise RuntimeError("Right-hand WeaponHandSocket must be verified before choosing right slots")
    unreal.log(f"{PREFIX} SOCKET bone={socket_bone} Slots=Primary_R,Secondary_R HandPolicy=Unassigned")

    blueprints = []
    for path, state in ABILITIES:
        blueprint = require(path, unreal.Blueprint)
        if blueprint.get_blueprint_parent_class().get_path_name() not in (
                "/Script/Project_RPG.RPGPlayerGameplayAbility", PARENT):
            raise RuntimeError("Unexpected ability parent: " + path)
        for graph in unreal.BlueprintEditorLibrary.list_graphs(blueprint):
            editor = unreal.BlueprintGraphEditor.get_graph_editor(graph)
            for node in editor.list_all_nodes():
                if "OnEndAbility" not in str(node.get_node_title()):
                    continue
                for pin in node.list_all_pins():
                    if str(pin.get_pin_name()) == "then" and pin.list_connected_pins():
                        raise RuntimeError("Authored end-ability effects need review: " + path)
        desired = getattr(unreal.EquipState, state)
        if blueprint.get_blueprint_parent_class().get_path_name() == PARENT:
            stance = unreal.get_default_object(blueprint.generated_class())
            if (stance.get_editor_property("desired_weapon_state") != desired
                    or stance.get_editor_property("net_security_policy") != unreal.GameplayAbilityNetSecurityPolicy.SERVER_ONLY_TERMINATION
                    or stance.get_editor_property("bServerRespectsRemoteAbilityCancellation")):
                raise RuntimeError("Unexpected native stance defaults: " + path)
            unreal.log(f"{PREFIX} NATIVE_STANCE {path} State={desired} ExecutionPolicy={stance.get_editor_property('net_execution_policy')}")
        blueprints.append((blueprint, desired))

    existing = unreal.load_asset(DEFINITION) if unreal.EditorAssetLibrary.does_asset_exist(DEFINITION) else None
    if existing:
        check_definition(existing, actor_class)
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.wait_for_completion()
    for info in registry.get_assets_by_class(unreal.TopLevelAssetPath("/Script/Project_RPG", "RPGItemDefinition"), search_sub_classes=True):
        asset = info.get_asset()
        if same_tag(asset.get_editor_property("item_tag"), tag(ITEM_TAG)) and asset != existing:
            raise RuntimeError("Another definition already owns the GreatSword tag: " + asset.get_path_name())
    if existing:
        settings = unreal.ValidateAssetsSettings()
        settings.set_editor_property("validation_usecase", unreal.DataValidationUsecase.SCRIPT)
        settings.set_editor_property("show_if_no_failures", False)
        asset_data = registry.get_asset_by_object_path(existing.get_path_name())
        problems, results = unreal.get_editor_subsystem(unreal.EditorValidatorSubsystem).validate_assets_with_settings([asset_data], settings)
        if problems or results.get_editor_property("num_valid") != 1:
            raise RuntimeError("GreatSword definition did not pass engine data validation")
        unreal.log(f"{PREFIX} DEFINITION_VALID Valid=1 Invalid=0")
    unreal.log(f"{PREFIX} PREFLIGHT_OK ValidAbilities={len(seen)} NoAssetsSaved=1")
    return parent, actor_class, blueprints, existing


def main():
    parent, actor_class, blueprints, existing = preflight()
    if "-RPGApplyGreatSwordMigration" not in unreal.SystemLibrary.get_command_line():
        return
    # Prepare and compile both compatibility entrypoints before saving any package.
    changed = []
    for blueprint, state in blueprints:
        execution_policy = unreal.get_default_object(blueprint.generated_class()).get_editor_property("net_execution_policy")
        if blueprint.get_blueprint_parent_class() != parent:
            unreal.BlueprintEditorLibrary.reparent_blueprint(blueprint, parent)
            changed.append(blueprint)
        default = unreal.get_default_object(blueprint.generated_class())
        if default.get_editor_property("desired_weapon_state") != state:
            default.set_editor_property("desired_weapon_state", state)
            if blueprint not in changed:
                changed.append(blueprint)
        # Native stance termination remains server-authoritative; retain authored execution policy.
        for name, value in (("net_security_policy", unreal.GameplayAbilityNetSecurityPolicy.SERVER_ONLY_TERMINATION),
                            ("bServerRespectsRemoteAbilityCancellation", False)):
            if default.get_editor_property(name) != value:
                default.set_editor_property(name, value)
                if blueprint not in changed:
                    changed.append(blueprint)
        if not unreal.BlueprintEditorLibrary.compile_blueprint(blueprint):
            raise RuntimeError("Stance Blueprint failed compilation: " + blueprint.get_path_name())
        verified = unreal.get_default_object(blueprint.generated_class())
        if (verified.get_editor_property("desired_weapon_state") != state
                or verified.get_editor_property("net_execution_policy") != execution_policy):
            raise RuntimeError("Stance defaults changed during compilation: " + blueprint.get_path_name())

    definition = existing
    if definition is None:
        folder, name = DEFINITION.rsplit("/", 1)
        definition = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, folder, unreal.RPGItemDefinition, unreal.DataAssetFactory())
        if definition is None:
            raise RuntimeError("Could not create GreatSword definition")
        definition.set_editor_property("item_tag", tag(ITEM_TAG))
        definition.set_editor_property("item_category", unreal.ItemCategory.EQUIP)
        definition.set_editor_property("display_name", unreal.Text("Great Sword"))
        fragment = unreal.new_object(unreal.RPGItemEquipmentDefinitionFragment, outer=definition, name="Equipment")
        fragment.set_editor_property("compatible_slots", [unreal.EquipmentSlotType.WEAPON_PRIMARY_R, unreal.EquipmentSlotType.WEAPON_SECONDARY_R])
        fragment.set_editor_property("weapon_type", unreal.RPGGladiatorWeaponType.GREAT_SWORD)
        fragment.set_editor_property("equipped_actor_class", actor_class)
        fragment.set_editor_property("attach_socket", "WeaponHandSocket")
        definition.set_editor_property("fragments", [fragment])
        check_definition(definition, actor_class)
        changed.append(definition)
    for asset in changed:
        if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
            raise RuntimeError("Save failed; restore scoped backup before retry: " + asset.get_path_name())
        unreal.log(f"{PREFIX} SAVED {asset.get_path_name()}")
    unreal.log(f"{PREFIX} COMPLETE SavedAssets={len(changed)} StartupItemsGranted=0 DatabaseWrites=0")


main()
