"""Create the initial PvE dungeon rewards and wire content GameModes.

Run with Unreal Editor closed:
    UnrealEditor-Cmd.exe Project_RPG.uproject \
        -run=pythonscript \
        -script=Scripts/Editor/CreateDungeonRewardAssets.py \
        -unattended -nop4 -nosplash -NullRHI

Existing reward assets and existing difficulty overrides are preserved. The
script only supplies missing development defaults and is safe to run again.
"""

import unreal


REWARD_ROOT = "/Game/Blueprints/GameData/DungeonReward"
POTION_PATH = (
    "/Game/Blueprints/GameData/Item/V2/"
    "DA_Item_Potion_Red_Large"
)
HELM_PATH = "/Game/Blueprints/GameData/Item/V2/DA_Item_Helm_Leather"
CONTENT_GAME_MODES = (
    "/Game/Blueprints/GameMode/BP_SurvivalGameMode",
    "/Game/Blueprints/GameMode/BP_BossBattleGameMode",
)

REWARD_SPECS = (
    ("Easy", "DA_DungeonReward_Easy", "pve_easy_v1", 100, 1, False),
    ("Normal", "DA_DungeonReward_Normal", "pve_normal_v1", 250, 2, False),
    ("Hard", "DA_DungeonReward_Hard", "pve_hard_v1", 500, 3, True),
    ("Hell", "DA_DungeonReward_Hell", "pve_hell_v1", 1000, 5, True),
)


def _require_asset(editor_assets, path, expected_type):
    asset = editor_assets.load_asset(path)
    if asset is None:
        raise RuntimeError(f"Required asset is missing: {path}")
    if not isinstance(asset, expected_type):
        raise RuntimeError(
            f"Asset {path} is {asset.get_class().get_name()}, "
            f"expected {expected_type.__name__}"
        )
    return asset


def _currency_change(amount):
    change = unreal.RPGCurrencyChange()
    change.set_editor_property("currency_code", unreal.Name("RosterGold"))
    change.set_editor_property("delta", amount)
    return change


def _item_reward(item_definition, quantity):
    reward = unreal.RPGDungeonItemRewardEntry()
    reward.set_editor_property("item_definition", item_definition)
    reward.set_editor_property("quantity", quantity)
    return reward


def _create_reward(
    editor_assets,
    asset_tools,
    potion,
    helm,
    asset_name,
    reward_version,
    gold,
    potion_quantity,
    include_helm,
):
    asset_path = f"{REWARD_ROOT}/{asset_name}"
    existing = editor_assets.load_asset(asset_path)
    if existing is not None:
        if not isinstance(existing, unreal.RPGDungeonRewardDefinition):
            raise RuntimeError(
                f"Existing asset has the wrong type: {asset_path}"
            )
        unreal.log(f"Preserving existing dungeon reward: {asset_path}")
        return existing

    reward = asset_tools.create_asset(
        asset_name,
        REWARD_ROOT,
        unreal.RPGDungeonRewardDefinition,
        unreal.DataAssetFactory(),
    )
    if reward is None:
        raise RuntimeError(f"Failed to create dungeon reward: {asset_path}")

    item_rewards = [_item_reward(potion, potion_quantity)]
    if include_helm:
        item_rewards.append(_item_reward(helm, 1))

    reward.set_editor_property("reward_version", unreal.Name(reward_version))
    reward.set_editor_property("currency_changes", [_currency_change(gold)])
    reward.set_editor_property("item_rewards", item_rewards)
    if not editor_assets.save_asset(asset_path, only_if_is_dirty=False):
        raise RuntimeError(f"Failed to save dungeon reward: {asset_path}")

    unreal.log(f"Created dungeon reward: {asset_path}")
    return reward


def _difficulty_value(name):
    return getattr(unreal.RPGGameDifficulty, name.upper())


def _wire_game_mode(editor_assets, blueprint_path, rewards):
    blueprint = _require_asset(editor_assets, blueprint_path, unreal.Blueprint)
    generated_class = blueprint.generated_class()
    if generated_class is None:
        raise RuntimeError(f"Blueprint has no generated class: {blueprint_path}")

    default_object = unreal.get_default_object(generated_class)
    configured_rewards = {
        key: value
        for key, value in default_object.get_editor_property(
            "dungeon_clear_rewards_by_difficulty"
        ).items()
    }
    changed = False
    for difficulty, reward in rewards.items():
        if configured_rewards.get(difficulty) is None:
            configured_rewards[difficulty] = reward
            changed = True

    if not default_object.get_editor_property("give_reward"):
        default_object.set_editor_property("give_reward", True)
        changed = True
    if changed:
        default_object.set_editor_property(
            "dungeon_clear_rewards_by_difficulty", configured_rewards
        )
        blueprint.modify()
        if not editor_assets.save_asset(
            blueprint_path, only_if_is_dirty=False
        ):
            raise RuntimeError(f"Failed to save GameMode: {blueprint_path}")
        unreal.log(f"Wired dungeon rewards into: {blueprint_path}")
    else:
        unreal.log(f"Dungeon rewards already wired: {blueprint_path}")


def main():
    editor_assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    potion = _require_asset(
        editor_assets, POTION_PATH, unreal.RPGItemDefinition
    )
    helm = _require_asset(editor_assets, HELM_PATH, unreal.RPGItemDefinition)

    rewards = {}
    for (
        difficulty_name,
        asset_name,
        reward_version,
        gold,
        potion_quantity,
        include_helm,
    ) in REWARD_SPECS:
        rewards[_difficulty_value(difficulty_name)] = _create_reward(
            editor_assets,
            asset_tools,
            potion,
            helm,
            asset_name,
            reward_version,
            gold,
            potion_quantity,
            include_helm,
        )

    for blueprint_path in CONTENT_GAME_MODES:
        _wire_game_mode(editor_assets, blueprint_path, rewards)

    unreal.log("Dungeon reward asset setup completed successfully.")


main()
