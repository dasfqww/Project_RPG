"""Validate loot presentation assets; -RPGApplyLootPresentation creates missing integration assets.

Only creates SC_LootDrops and DA_LootPresentationAssets. Existing authored VFX,
item identities, drop rates, meshes, gameplay maps and item definitions are preserved.
The explicit cook label retains code-selected soft references in packaged builds.
"""
import json
import unreal

ROOT = "/Game/DrapEffet/Integration"
MARKER = "RPGLootPresentation/v1"
FX = ("NE_drop_effects01", "NE_drop_effcts02", "NE_drop_effects03", "NE_drop_effects04",
      "NE_drop_effect05", "NE_drop_effects06", "NE_drop_effects07")
CUES = ("Pop_02_Cue", "Abstract_Pop_03_Cue", "Special_Musical_01_Cue",
        "Special_Powerup_04_Cue", "Special_Powerup_10_Cue", "Abstract_Pop_01_Cue")


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    apply = "-RPGApplyLootPresentation" in unreal.SystemLibrary.get_command_line()
    assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    paths = ["/Game/DrapEffet/VFX/" + name for name in FX]
    paths += ["/Game/Interface_And_Item_Sounds/Cues/" + name for name in CUES]
    loaded = []
    for path in paths:
        asset = assets.load_asset(path)
        expected = unreal.NiagaraSystem if "/VFX/" in path else unreal.SoundBase
        require(isinstance(asset, expected), "Missing/incorrect asset: " + path)
        loaded.append(asset)

    concurrency_path = ROOT + "/SC_LootDrops"
    label_path = ROOT + "/DA_LootPresentationAssets"
    concurrency = assets.load_asset(concurrency_path) if assets.does_asset_exist(concurrency_path) else None
    label = assets.load_asset(label_path) if assets.does_asset_exist(label_path) else None
    for asset in (concurrency, label):
        if asset:
            require(assets.get_metadata_tag(asset, "RPGPresentation") == MARKER,
                    "Existing asset has different ownership/version; preserve it: " + asset.get_path_name())

    changed = []
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    if apply and not concurrency:
        concurrency = tools.create_asset("SC_LootDrops", ROOT, unreal.SoundConcurrency, unreal.SoundConcurrencyFactory())
        require(isinstance(concurrency, unreal.SoundConcurrency), "Could not create concurrency asset")
        settings = concurrency.get_editor_property("concurrency")
        settings.set_editor_property("enable_max_count_platform_scaling", False)
        settings.set_editor_property("limit_to_owner", False)
        settings.set_editor_property("resolution_rule", unreal.MaxConcurrentResolutionRule.STOP_FARTHEST_THEN_OLDEST)
        settings.set_editor_property("retrigger_time", 0.045)
        concurrency.set_editor_property("concurrency", settings)
        require(concurrency.set_max_count(4), "Could not set maximum concurrent voices")
        assets.set_metadata_tag(concurrency, "RPGPresentation", MARKER)
        changed.append(concurrency)
    if concurrency:
        require(isinstance(concurrency, unreal.SoundConcurrency), "Wrong concurrency asset type")
        settings = concurrency.get_editor_property("concurrency")
        require(settings.get_editor_property("max_count") == 4
                and not settings.get_editor_property("enable_max_count_platform_scaling")
                and not settings.get_editor_property("limit_to_owner")
                and settings.get_editor_property("resolution_rule") == unreal.MaxConcurrentResolutionRule.STOP_FARTHEST_THEN_OLDEST
                and abs(settings.get_editor_property("retrigger_time") - 0.045) < 0.001,
                "Concurrency settings changed; review instead of overwriting")
        loaded.append(concurrency)

    if apply and not label:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", unreal.PrimaryAssetLabel)
        label = tools.create_asset("DA_LootPresentationAssets", ROOT, unreal.PrimaryAssetLabel, factory)
        require(isinstance(label, unreal.PrimaryAssetLabel), "Could not create cook label")
        rules = label.get_editor_property("rules")
        rules.set_editor_property("priority", 1)
        rules.set_editor_property("apply_recursively", True)
        rules.set_editor_property("cook_rule", unreal.PrimaryAssetCookRule.ALWAYS_COOK)
        label.set_editor_property("rules", rules)
        label.set_editor_property("label_assets_in_my_directory", False)
        label.set_editor_property("is_runtime_label", False)
        label.set_editor_property("explicit_assets", loaded)
        assets.set_metadata_tag(label, "RPGPresentation", MARKER)
        changed.append(label)
    if label:
        require(isinstance(label, unreal.PrimaryAssetLabel), "Wrong cook-label type")
        actual = {value.get_path_name() if isinstance(value, unreal.Object) else str(value)
                  for value in label.get_editor_property("explicit_assets")}
        expected = {asset.get_path_name() for asset in loaded}
        require(actual == expected, "Cook label references changed; review instead of overwriting")
        require(label.get_editor_property("rules").get_editor_property("cook_rule") == unreal.PrimaryAssetCookRule.ALWAYS_COOK,
                "Code-selected effects and sounds must be retained by the cooker")

    for asset in changed:
        require(assets.save_loaded_asset(asset, only_if_is_dirty=False), "Save failed: " + asset.get_path_name())
    result = {"passed": True, "applied": apply, "effect_count": len(FX), "cue_count": len(CUES),
              "saved": [asset.get_path_name() for asset in changed], "integration_ready": bool(concurrency and label)}
    unreal.log("RPG_LOOT_PRESENTATION " + json.dumps(result))


main()
