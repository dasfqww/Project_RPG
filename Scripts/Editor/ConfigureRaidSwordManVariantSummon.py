"""Author a real phase-two summon pattern; reuse existing NPC/GAS/lifetimes.

Back up variant boss/startup/definition before -RPGApplySwordManVariantSummon.
Default: read-only. No minion/source boss/BT/player/native code is saved.
"""
import importlib.util
import os
import unreal


def import_local(name):
    spec = importlib.util.spec_from_file_location(name, os.path.join(os.path.dirname(__file__), name + ".py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


base = import_local("ConfigureRaidSwordManVariantHazard")
check, load, default = base.check, base.load, base.default
ROOT = base.ROOT
PATH = ROOT + "/GameplayAbility/GA_RaidSwordManVariant_Summon"
MINION = "/Game/Blueprints/Character/NPC/Gruntling/Guardian/BP_Gruntling_Guardian"
ACTION = "Boss.Action.SwordMan.Summon"
COOLDOWN = "Boss.Cooldown.SwordMan.Summon"
APPLY = "-rpgapplyswordmanvariantsummon" in unreal.SystemLibrary.get_command_line().lower()
TUNING = {"windup_seconds": 2.0, "summon_lifetime": 25.0, "startup_timeout": 10.0,
          "maximum_owned_summons": 2, "PatternCooldownSeconds": 30.0, "bRequireNavigation": True}
OFFSETS = [unreal.Vector(-250.0, -300.0, 0.0), unreal.Vector(-250.0, 300.0, 0.0)]
boss, definition, startup = base.boss, base.definition, base.startup
minion_bp = load(MINION, unreal.Blueprint)
minion_class = minion_bp.generated_class()
minion = default(minion_bp)
check(isinstance(minion, unreal.RPGNonPlayerCharacter) and not isinstance(minion, unreal.RPGBossCharacter),
      "Summon must use an ordinary NPC, not a recursive raid boss")
check("ERROR" not in str(minion_bp.get_editor_property("status")).upper() and
      minion.get_editor_property("replicates"), "Minion must be a compiled replicated NPC")
minion_startup = load("/Game/Blueprints/Character/NPC/Gruntling/Guardian/DA_Guardian", unreal.DataAsset)
check(minion.get_editor_property("character_start_up_data") == minion_startup, "Unexpected minion startup data")
check(len(minion_startup.get_editor_property("NPCStartUpAbilities")) > 0 and
      len(minion_startup.get_editor_property("StartUpGameplayEffects")) > 0, "Minion needs existing GAS combat/stats")
for ability in minion_startup.get_editor_property("NPCStartUpAbilities"):
    check(ability and not isinstance(unreal.get_default_object(ability), unreal.RPGBossSummonAbility), "Recursive summon grant")
controller_class = minion.get_editor_property("ai_controller_class")
check(controller_class, "Minion is missing an AIController")
controller = unreal.get_default_object(controller_class)
check(isinstance(controller, unreal.AIController), "Minion controller must be server AI")
minion_tree = controller.get_editor_property("BTAsset")
check(isinstance(minion_tree, unreal.BehaviorTree) and minion_tree.get_editor_property("root_node") and
      minion_tree.get_editor_property("blackboard_asset"), "Minion needs a loadable combat BT/BB")

dash_class = load(ROOT + "/GameplayAbility/GA_RaidSwordManVariant_Dash", unreal.Blueprint).generated_class()
before_grants = [*base.original_grants, base.attack_class, dash_class]
before_patterns = [base.original_patterns[0],
                   [base.attack_class, base.original_patterns[1][0], dash_class, *base.original_patterns[1][1:]]]
attack = load(PATH, unreal.Blueprint) if unreal.EditorAssetLibrary.does_asset_exist(PATH) else None
summon_class = attack.generated_class() if attack else None
grants = list(startup.get_editor_property("NPCStartUpAbilities"))
phases = list(definition.get_editor_property("phases"))
check(len(phases) == 2 and len(base.original_patterns[1]) == 2, "Unexpected variant phase layout")
check(grants == before_grants or (summon_class and grants == [*before_grants, summon_class]), "Custom grants; refusing overwrite")
for index, phase in enumerate(phases):
    current = list(phase.get_editor_property("ability_patterns"))
    after = [summon_class, *before_patterns[index]] if index == 1 else before_patterns[index]
    check(current == before_patterns[index] or (summon_class and current == after), "Custom phase priority; refusing overwrite")
styles = list(base.component.get_editor_property("styles"))
summon_styles = [style for style in styles if base.tag_name(style.get_editor_property("action_tag")) == ACTION]
check(len(summon_styles) <= 1, "Duplicate summon warning")
for style in summon_styles:
    check(style.get_editor_property("radius") == 450.0 and
          style.get_editor_property("material").get_path_name() == base.material.get_path_name(), "Custom summon warning")
action, cooldown = base.tag(ACTION), base.tag(COOLDOWN)


def verify_attack(bp):
    check(bp.get_blueprint_parent_class() == unreal.RPGBossSummonAbility.static_class(), "Wrong summon parent")
    check("ERROR" not in str(bp.get_editor_property("status")).upper(), "Summon BP compile error")
    cdo = default(bp)
    for field, value in TUNING.items():
        check(cdo.get_editor_property(field) == value, "Custom summon tuning: " + field)
    check(cdo.get_editor_property("summon_class") == minion_class and
          [offset.export_text() for offset in cdo.get_editor_property("local_spawn_offsets")] ==
          [offset.export_text() for offset in OFFSETS], "Wrong minion/spawn positions")
    check(base.tag_name(cdo.get_editor_property("action_tag")) == ACTION and
          base.tag_name(cdo.get_editor_property("PatternCooldownTag")) == COOLDOWN and
          cdo.get_editor_property("cooldown_gameplay_effect_class") is None, "Wrong summon tags/cooldown")


if attack:
    verify_attack(attack)
elif APPLY:
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", unreal.RPGBossSummonAbility.static_class())
    folder, name = PATH.rsplit("/", 1)
    attack = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.Blueprint, factory)
    check(attack, "Could not create summon ability")
    cdo = default(attack)
    for field, value in TUNING.items():
        cdo.set_editor_property(field, value)
    cdo.set_editor_property("summon_class", minion_class)
    cdo.set_editor_property("local_spawn_offsets", OFFSETS)
    cdo.set_editor_property("action_tag", action)
    cdo.set_editor_property("PatternCooldownTag", cooldown)
    base.compile_bp(attack)
    verify_attack(attack)
else:
    raise RuntimeError("Summon is absent; use -RPGApplySwordManVariantSummon after backing up the variant")

summon_class = attack.generated_class()
desired_grants = [*before_grants, summon_class]
desired_patterns = [before_patterns[0], [summon_class, *before_patterns[1]]]
dirty = []
if APPLY:
    if grants != desired_grants:
        startup.modify()
        startup.set_editor_property("NPCStartUpAbilities", desired_grants)
        dirty.append(startup)
    if any(list(phase.get_editor_property("ability_patterns")) != desired for phase, desired in zip(phases, desired_patterns)):
        definition.modify()
        for phase, desired in zip(phases, desired_patterns):
            phase.set_editor_property("ability_patterns", desired)
        definition.set_editor_property("phases", phases)
        dirty.append(definition)
    if not summon_styles:
        style = unreal.RPGBossTelegraphStyle()
        style.set_editor_property("action_tag", action)
        style.set_editor_property("radius", 450.0)  # Non-damaging cast indicator, not a hit radius.
        style.set_editor_property("material", base.material)
        style.set_editor_property("color", unreal.LinearColor(.3, .5, 1.0, 1.0))
        base.component.modify()
        base.component.set_editor_property("styles", [*styles, style])
        base.compile_bp(boss)
        dirty.append(boss)

verify_attack(attack)
check(list(startup.get_editor_property("NPCStartUpAbilities")) == desired_grants, "Summon not granted")
check(all(list(phase.get_editor_property("ability_patterns")) == desired for phase, desired in
          zip(definition.get_editor_property("phases"), desired_patterns)), "Summon not selected in phase two")
check(sum(base.tag_name(style.get_editor_property("action_tag")) == ACTION for style in
          default(boss).get_editor_property("boss_telegraph_presentation").get_editor_property("styles")) == 1, "Missing summon warning")
if APPLY:
    base.save(attack)
    for asset in dirty:
        base.save(asset)
else:
    import_local("ConfigureRaidSwordManVariantDash")
unreal.log("RAID_SWORDMAN_VARIANT_SUMMON COMPLETE Phase2=1 Guardians=2 Lifetime=25 Cooldown=30 NavigationRequired=1 Applied=" + str(int(APPLY)))
