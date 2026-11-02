"""Author missing first-slice equipment in a private Content branch.

Default is read-only. -RPGCreateProgressionEquipmentAssets creates missing assets;
-RPGProgressionEquipmentAssetsReload strictly checks registered IDs without saves.
Requires Config/Tags/ProgressionEquipment.ini and the existing GreatSword template.
Does not install the package, grant items, initialize growth, or implement effects.
"""
import ctypes
import hashlib
import json
import os
import re
from pathlib import Path

ROOT = "/Game/Blueprints/GameData/Item/Progression"
TEMPLATE = "/Game/Blueprints/GameData/Item/Weapons/DA_Item_GreatSword"
MARKER = "FirstSlice.Equipment.v1"
SPECS = {
    "StarterGreatSword": ("Starter.GreatSword", "시작 대검", "weapon", 9),
    "StarterChest": ("Starter.Chest", "시작 상의", "chest", 1),
    "StarterGloves": ("Starter.Gloves", "시작 장갑", "gloves", 4),
    "EnduranceGreatSword": ("Target.EnduranceGreatSword", "인내 대검", "weapon", 9),
    "CirculationGreatSword": ("Target.CirculationGreatSword", "순환 대검", "weapon", 9),
    "TargetChest": ("Target.Chest", "목표 상의", "chest", 1),
    "TargetGloves": ("Target.Gloves", "목표 장갑", "gloves", 4),
}
EQUIPMENT_FIELDS = ("weapon_type", "weapon_hand_type", "utility_type", "equipped_actor_class",
                    "attach_socket", "equipped_actor_relative_transform", "granted_ability_sets")
HIDDEN_COUNT_VALUES = {"weapon_type": ("RPGGladiatorWeaponType", 7),
                       "weapon_hand_type": ("WeaponHandType", 3),
                       "utility_type": ("RPGGladiatorUtilityType", 2)}


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def asset_path(model):
    return ROOT + "/DA_Item_Progression_" + model


def item_tag(model):
    return "GameItem.Equipment.Progression." + SPECS[model][0]


def path_of(value):
    return value.get_path_name() if hasattr(value, "get_path_name") else str(value)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else None


def dirty(unreal):
    return {p.get_name() for p in list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
            + list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())}


def disk(unreal, package):
    return Path(unreal.Paths.project_content_dir()) / (package[len("/Game/"):] + ".uasset")


def tag(unreal, name):
    result = unreal.GameplayTag()
    require(result.import_text('(TagName="' + name + '")') and name in result.export_text(),
            "Load ProgressionEquipment.ini before authoring: " + name)
    return result


def equipment_value(fragment, key):
    try:
        return fragment.get_editor_property(key)
    except TypeError as error:
        # UE omits UMETA(Hidden) enum entries from its Python enum wrappers.
        # Preserve only the known native Count defaults; other conversion errors fail.
        match = re.search(r"Cannot pythonize '(\d+)' \(int64\) as '([^']+)'", str(error))
        expected = HIDDEN_COUNT_VALUES.get(key)
        require(match and expected == (match.group(2), int(match.group(1))),
                "Unexpected property conversion: " + str(error))
        return {"native_enum": expected[0], "entry": "Count", "value": expected[1]}


def equipment_shape(fragment):
    result = {}
    for key in EQUIPMENT_FIELDS:
        value = equipment_value(fragment, key)
        if key == "granted_ability_sets":
            result[key] = [path_of(v) for v in value]
        elif isinstance(value, dict):
            result[key] = value
        elif hasattr(value, "export_text"):
            result[key] = value.export_text()
        else:
            result[key] = path_of(value)
    return result


def validate(unreal, asset):
    state, errors, warnings = unreal.get_editor_subsystem(unreal.EditorValidatorSubsystem).is_object_valid(
        asset, unreal.DataValidationUsecase.SCRIPT)
    require(state != unreal.DataValidationResult.INVALID and not errors and not warnings,
            "Native validation rejected " + asset.get_path_name() + ": " + str((state, errors, warnings)))
    return {"result": str(state), "errors": [str(v) for v in errors], "warnings": [str(v) for v in warnings]}


