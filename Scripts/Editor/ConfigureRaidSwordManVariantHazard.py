"""Wire a phase-two ground hazard into ONLY the isolated SwordMan variant.

Back up the variant boss/definition/startup before -RPGApplySwordManVariantHazard.
Default is read-only verification. Existing custom patterns/tuning are rejected.
No original boss, common BT, map, player asset or native class is changed.
"""
import unreal

ROOT = "/Game/Blueprints/Character/NPC/Raid/SwordManVariant"
SOURCE = "/Game/Blueprints/Character/NPC/SwordMan"
PATH = ROOT + "/GameplayAbility/GA_RaidSwordManVariant_GroundHazard"
ACTION = "Boss.Action.SwordMan.GroundHazard"
COOLDOWN = "Boss.Cooldown.SwordMan.GroundHazard"
APPLY = "-rpgapplyswordmanvarianthazard" in unreal.SystemLibrary.get_command_line().lower()
TUNING = {"windup_seconds": 1.5, "radius": 250.0, "duration_seconds": 5.0,
          "pulse_interval": 1.0, "maximum_active_hazards": 1, "PatternCooldownSeconds": 12.0}


def check(value, message):
    if not value:
        raise RuntimeError(message)


def load(path, kind):
    value = unreal.load_asset(path)
    check(isinstance(value, kind), "Missing/incompatible asset: " + path)
    return value


def tag(name):
    value = unreal.GameplayTag()
    check(value.import_text('(TagName="{}")'.format(name)), "Missing tag: " + name)
    return value


def tag_name(value):
    return str(value.get_editor_property("TagName"))


def default(bp):
    return unreal.get_default_object(bp.generated_class())


def compile_bp(bp):
    bp.modify()
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    check("ERROR" not in str(bp.get_editor_property("status")).upper(), "Compile failed: " + bp.get_path_name())


def save(asset):
    check(unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False),
          "Could not save: " + asset.get_path_name())


boss = load(ROOT + "/BP_RaidSwordManVariant", unreal.Blueprint)
definition = load(ROOT + "/DA_RaidSwordManVariant", unreal.RPGBossDefinition)
startup = load(ROOT + "/DA_RaidSwordManVariantStartUp", unreal.DataAsset)
original_definition = load(SOURCE + "/DA_RaidSwordMan", unreal.RPGBossDefinition)
original_startup = load(SOURCE + "/DA_RaidSwordManStartUp", unreal.DataAsset)
material = load(SOURCE + "/Presentation/M_RaidSwordManTelegraph", unreal.Material)
damage_class = unreal.EditorAssetLibrary.load_blueprint_class(
    "/Game/Blueprints/Character/Shared/GameplayEffect/GE_Shared_ApplyDamage")
check(damage_class, "Missing shared damage effect")
action, cooldown = tag(ACTION), tag(COOLDOWN)
phases = list(definition.get_editor_property("phases"))
original_phases = list(original_definition.get_editor_property("phases"))
check(len(phases) == len(original_phases) == 2, "Expected two phases")
original_patterns = [list(phase.get_editor_property("ability_patterns")) for phase in original_phases]
for phase, original in zip(phases, original_phases):
    check(tag_name(phase.get_editor_property("phase_tag")) == tag_name(original.get_editor_property("phase_tag")) and
          phase.get_editor_property("health_threshold") == original.get_editor_property("health_threshold"),
          "Custom phase gate; refusing overwrite")
original_grants = list(original_startup.get_editor_property("NPCStartUpAbilities"))
for field in ("ActivateOnGivenAbilities", "ReactiveAbilities", "StartUpGameplayEffects"):
    check(list(startup.get_editor_property(field)) == list(original_startup.get_editor_property(field)),
          "Custom startup policy: " + field)
component = default(boss).get_editor_property("boss_telegraph_presentation")
check(component and component.get_outer() == default(boss), "Refusing shared presentation component")
check(default(boss).get_editor_property("character_start_up_data") == startup and
      default(boss).get_editor_property("boss_ai_component").get_editor_property("definition") == definition,
      "Variant is not wired to its isolated data")

exists = unreal.EditorAssetLibrary.does_asset_exist(PATH)
attack = load(PATH, unreal.Blueprint) if exists else None
attack_class = attack.generated_class() if attack else None
desired_patterns = [[*original_patterns[0]], [attack_class, *original_patterns[1]]]
grants = list(startup.get_editor_property("NPCStartUpAbilities"))
dash_path = ROOT + "/GameplayAbility/GA_RaidSwordManVariant_Dash"
dash = load(dash_path, unreal.Blueprint).generated_class() if unreal.EditorAssetLibrary.does_asset_exist(dash_path) else None
extra_grants = [dash] if dash and dash in grants else []
summon_path = ROOT + "/GameplayAbility/GA_RaidSwordManVariant_Summon"
summon_class = load(summon_path, unreal.Blueprint).generated_class() if unreal.EditorAssetLibrary.does_asset_exist(summon_path) else None
if summon_class and summon_class in grants:
    extra_grants.append(summon_class)
if dash and dash in extra_grants:
    desired_patterns[1].insert(2, dash)
if summon_class and summon_class in extra_grants:
    desired_patterns[1].insert(0, summon_class)
check(grants == original_grants or (attack_class and grants == [*original_grants, attack_class, *extra_grants]),
      "Custom startup grants; refusing overwrite")
