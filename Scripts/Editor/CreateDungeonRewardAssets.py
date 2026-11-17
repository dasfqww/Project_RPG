"""Inspect ordinary PvE supplies; opt in with -RPGCreateDungeonRewardAssets.

The exact original pve_*_v1 bundle may be upgraded to v2 by appending shop
salvage. Customized, dirty and unknown-version assets are never overwritten.
-RPGDungeonRewardsOnly skips GameMode checks. RPG_DUNGEON_REWARD_REPORT sets
an absolute JSON path for saved/reloaded content evidence.
"""
import hashlib
import json
import os
from pathlib import Path
import unreal

REWARD_ROOT = "/Game/Blueprints/GameData/DungeonReward"
POTION_PATH = "/Game/Blueprints/GameData/Item/V2/DA_Item_Potion_Red_Large"
HELM_PATH = "/Game/Blueprints/GameData/Item/V2/DA_Item_Helm_Leather"
JUNK_PATH = "/Game/Blueprints/GameData/Item/Shop/DA_Item_BrokenPart"
JUNK_TAG = "GameItem.Material.Junk.BrokenPart"
JUNK_NAME = "망가진 부품"
JUNK_DESCRIPTION = "상점에 판매하는 잡동사니입니다. 장비 강화나 제작에 쓰이지 않습니다."
MARKER = "ShopSupplies.v1"
CONTENT_GAME_MODES = (
    "/Game/Blueprints/GameMode/BP_SurvivalGameMode",
    "/Game/Blueprints/GameMode/BP_BossBattleGameMode",
)
REWARD_SPECS = (
    ("Easy", "DA_DungeonReward_Easy", "pve_easy_v2", 100, 1, False),
    ("Normal", "DA_DungeonReward_Normal", "pve_normal_v2", 250, 2, False),
    ("Hard", "DA_DungeonReward_Hard", "pve_hard_v2", 500, 3, True),
    ("Hell", "DA_DungeonReward_Hell", "pve_hell_v2", 1000, 5, True),
)

def require(condition, message):
    if not condition:
        raise RuntimeError(message)

def disk(package):
    root = Path(unreal.Paths.project_content_dir()).resolve()
    target = root / (package[len("/Game/"):] + ".uasset")
    require(root in target.resolve().parents, "Target escapes Content through a link: " + package)
    return target

def digest(package):
    path = disk(package)
    return hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else None

def load(editor, path, cls):
    asset = editor.load_asset(path)
    require(isinstance(asset, cls), "Required asset missing or wrong type: " + path)
    return asset

def item_reward(item, quantity):
    return unreal.RPGDungeonItemRewardEntry(item_definition=item, quantity=quantity)

def validate(asset):
    state, errors, warnings = unreal.get_editor_subsystem(unreal.EditorValidatorSubsystem).is_object_valid(
        asset, unreal.DataValidationUsecase.SCRIPT)
    require(state != unreal.DataValidationResult.INVALID and not errors and not warnings,
            "Native validation failed: " + asset.get_path_name() + ": " + str((state, errors, warnings)))
    return {"result": str(state), "errors": [], "warnings": []}

def inspect_junk(asset):
    require(isinstance(asset, unreal.RPGItemDefinition), "Preserving another junk asset class")
    require(unreal.EditorAssetLibrary.get_metadata_tag(asset, "RPGShopSupplySpec") == MARKER,
            "Preserving a junk definition authored elsewhere")
    require(JUNK_TAG in asset.get_editor_property("item_tag").export_text(), "Junk tag differs")
    require(asset.get_editor_property("definition_version") == 1
            and asset.get_editor_property("max_stack_size") == 99
            and asset.get_editor_property("item_category") == unreal.ItemCategory.CRAFT,
            "Junk definition version, stack or category differs")
    require(str(asset.get_editor_property("display_name")) == JUNK_NAME
            and str(asset.get_editor_property("description")) == JUNK_DESCRIPTION,
            "Junk presentation differs")
    size = asset.get_editor_property("grid_size")
    require(size.x == size.y == 1 and not asset.get_editor_property("fragments")
            and not asset.get_editor_property("item_traits").gameplay_tags,
            "Junk must not acquire gameplay traits or fragments")
    return {"definition_type": "RPGItemDefinition", "definition_name": JUNK_TAG,
            "definition_version": 1, "max_stack_size": 99, "category": "Craft",
            "display_name": JUNK_NAME, "description": JUNK_DESCRIPTION, "validation": validate(asset)}

def inspect_reward(asset, spec, potion, helm, junk):
    mode, _, version, gold, quantity, include_helm = spec
    require(isinstance(asset, unreal.RPGDungeonRewardDefinition), "Preserving another reward class")
    actual_version = str(asset.get_editor_property("reward_version"))
    require(actual_version in (version, version[:-1] + "1"), "Preserving unknown reward version: " + actual_version)
    currencies = list(asset.get_editor_property("currency_changes"))
    require(len(currencies) == 1 and str(currencies[0].get_editor_property("currency_code")) == "RosterGold"
            and currencies[0].get_editor_property("delta") == gold, "Preserving customized currency rewards")
    entries = list(asset.get_editor_property("item_rewards"))
    expected = [item_reward(potion, quantity)]
    if include_helm:
        expected.append(item_reward(helm, 1))
    if actual_version == version:
        require(junk is not None, "Current reward has no shop salvage definition")
        expected.append(item_reward(junk, quantity))
    require(len(entries) == len(expected) and all(a.export_text() == b.export_text() for a, b in zip(entries, expected)),
            "Preserving customized item quantities, order, bindings or metadata: " + mode)
    return {"difficulty": mode, "reward_version": actual_version, "gold": gold,
            "potion_quantity": quantity, "helm_quantity": int(include_helm),
            "salvage_quantity": quantity if actual_version == version else 0,
            "item_count": len(entries), "validation": validate(asset)}