def inspect(unreal, asset, model, expected_shape, expected_slot):
    require(isinstance(asset, unreal.RPGItemDefinition), "Preserving another asset class: " + model)
    require(unreal.EditorAssetLibrary.get_metadata_tag(asset, "RPGProgressionEquipmentSpec") == MARKER,
            "Preserving an asset authored elsewhere: " + model)
    require(unreal.EditorAssetLibrary.get_metadata_tag(asset, "RPGProgressionEquipmentModel") == model,
            "Equipment recipe marker differs: " + model)
    require(asset.get_editor_property("item_tag").export_text() == tag(unreal, item_tag(model)).export_text(), "Item tag differs")
    require(asset.get_editor_property("definition_version") == 1
            and asset.get_editor_property("max_stack_size") == 1
            and asset.get_editor_property("item_category") == unreal.ItemCategory.EQUIP, "Equipment version/category/stack differs")
    require(str(asset.get_editor_property("display_name")) == SPECS[model][1], "Equipment display name differs")
    size = asset.get_editor_property("grid_size")
    require(size.x == size.y == 1, "Prototype grid differs")
    require(not asset.get_editor_property("item_traits").gameplay_tags, "Preserving customized traits")
    for key in ("icon", "pickup_mesh", "pickup_actor_class"):
        require(path_of(asset.get_editor_property(key)) in ("None", ""), "Preserving customized presentation/world data: " + key)
    fragments = list(asset.get_editor_property("fragments"))
    equipment = [f for f in fragments if isinstance(f, unreal.RPGItemEquipmentDefinitionFragment)]
    effect_type = {"EnduranceGreatSword": getattr(unreal, "RPGItemShieldOnHitDefinitionFragment", None),
                   "CirculationGreatSword": getattr(unreal, "RPGItemResolveOnHitDefinitionFragment", None)}.get(model)
    extra = [f for f in fragments if f not in equipment]
    require(len(equipment) == 1 and len(extra) <= 1 and
            (not extra or (effect_type is not None and isinstance(extra[0], effect_type))),
            "Equipment requires one equipment fragment and only its supported optional hit effect")
    fragment = equipment[0]
    require(fragment.get_outer() == asset, "Equipment fragment is not owned by its definition")
    require(set(fragment.get_editor_property("compatible_slots")) == {expected_slot}, "Compatible slot differs")
    shape = equipment_shape(fragment)
    expected = dict(expected_shape)
    if extra:
        effect = "Shield" if model == "EnduranceGreatSword" else "Resolve"
        package = ROOT + "/Effects/DA_AbilitySet_" + effect + "OnHit"
        expected["granted_ability_sets"] = list(expected["granted_ability_sets"]) + [package + "." + package.rsplit("/", 1)[1]]
    require(shape == expected, "Preserving customized equipment behavior: " + model + ": " + str(shape))
    identity = unreal.SystemLibrary.conv_primary_asset_id_to_string(unreal.SystemLibrary.get_primary_asset_id_from_object(asset))
    expected_id = "RPGItemDefinition:" + item_tag(model)
    require(not identity or identity == expected_id, "Primary identity differs: " + model)
    return {"model": model, "expected_primary_asset_id": expected_id, "definition_version": 1,
            "slot_index": SPECS[model][3], "compatible_slot": str(expected_slot), "max_stack_size": 1,
            "grid_size": [1, 1], "equipment": shape, "rolled_stat_fragment_count": 0,
            "growth_instance_initialized": False, "validation": validate(unreal, asset)}