for index, phase in enumerate(phases):
    current = list(phase.get_editor_property("ability_patterns"))
    check(current == original_patterns[index] or (attack_class and current == desired_patterns[index]),
          "Custom phase patterns; refusing overwrite")
styles = list(component.get_editor_property("styles"))
hazard_styles = [style for style in styles if tag_name(style.get_editor_property("action_tag")) == ACTION]
check(len(hazard_styles) <= 1, "Duplicate hazard style")
for style in hazard_styles:
    check(style.get_editor_property("radius") == TUNING["radius"] and
          style.get_editor_property("material").get_path_name() == material.get_path_name(),
          "Custom hazard style; refusing overwrite")


def verify_attack(bp):
    check(bp.get_blueprint_parent_class() == unreal.RPGBossPersistentHazardAbility.static_class(),
          "Wrong hazard parent")
    cdo = default(bp)
    for field, value in TUNING.items():
        check(abs(cdo.get_editor_property(field) - value) < .0001, "Custom hazard tuning: " + field)
    check(tag_name(cdo.get_editor_property("action_tag")) == ACTION and
          tag_name(cdo.get_editor_property("PatternCooldownTag")) == COOLDOWN, "Wrong hazard tags")
    check(cdo.get_editor_property("damage_effect_class") == damage_class and
          cdo.get_editor_property("hazard_class") == unreal.RPGBossPersistentHazard.static_class() and
          cdo.get_editor_property("cooldown_gameplay_effect_class") is None, "Wrong hazard effect/classes")
    check(cdo.get_editor_property("base_damage").export_text() == unreal.ScalableFloat(value=12.0).export_text(),
          "Custom hazard damage")
    security = cdo.get_editor_property("security_profile")
    for field, value in {"maximum_server_hit_distance": 1000.0, "hit_location_tolerance": 80.0,
                         "maximum_targets_per_query": 16, "maximum_hits_per_activation": 5,
                         "maximum_damage_per_hit": 1000.0, "bRequireAuthorityHitQuery": True}.items():
        check(security.get_editor_property(field) == value, "Custom hazard security: " + field)


if attack:
    verify_attack(attack)
elif APPLY:
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", unreal.RPGBossPersistentHazardAbility.static_class())
    folder, name = PATH.rsplit("/", 1)
    attack = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.Blueprint, factory)
    check(attack, "Could not create hazard ability")
    cdo = default(attack)
    for field, value in TUNING.items():
        cdo.set_editor_property(field, value)
    cdo.set_editor_property("action_tag", action)
    cdo.set_editor_property("PatternCooldownTag", cooldown)
    cdo.set_editor_property("damage_effect_class", damage_class)
    cdo.set_editor_property("base_damage", unreal.ScalableFloat(value=12.0))
    cdo.set_editor_property("security_profile", unreal.RPGSkillSecurityProfile(
        maximum_server_hit_distance=1000.0, hit_location_tolerance=80.0,
        maximum_targets_per_query=16, maximum_hits_per_activation=5, maximum_damage_per_hit=1000.0))
    compile_bp(attack)
    verify_attack(attack)
    save(attack)
else:
    raise RuntimeError("Hazard is absent; use -RPGApplySwordManVariantHazard after backing up the variant")

attack_class = attack.generated_class()
desired_patterns = [original_patterns[0], [attack_class, *original_patterns[1]]]
if dash and dash in extra_grants:
    desired_patterns[1].insert(2, dash)
if summon_class and summon_class in extra_grants:
    desired_patterns[1].insert(0, summon_class)
if APPLY:
    if grants != [*original_grants, attack_class, *extra_grants]:
        startup.modify()
        startup.set_editor_property("NPCStartUpAbilities", [*original_grants, attack_class, *extra_grants])
        save(startup)
    if any(list(phase.get_editor_property("ability_patterns")) != desired
           for phase, desired in zip(phases, desired_patterns)):
        definition.modify()
        for phase, desired in zip(phases, desired_patterns):
            phase.set_editor_property("ability_patterns", desired)
        definition.set_editor_property("phases", phases)
        save(definition)
    if not hazard_styles:
        style = unreal.RPGBossTelegraphStyle()
        style.set_editor_property("action_tag", action)
        style.set_editor_property("radius", TUNING["radius"])
        style.set_editor_property("material", material)
        style.set_editor_property("color", unreal.LinearColor(1.0, .25, .02, 1.0))
        component.modify()
        component.set_editor_property("styles", [*styles, style])
        compile_bp(boss)
        save(boss)

check(list(startup.get_editor_property("NPCStartUpAbilities")) == [*original_grants, attack_class, *extra_grants], "Hazard not granted")
check(all(list(phase.get_editor_property("ability_patterns")) == desired
          for phase, desired in zip(definition.get_editor_property("phases"), desired_patterns)), "Hazard not selected in phase two")
styles = list(default(boss).get_editor_property("boss_telegraph_presentation").get_editor_property("styles"))
check(sum(tag_name(style.get_editor_property("action_tag")) == ACTION for style in styles) == 1, "Missing hazard style")
unreal.log("RAID_SWORDMAN_VARIANT_HAZARD COMPLETE Phase2Priority=1 Cooldown=12 Windup=1.5 Radius=250 Lifetime=5 DamagePerPulse=12 Applied=" + str(int(APPLY)))
