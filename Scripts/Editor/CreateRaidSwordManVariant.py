"""Connect an isolated SwordMan copy to the common, phase-agnostic boss BT.

Opt-in creation: -RPGCreateRaidSwordManVariant. Existing assets are read-only;
partial bundles are rejected. No map/GameMode, source boss or ability is saved.
The initial variant retains source attacks, grants/effects and phase settings.
The opt-in phase-two hazard/dash/summon extensions are accepted on verification.
"""
import importlib.util
import os
import unreal

SOURCE = "/Game/Blueprints/Character/NPC/SwordMan"
COMMON = "/Game/Blueprints/Character/NPC/Raid/Common"
ROOT = "/Game/Blueprints/Character/NPC/Raid/SwordManVariant"
APPLY = "-rpgcreateraidswordmanvariant" in unreal.SystemLibrary.get_command_line().lower()
SOURCES = {
    "blackboard": COMMON + "/BB_RaidBossTemplate",
    "tree": COMMON + "/BT_RaidBossTemplate",
    "definition": SOURCE + "/DA_RaidSwordMan",
    "startup": SOURCE + "/DA_RaidSwordManStartUp",
    "controller": SOURCE + "/AIC_RaidSwordMan",
    "boss": SOURCE + "/BP_RaidSwordMan",
}
NAMES = {
    "blackboard": "BB_RaidSwordManVariant",
    "tree": "BT_RaidSwordManVariant",
    "definition": "DA_RaidSwordManVariant",
    "startup": "DA_RaidSwordManVariantStartUp",
    "controller": "AIC_RaidSwordManVariant",
    "boss": "BP_RaidSwordManVariant",
}


def require(value, message):
    if not value:
        raise RuntimeError(message)


def load(path):
    value = unreal.load_asset(path)
    require(value, "Missing asset: " + path)
    return value


def classes(values):
    return [value.get_path_name() if value else None for value in values]


def default(blueprint):
    require(isinstance(blueprint, unreal.Blueprint), "Expected Blueprint")
    return unreal.get_default_object(blueprint.generated_class())


def compile_bp(blueprint):
    blueprint.modify()
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    require("ERROR" not in str(blueprint.get_editor_property("status")).upper(),
            "Blueprint compile failed: " + blueprint.get_path_name())


