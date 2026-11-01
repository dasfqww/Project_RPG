"""Enable player-owned shortcut input and wire its existing development Experience.

Run after CreateExperienceDevelopment.py in UnrealEditor-Cmd with
-run=PythonScript -Script=<this file> -RPGConfigureDevelopmentInput -nowrite.
Only the five packages in ALLOWED_PACKAGES can be saved. No gameplay is started.
"""
import json
import pathlib
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import unreal
import CreateExperienceDevelopment as base

SHORTCUT_SET = "/Game/System/Experiences/ActionSets/DA_OptionsShortcut"
SHORTCUT_CONTEXT = "/RPGInputMapping/Input/IMC_OptionsShortcut"
ALLOWED_PACKAGES = {
    SHORTCUT_SET, base.MODE_PATH, base.CONTROLLER_PATH,
    base.INPUT_CONFIG_PATH, base.INPUT_CONTEXT_PATH,
}
CREATED = []
UPDATED = []
PRESERVED = []
COMPILED = []


def get_property(obj, *names):
    for name in names:
        try:
            return name, obj.get_editor_property(name)
        except Exception:
            pass
    raise RuntimeError("Required reflected property unavailable: " + "/".join(names)
                       + ". Build/restart the Editor modules before applying.")


def asset_path(value):
    if value is None:
        return ""
    if hasattr(value, "get_path_name"):
        return value.get_path_name().split(".", 1)[0]
    if hasattr(value, "to_string"):
        return value.to_string().split(".", 1)[0]
    return str(value).split(".", 1)[0]


def save_owned(asset, path, created=False):
    base.check(path in ALLOWED_PACKAGES, "Refusing to save outside the input migration")
    base.check(unreal.EditorAssetLibrary.save_loaded_asset(asset, False), "Save failed: " + path)
    (CREATED if created else UPDATED).append(path)
    unreal.log("RPG_EXPERIENCE_INPUT SAVED " + path)


def check_empty_context(context):
    data = context.get_editor_property("DefaultKeyMappings")
    base.check(len(data.get_editor_property("Mappings")) == 0, "Development context must stay empty")
    base.check(len(context.get_editor_property("MappingProfileOverrides")) == 0,
               "Unexpected development key profiles")


def prepare_data(path, asset_class, expected=None):
    exists = unreal.EditorAssetLibrary.does_asset_exist(path)
    if exists:
        asset = base.load_asset(path)
        base.check(asset.get_class() == asset_class, "Unexpected data asset type: " + path)
    else:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", asset_class)
        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            base.short_name(path), base.ROOT, asset_class, factory)
        base.check(asset is not None, "Create failed: " + path)
    if expected is not None:
        if not exists:
            for name, value in expected.items():
                asset.set_editor_property(name, value)
        base.check_properties(asset, expected, path)
    else:
        check_empty_context(asset)
    if exists:
        PRESERVED.append(path)
    else:
        save_owned(asset, path, True)
    return asset


def compile_blueprint(blueprint, path):
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    base.check(blueprint.get_editor_property("status") == unreal.BlueprintStatus.BS_UP_TO_DATE,
               "Blueprint did not compile cleanly: " + path)
    COMPILED.append(path)
    return unreal.get_default_object(blueprint.generated_class())


def prepare_controller(parent, config):
    path = base.CONTROLLER_PATH
    exists = unreal.EditorAssetLibrary.does_asset_exist(path)
    if exists:
        blueprint = base.load_asset(path)
        base.check(isinstance(blueprint, unreal.Blueprint), "Expected controller Blueprint")
        base.check(blueprint.get_editor_property("status") == unreal.BlueprintStatus.BS_UP_TO_DATE,
                   "Development controller must compile cleanly")
        base.check(unreal.BlueprintEditorLibrary.get_blueprint_parent_class(blueprint) == parent,
                   "Unexpected controller parent")
        base.check_properties(unreal.get_default_object(blueprint.generated_class()),
                              {"InputConfigDataAsset": config}, path)
        PRESERVED.append(path)
    else:
        factory = unreal.BlueprintFactory()
        factory.set_editor_property("parent_class", parent)
        blueprint = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            base.short_name(path), base.ROOT, unreal.Blueprint, factory)
        base.check(blueprint is not None, "Could not create development controller")
        compile_blueprint(blueprint, path)
        defaults = unreal.get_default_object(blueprint.generated_class())
        defaults.set_editor_property("InputConfigDataAsset", config)
        defaults = compile_blueprint(blueprint, path)
        base.check_properties(defaults, {"InputConfigDataAsset": config}, path)
        save_owned(blueprint, path, True)
    return blueprint.generated_class()