def main():
    flags = unreal.SystemLibrary.get_command_line().lower().split()
    apply = "-rpgcreatedungeonrewardassets" in flags
    rewards_only = "-rpgdungeonrewardsonly" in flags
    editor = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    require(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is None,
            "Author outside PIE")
    targets = [JUNK_PATH] + [REWARD_ROOT + "/" + spec[1] for spec in REWARD_SPECS]
    dirty = {str(p.get_name()) for p in list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
             + list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())}
    require(not dirty.intersection(targets + ([] if rewards_only else list(CONTENT_GAME_MODES))),
            "Preserving unsaved target packages")
    before = {path: digest(path) for path in targets}
    require(all(editor.does_asset_exist(path) == bool(before[path]) for path in targets), "Registry/disk mismatch")
    potion = load(editor, POTION_PATH, unreal.RPGItemDefinition)
    helm = load(editor, HELM_PATH, unreal.RPGItemDefinition)
    tag = unreal.GameplayTag()
    require(tag.import_text('(TagName="' + JUNK_TAG + '")') and JUNK_TAG in tag.export_text(),
            "Load Config/Tags/ShopItems.ini before authoring")
    junk = editor.load_asset(JUNK_PATH) if before[JUNK_PATH] else None
    if junk:
        inspect_junk(junk)
    existing = {}
    for spec in REWARD_SPECS:
        package = REWARD_ROOT + "/" + spec[1]
        existing[package] = editor.load_asset(package) if before[package] else None
        if existing[package]:
            inspect_reward(existing[package], spec, potion, helm, junk)
    saved = []
    if apply:
        tools = unreal.AssetToolsHelpers.get_asset_tools()
        if not junk:
            junk = tools.create_asset(JUNK_PATH.rsplit("/", 1)[1], JUNK_PATH.rsplit("/", 1)[0],
                                      unreal.RPGItemDefinition, unreal.DataAssetFactory())
            require(junk is not None, "Could not create junk definition")
            for key, value in {"item_tag": tag, "definition_version": 1, "max_stack_size": 99,
                               "item_category": unreal.ItemCategory.CRAFT, "display_name": unreal.Text(JUNK_NAME),
                               "description": unreal.Text(JUNK_DESCRIPTION), "grid_size": unreal.IntPoint(1, 1)}.items():
                junk.set_editor_property(key, value)
            unreal.EditorAssetLibrary.set_metadata_tag(junk, "RPGShopSupplySpec", MARKER)
            inspect_junk(junk)
            require(editor.save_loaded_asset(junk, only_if_is_dirty=False), "Could not save junk definition")
            saved.append(JUNK_PATH)
        for spec in REWARD_SPECS:
            _, name, version, gold, quantity, include_helm = spec
            package = REWARD_ROOT + "/" + name
            reward = existing[package]
            if reward and str(reward.get_editor_property("reward_version")) == version:
                continue
            if not reward:
                reward = tools.create_asset(name, REWARD_ROOT, unreal.RPGDungeonRewardDefinition, unreal.DataAssetFactory())
                require(reward is not None, "Could not create reward")
                change = unreal.RPGCurrencyChange(currency_code=unreal.Name("RosterGold"), delta=gold)
                reward.set_editor_property("currency_changes", [change])
                entries = [item_reward(potion, quantity)]
                if include_helm:
                    entries.append(item_reward(helm, 1))
            else:
                entries = list(reward.get_editor_property("item_rewards"))
            entries.append(item_reward(junk, quantity))
            reward.set_editor_property("reward_version", unreal.Name(version))
            reward.set_editor_property("item_rewards", entries)
            inspect_reward(reward, spec, potion, helm, junk)
            require(editor.save_loaded_asset(reward, only_if_is_dirty=False), "Could not save reward")
            existing[package] = reward
            saved.append(package)
    states = []
    if junk:
        states.append({"asset": JUNK_PATH, "properties": inspect_junk(junk), "sha256": digest(JUNK_PATH)})
    for spec in REWARD_SPECS:
        package = REWARD_ROOT + "/" + spec[1]
        if existing[package]:
            states.append({"asset": package, "properties": inspect_reward(existing[package], spec, potion, helm, junk),
                           "sha256": digest(package)})
    if not rewards_only:
        for path in CONTENT_GAME_MODES:
            blueprint = load(editor, path, unreal.Blueprint)
            require(blueprint.generated_class() is not None, "GameMode has no generated class")
            defaults = unreal.get_default_object(blueprint.generated_class())
            require(defaults.get_editor_property("give_reward"), "Enable GameMode rewards explicitly")
            configured = defaults.get_editor_property("dungeon_clear_rewards_by_difficulty")
            for spec in REWARD_SPECS:
                difficulty = getattr(unreal.RPGGameDifficulty, spec[0].upper())
                require(configured.get(difficulty) is not None, "Wire missing GameMode rewards explicitly")
    report = {"passed": True, "apply": apply, "saved": saved, "saved_count": len(saved), "assets": states,
              "initial_hashes": before, "game_mode_writes": 0, "progression_writes": 0}
    output = os.environ.get("RPG_DUNGEON_REWARD_REPORT")
    if output:
        require(Path(output).is_absolute(), "Report path must be absolute")
        Path(output).write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    unreal.log("Shop supplies inspected; saved=" + str(len(saved)))

main()