def loaded_module():
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.GetModuleHandleW.argtypes = [ctypes.c_wchar_p]
    kernel.GetModuleHandleW.restype = ctypes.c_void_p
    kernel.GetModuleFileNameW.argtypes = [ctypes.c_void_p, ctypes.c_wchar_p, ctypes.c_uint32]
    kernel.GetModuleFileNameW.restype = ctypes.c_uint32
    handle = kernel.GetModuleHandleW("UnrealEditor-Project_RPG.dll")
    buffer = ctypes.create_unicode_buffer(32768)
    require(handle and kernel.GetModuleFileNameW(handle, buffer, len(buffer)), "Cannot observe loaded project module")
    module = Path(buffer.value).resolve()
    expected = os.environ.get("RPG_PROGRESSION_EQUIPMENT_MODULE")
    require(expected and module == Path(expected).resolve(), "Unexpected loaded module: " + str(module))
    return {"path": str(module), "sha256": digest(module)}


def main():
    import unreal
    flags = unreal.SystemLibrary.get_command_line().lower().split()
    apply = "-rpgcreateprogressionequipmentassets" in flags
    reload = "-rpgprogressionequipmentassetsreload" in flags
    require(not (apply and reload), "Reload must remain read-only")
    output = os.environ.get("RPG_PROGRESSION_EQUIPMENT_REPORT")
    require(output and Path(output).is_absolute(), "Set absolute RPG_PROGRESSION_EQUIPMENT_REPORT")
    require(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is None, "Author outside PIE")
    module = loaded_module()
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.wait_for_completion()
    editor = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    targets = {asset_path(m) for m in SPECS}
    require(not dirty(unreal).intersection(targets | {TEMPLATE}), "Preserving unsaved source/targets")
    base = editor.load_asset(TEMPLATE)
    require(isinstance(base, unreal.RPGItemDefinition)
            and base.get_editor_property("item_tag").export_text() == tag(unreal, "GameItem.Equipment.Weapon.GreatSword").export_text(),
            "Expected native GreatSword template")
    base_fragments = list(base.get_editor_property("fragments"))
    require(len(base_fragments) == 1 and isinstance(base_fragments[0], unreal.RPGItemEquipmentDefinitionFragment),
            "GreatSword template must retain its single equipment fragment")
    source = base_fragments[0]
    require(all(v is not None for v in source.get_editor_property("granted_ability_sets")), "Source AbilitySet failed to load")
    require(path_of(source.get_editor_property("equipped_actor_class")) not in ("None", ""), "Source equipped actor is missing")
    base_hash = digest(disk(unreal, TEMPLATE))
    armor_default = unreal.get_default_object(unreal.RPGItemEquipmentDefinitionFragment.static_class())
    require(equipment_value(source, "weapon_type") == unreal.RPGGladiatorWeaponType.GREAT_SWORD,
            "GreatSword template has another weapon identity")
    # The existing loadout author intentionally leaves hand policy at Count.
    # Reuse it exactly; a new item definition must not invent a two-hand rule.
    expected_shapes = {"weapon": equipment_shape(source), "chest": equipment_shape(armor_default), "gloves": equipment_shape(armor_default)}
    slots = {"weapon": unreal.EquipmentSlotType.WEAPON_PRIMARY_R,
             "chest": unreal.EquipmentSlotType.CHEST, "gloves": unreal.EquipmentSlotType.HANDS}
    require(slots["weapon"] in source.get_editor_property("compatible_slots"), "GreatSword template does not support the primary right slot")
    before, assets, states = {}, {}, {}
    content = Path(unreal.Paths.project_content_dir()).resolve()
    for model in SPECS:
        package = asset_path(model)
        path = disk(unreal, package)
        require(content in path.resolve().parents, "Target escapes private Content through a link: " + package)
        require(editor.does_asset_exist(package) == path.exists(), "Registry/disk mismatch: " + package)
        before[model] = digest(path)
        assets[model] = editor.load_asset(package) if before[model] else None
        tag(unreal, item_tag(model))
        if assets[model]:
            kind = SPECS[model][2]
            states[model] = inspect(unreal, assets[model], model, expected_shapes[kind], slots[kind])
    saved = []
    if apply:
        for model in SPECS:
            if assets[model]:
                continue
            package = asset_path(model)
            require(not disk(unreal, package).exists() and not editor.does_asset_exist(package), "Target appeared: " + package)
            asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
                package.rsplit("/", 1)[1], package.rsplit("/", 1)[0], unreal.RPGItemDefinition, unreal.DataAssetFactory())
            require(asset is not None, "Could not create " + package)
            unreal.EditorAssetLibrary.set_metadata_tag(asset, "RPGProgressionEquipmentSpec", MARKER)
            unreal.EditorAssetLibrary.set_metadata_tag(asset, "RPGProgressionEquipmentModel", model)
            for key, value in {"item_tag": tag(unreal, item_tag(model)), "definition_version": 1,
                               "item_category": unreal.ItemCategory.EQUIP, "display_name": unreal.Text(SPECS[model][1]),
                               "description": unreal.Text("첫 성장 구간에서 사용하는 장비."),
                               "max_stack_size": 1, "grid_size": unreal.IntPoint(1, 1)}.items():
                asset.set_editor_property(key, value)
            fragment = unreal.new_object(unreal.RPGItemEquipmentDefinitionFragment, outer=asset, name="Equipment")
            kind = SPECS[model][2]
            if kind == "weapon":
                for key in EQUIPMENT_FIELDS:
                    value = equipment_value(source, key)
                    if isinstance(value, dict):
                        require(equipment_value(fragment, key) == value, "Native default differs: " + key)
                    else:
                        fragment.set_editor_property(key, value)
            fragment.set_editor_property("compatible_slots", {slots[kind]})
            asset.set_editor_property("fragments", [fragment])
            states[model] = inspect(unreal, asset, model, expected_shapes[kind], slots[kind])
            require(not disk(unreal, package).exists(), "Disk target appeared before save")
            require(editor.save_loaded_asset(asset, only_if_is_dirty=False), "Save failed: " + package)
            assets[model] = asset
            saved.append(model)
    current_dirty = dirty(unreal)
    rows, identities = [], {}
    for model in SPECS:
        package = asset_path(model)
        current = digest(disk(unreal, package))
        require(before[model] is None or current == before[model], "Existing target changed: " + package)
        require(package not in current_dirty, "Target remains dirty: " + package)
        if assets[model]:
            identity = unreal.SystemLibrary.conv_primary_asset_id_to_string(unreal.SystemLibrary.get_primary_asset_id_from_object(assets[model]))
            identities[model] = identity
            if reload:
                require(identity == "RPGItemDefinition:" + item_tag(model), "Reloaded ID is not registered: " + model)
        elif reload:
            require(False, "Reload target missing: " + model)
        rows.append({"asset": package, "model": model, "exists": current is not None, "saved": model in saved,
                     "sha256": current, "dirty": False, "properties": states.get(model)})
    require(base_hash == digest(disk(unreal, TEMPLATE)), "Original GreatSword template changed")
    report = {"read_only": not apply, "saved_count": len(saved), "missing_count": sum(not r["exists"] for r in rows),
              "assets": rows, "registered_primary_asset_ids": identities, "loaded_module": module,
              "source_template": TEMPLATE, "source_template_sha256": base_hash,
              "source_weapon_equipment": expected_shapes["weapon"], "original_installed": False,
              "gameplay_or_grant_executed": False,
              "special_weapon_effects_implemented": all(
                  assets[model] and any(isinstance(fragment, getattr(unreal, fragment_type, ()))
                                        for fragment in assets[model].get_editor_property("fragments"))
                  for model, fragment_type in (("EnduranceGreatSword", "RPGItemShieldOnHitDefinitionFragment"),
                                               ("CirculationGreatSword", "RPGItemResolveOnHitDefinitionFragment")))}
    Path(output).parent.mkdir(parents=True, exist_ok=True)
    Path(output).write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    unreal.log("RPG_PROGRESSION_EQUIPMENT_ASSETS " + json.dumps(report, ensure_ascii=False))


if __name__ == "__main__":
    main()