def verify(bundle, sources, template):
    tree, blackboard = bundle["tree"], bundle["blackboard"]
    dash_path = ROOT + "/GameplayAbility/GA_RaidSwordManVariant_Dash"
    dash = (load(dash_path).generated_class()
            if unreal.EditorAssetLibrary.does_asset_exist(dash_path) else None)
    template.verify(tree, blackboard, dash_class=dash)
    for key, asset in bundle.items():
        require(asset != sources[key] and asset.get_path_name() == ROOT + "/" + NAMES[key] + "." + NAMES[key],
                "Variant asset is not isolated: " + key)
    require(isinstance(bundle["definition"], unreal.RPGBossDefinition), "Expected boss definition")
    require(bundle["startup"].get_class() == sources["startup"].get_class(), "Startup class differs")
    for key in ("boss", "controller"):
        require(bundle[key].get_blueprint_parent_class() == sources[key].get_blueprint_parent_class(),
                "Blueprint parent differs: " + key)
        require("ERROR" not in str(bundle[key].get_editor_property("status")).upper(), "Blueprint is not compiled")
    boss = default(bundle["boss"])
    require(boss.get_editor_property("ai_controller_class") == bundle["controller"].generated_class(), "Wrong AIController")
    require(boss.get_editor_property("character_start_up_data") == bundle["startup"], "Wrong startup data")
    component = boss.get_editor_property("boss_ai_component")
    require(component and component.get_outer() == boss and component.get_editor_property("definition") == bundle["definition"],
            "Boss component must own an independent definition reference")
    require(default(bundle["controller"]).get_editor_property("BTAsset") == tree, "Controller is not using the variant tree")
    for field in ("EnrageSeconds", "EnragedAttackMultiplier"):
        require(bundle["definition"].get_editor_property(field) == sources["definition"].get_editor_property(field), "Enrage tuning changed")
    grants = classes(bundle["startup"].get_editor_property("NPCStartUpAbilities"))
    hazard_path = ROOT + "/GameplayAbility/GA_RaidSwordManVariant_GroundHazard"
    hazard = (load(hazard_path).generated_class()
              if unreal.EditorAssetLibrary.does_asset_exist(hazard_path) else None)
    original_grants = classes(sources["startup"].get_editor_property("NPCStartUpAbilities"))
    additions = [hazard.get_path_name()] if hazard else []
    if dash:
        require(hazard, "Dash extension requires the authored phase-two hazard")
        additions.append(dash.get_path_name())
    summon_path = ROOT + "/GameplayAbility/GA_RaidSwordManVariant_Summon"
    summon = (load(summon_path).generated_class()
              if unreal.EditorAssetLibrary.does_asset_exist(summon_path) else None)
    if summon:
        require(hazard and dash, "Summon extension requires the existing variant patterns")
        additions.append(summon.get_path_name())
    extended = bool(additions) and grants == original_grants + additions
    require(grants == original_grants or extended, "Unexpected startup grants")
    for field in ("ActivateOnGivenAbilities", "ReactiveAbilities", "StartUpGameplayEffects"):
        require(classes(bundle["startup"].get_editor_property(field)) == classes(sources["startup"].get_editor_property(field)),
                "Startup grants/effects changed: " + field)
    phases = list(bundle["definition"].get_editor_property("phases"))
    original_phases = list(sources["definition"].get_editor_property("phases"))
    require(len(phases) == len(original_phases) and len(phases) > 0, "Phase count changed")
    for index, (phase, original) in enumerate(zip(phases, original_phases)):
        tag_name = str(phase.get_editor_property("phase_tag").get_editor_property("TagName"))
        original_tag_name = str(original.get_editor_property("phase_tag").get_editor_property("TagName"))
        require(tag_name == original_tag_name and
                phase.get_editor_property("health_threshold") == original.get_editor_property("health_threshold"), "Phase gate changed")
        patterns = classes(phase.get_editor_property("ability_patterns"))
        expected = classes(original.get_editor_property("ability_patterns"))
        if extended and index == 1:
            expected = [hazard.get_path_name()] + expected
            if dash:
                expected.insert(2, dash.get_path_name())
            if summon:
                expected.insert(0, summon.get_path_name())
        require(patterns == expected and len(patterns) > 0,
                "Phase pattern priority changed or is empty")
        require(all(pattern in grants for pattern in patterns), "Phase references an ungranted ability")
    # The real encounter owner still decides when combat starts; do not turn an
    # authored battle boss into an auto-start NPC merely by duplicating it.
    require(boss.get_editor_property("bStartEncounterOnReady") == default(sources["boss"]).get_editor_property("bStartEncounterOnReady"),
            "Encounter startup policy changed")


def create(sources, template):
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    bundle = {}
    for key, source in sources.items():
        bundle[key] = tools.duplicate_asset(NAMES[key], ROOT, source)
        require(bundle[key], "Could not duplicate " + key)
    bundle["tree"].set_editor_property("blackboard_asset", bundle["blackboard"])
    controller = default(bundle["controller"])
    controller.set_editor_property("BTAsset", bundle["tree"])
    compile_bp(bundle["controller"])
    boss = default(bundle["boss"])
    boss.set_editor_property("ai_controller_class", bundle["controller"].generated_class())
    boss.set_editor_property("character_start_up_data", bundle["startup"])
    component = boss.get_editor_property("boss_ai_component")
    require(component and component.get_outer() == boss, "Refusing to edit a shared source component")
    component.modify()
    component.set_editor_property("definition", bundle["definition"])
    compile_bp(bundle["boss"])
    verify(bundle, sources, template)
    for asset in bundle.values():
        require(unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False),
                "Could not save new variant: " + asset.get_path_name())
    return bundle


# Reuse the existing template's structural validation instead of introducing a
# new native test class or a second implementation of its BT contract.
spec = importlib.util.spec_from_file_location("raid_boss_bt_template", os.path.join(os.path.dirname(__file__), "CreateRaidBossBTTemplate.py"))
template = importlib.util.module_from_spec(spec)
spec.loader.exec_module(template)
sources = {key: load(path) for key, path in SOURCES.items()}
template.verify(sources["tree"], sources["blackboard"])
exists = [unreal.EditorAssetLibrary.does_asset_exist(ROOT + "/" + name) for name in NAMES.values()]
if all(exists):
    bundle = {key: load(ROOT + "/" + name) for key, name in NAMES.items()}
elif any(exists):
    raise RuntimeError("Partial variant already exists; refusing to overwrite or repair existing assets")
elif APPLY:
    bundle = create(sources, template)
else:
    raise RuntimeError("Variant is absent. Use -RPGCreateRaidSwordManVariant to create only the isolated bundle")
verify(bundle, sources, template)
unreal.log("RAID_SWORDMAN_VARIANT COMPLETE IndependentAssets=6 CommonBT=1 GrantedPhasePatterns=1 OriginalBossUnchanged=1")