def main():
    base.check("-RPGConfigureDevelopmentInput" in unreal.SystemLibrary.get_command_line(),
               "Explicit -RPGConfigureDevelopmentInput is required")
    # Check existing shared data and reflected schema before creating or changing any package.
    parent = base.load_class("/Script/Project_RPG.RPGPlayerController")
    legacy_parent = base.load_class("/Script/Engine.PlayerController")
    context_class = base.load_class("/Script/EnhancedInput.InputMappingContext")
    config_class = base.load_class("/Script/Project_RPG.DataAsset_InputConfig")
    mapping_action_class = base.load_class("/Script/RPGInputMappingRuntime.GameFeatureAction_RPGInputMapping")
    for path in (base.EXPERIENCE_PATH, base.HUD_PATH, base.MAP_PATH):
        base.check(unreal.EditorAssetLibrary.does_asset_exist(path),
                   "Run CreateExperienceDevelopment.py first: " + path)
    shortcut_set = base.load_asset(SHORTCUT_SET)
    base.check(list(shortcut_set.get_editor_property("game_features_to_enable"))
               == ["RPGInputMapping", "RPGOptionsUI"], "Unexpected shortcut feature composition")
    actions = list(shortcut_set.get_editor_property("actions"))
    base.check(len(actions) == 2, "Unexpected shortcut action count")
    owners = [value for value in actions if value.get_class() == mapping_action_class]
    base.check(len(owners) == 1, "Expected one input mapping action")
    owner = owners[0]
    mappings = owner.get_editor_property("InputMappings")
    targets = [(index, value) for index, value in enumerate(mappings)
               if asset_path(value.get_editor_property("MappingContext")) == SHORTCUT_CONTEXT]
    base.check(len(targets) == 1, "Expected exactly one options shortcut registration")
    index, entry = targets[0]
    copy_property, old_copy = get_property(entry, "bUsePlayerOwnedCopy", "use_player_owned_copy")
    priority = entry.get_editor_property("Priority")
    shortcut_context = base.load_asset(SHORTCUT_CONTEXT)
    tracking = str(shortcut_context.get_editor_property("RegistrationTrackingMode")).replace("_", "").lower()
    base.check("countregistrations" in tracking, "Shortcut context must use CountRegistrations")
    mode = base.load_asset(base.MODE_PATH)
    base.check(isinstance(mode, unreal.Blueprint), "Expected development GameMode Blueprint")
    base.check(unreal.BlueprintEditorLibrary.get_blueprint_parent_class(mode)
               == base.load_class("/Script/Engine.GameModeBase"), "Unexpected development GameMode parent")
    defaults = unreal.get_default_object(mode.generated_class())
    expected_mode = {
        "game_state_class": base.load_class("/Script/Project_RPG.RPGGameStateBase"),
        "player_state_class": base.load_class("/Script/Engine.PlayerState"),
        "default_pawn_class": base.load_class("/Script/Engine.SpectatorPawn"),
        "hud_class": base.load_class(base.HUD_PATH + "." + base.short_name(base.HUD_PATH) + "_C"),
    }
    base.check_properties(defaults, expected_mode, "Development mode")
    current_controller = defaults.get_editor_property("player_controller_class")
    if unreal.EditorAssetLibrary.does_asset_exist(base.CONTROLLER_PATH):
        wanted = base.load_class(base.CONTROLLER_PATH + "." + base.short_name(base.CONTROLLER_PATH) + "_C")
        base.check(current_controller in (legacy_parent, wanted), "Unexpected custom development controller")
    else:
        base.check(current_controller == legacy_parent, "Unexpected custom development controller")

    context = prepare_data(base.INPUT_CONTEXT_PATH, context_class)
    config = prepare_data(base.INPUT_CONFIG_PATH, config_class, {
        "DefaultMappingContext": context, "NativeInputActions": [], "AbilityInputActions": [],
    })
    controller = prepare_controller(parent, config)
    if current_controller != controller:
        defaults.set_editor_property("player_controller_class", controller)
        defaults = compile_blueprint(mode, base.MODE_PATH)
        base.check_properties(defaults, dict(expected_mode, player_controller_class=controller), "Updated mode")
        save_owned(mode, base.MODE_PATH)
    else:
        PRESERVED.append(base.MODE_PATH)

    if not old_copy:
        entry.set_editor_property(copy_property, True)
        mappings[index] = entry
        owner.set_editor_property("InputMappings", mappings)
        applied = owner.get_editor_property("InputMappings")[index]
        base.check(get_property(applied, copy_property)[1] is True, "Player copy was not applied")
        base.check(applied.get_editor_property("Priority") == priority, "Shortcut priority changed")
        base.check(asset_path(applied.get_editor_property("MappingContext")) == SHORTCUT_CONTEXT,
                   "Shortcut source changed")
        save_owned(shortcut_set, SHORTCUT_SET)
    else:
        PRESERVED.append(SHORTCUT_SET)
    check_empty_context(context)
    result = {
        "created": CREATED, "updated": UPDATED, "preserved": PRESERVED,
        "blueprints_compiled": COMPILED,
        "controller": base.CONTROLLER_PATH, "input_config": base.INPUT_CONFIG_PATH,
        "primary_context": base.INPUT_CONTEXT_PATH, "primary_binding_count": 0,
        "shortcut_action_set": SHORTCUT_SET, "player_owned_copy": True,
        "shortcut_priority": priority, "shortcut_key_asset_modified": False,
        "gameplay_started": False, "tests_run": False,
    }
    record = pathlib.Path(__file__).resolve().parents[2] / "Saved" / "Logs" / "RPGExperienceInputAssets-20261024"
    record.mkdir(parents=True, exist_ok=True)
    (record / "AssetApplication.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    unreal.log("RPG_EXPERIENCE_INPUT ASSETS_APPLIED " + json.dumps(result, sort_keys=True))


if __name__ == "__main__":
    main()
