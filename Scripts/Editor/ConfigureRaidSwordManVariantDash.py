"""Opt-in phase-two dash content for the isolated SwordMan; no native changes.

Back up variant boss/startup/definition/tree before -RPGApplySwordManVariantDash.
Default: verify only. Existing unknown graphs, grants or tuning are rejected.
The 400..800cm opener falls back to the original chase when GAS refuses it.
Distance gates have no abort observer: crossing 400cm must not cancel movement.
-RPGApplySwordManVariantPhaseTagGate migrates only the existing BT phase gate.
-RPGApplySwordManDashActivationRange applies only the dash ability's existing
400..800cm entry envelope to GAS activation as well. Back up the dash BP first.
"""
import importlib.util
import os
import unreal


def import_local(name):
    spec = importlib.util.spec_from_file_location(name, os.path.join(os.path.dirname(__file__), name + ".py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


# Reuse the existing hazard contract and common BT validation, not new tests.
hazard = import_local("ConfigureRaidSwordManVariantHazard")
bt = import_local("CreateRaidBossBTTemplate")
check, load, default = hazard.check, hazard.load, hazard.default
ROOT = hazard.ROOT
PATH = ROOT + "/GameplayAbility/GA_RaidSwordManVariant_Dash"
ACTION = "Boss.Action.SwordMan.Dash"
COOLDOWN = "Boss.Cooldown.SwordMan.Dash"
APPLY = "-rpgapplyswordmanvariantdash" in unreal.SystemLibrary.get_command_line().lower()
APPLY_PHASE_GATE = "-rpgapplyswordmanvariantphasetaggate" in unreal.SystemLibrary.get_command_line().lower()
APPLY_ACTIVATION_RANGE = "-rpgapplyswordmandashactivationrange" in unreal.SystemLibrary.get_command_line().lower()
TUNING = {"windup_seconds": 1.2, "maximum_distance": 800.0, "dash_duration": .6,
          "hit_radius": 120.0, "PatternCooldownSeconds": 14.0}
ACTIVATION_RANGE = (400.0, TUNING["maximum_distance"])
SECURITY = unreal.RPGSkillSecurityProfile(maximum_server_hit_distance=1000.0,
    hit_location_tolerance=80.0, maximum_targets_per_query=16,
    maximum_hits_per_activation=1, maximum_damage_per_hit=1000.0)
tree = load(ROOT + "/BT_RaidSwordManVariant", unreal.BehaviorTree)
blackboard = load(ROOT + "/BB_RaidSwordManVariant", unreal.BlackboardData)
boss, definition, startup = hazard.boss, hazard.definition, hazard.startup
action, cooldown = hazard.tag(ACTION), hazard.tag(COOLDOWN)
attack = load(PATH, unreal.Blueprint) if unreal.EditorAssetLibrary.does_asset_exist(PATH) else None
combat = list(tree.get_editor_property("root_node").get_editor_property("children"))[0].get_editor_property("child_composite")
combat_entries = list(combat.get_editor_property("children"))
has_opener = bool(combat_entries[0].get_editor_property("child_composite"))
check(not has_opener or attack, "Unknown movement branch; refusing overwrite")
bt.verify(tree, blackboard, dash_class=attack.generated_class() if has_opener else None)
before_grants = [*hazard.original_grants, hazard.attack_class]
before_patterns = [hazard.original_patterns[0], [hazard.attack_class, *hazard.original_patterns[1]]]
check(len(before_patterns[1]) == 3, "Expected authored hazard/heavy/strike phase")
grants = list(startup.get_editor_property("NPCStartUpAbilities"))
current_phases = list(definition.get_editor_property("phases"))
if not attack:
    check(grants == before_grants and all(list(phase.get_editor_property("ability_patterns")) == expected
          for phase, expected in zip(current_phases, before_patterns)), "Custom variant patterns")


def verify_attack(bp):
    check(bp.get_blueprint_parent_class() == unreal.RPGBossDashAbility.static_class(), "Wrong dash parent")
    check("ERROR" not in str(bp.get_editor_property("status")).upper(), "Dash BP compile error")
    cdo = default(bp)
    for field, expected in TUNING.items():
        check(abs(cdo.get_editor_property(field) - expected) < .0001, "Custom dash tuning: " + field)
    check(hazard.tag_name(cdo.get_editor_property("action_tag")) == ACTION and
          hazard.tag_name(cdo.get_editor_property("PatternCooldownTag")) == COOLDOWN, "Wrong dash tags")
    check(cdo.get_editor_property("damage_effect_class") == hazard.damage_class and
          cdo.get_editor_property("cooldown_gameplay_effect_class") is None, "Wrong dash effect")
    check(cdo.get_editor_property("base_damage").export_text() == unreal.ScalableFloat(value=25.0).export_text(), "Custom dash damage")
    check(cdo.get_editor_property("security_profile").export_text() == SECURITY.export_text(), "Custom dash security")
    activation_range = (cdo.get_editor_property("MinimumActivationDistance"), cdo.get_editor_property("MaximumActivationDistance"))
    check(activation_range in ((0.0, 0.0), ACTIVATION_RANGE), "Custom dash activation range; refusing overwrite")


styles = list(hazard.component.get_editor_property("styles"))
dash_styles = [style for style in styles if hazard.tag_name(style.get_editor_property("action_tag")) == ACTION]
check(len(dash_styles) <= 1, "Duplicate dash style")
for style in dash_styles:
    check(style.get_editor_property("radius") == TUNING["hit_radius"] and
          style.get_editor_property("material").get_path_name() == hazard.material.get_path_name(), "Custom dash style")
if attack:
    verify_attack(attack)
elif APPLY:
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", unreal.RPGBossDashAbility.static_class())
    folder, name = PATH.rsplit("/", 1)
    attack = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.Blueprint, factory)
    check(attack, "Could not create dash ability")
    cdo = default(attack)
    for field, value in TUNING.items():
        cdo.set_editor_property(field, value)
    cdo.set_editor_property("action_tag", action)
    cdo.set_editor_property("PatternCooldownTag", cooldown)
    cdo.set_editor_property("damage_effect_class", hazard.damage_class)
    cdo.set_editor_property("base_damage", unreal.ScalableFloat(value=25.0))
    cdo.set_editor_property("security_profile", SECURITY)
    cdo.set_editor_property("MinimumActivationDistance", ACTIVATION_RANGE[0])
    cdo.set_editor_property("MaximumActivationDistance", ACTIVATION_RANGE[1])
    hazard.compile_bp(attack)
    verify_attack(attack)
    hazard.save(attack)
else:
    raise RuntimeError("Dash is absent; back up the variant and use -RPGApplySwordManVariantDash")

dash_class = attack.generated_class()
desired_grants = [*before_grants, dash_class]
desired_patterns = [before_patterns[0], [*before_patterns[1][:2], dash_class, *before_patterns[1][2:]]]
if hazard.summon_class and hazard.summon_class in grants:
    desired_grants.append(hazard.summon_class)
    desired_patterns[1].insert(0, hazard.summon_class)
check(grants in (before_grants, desired_grants), "Custom grants; refusing overwrite")
for phase, before, desired in zip(current_phases, before_patterns, desired_patterns):
    check(list(phase.get_editor_property("ability_patterns")) in (before, desired), "Custom phase patterns; refusing overwrite")

if APPLY:
    if not has_opener:
        tree.modify()
        movement = bt.node(tree, unreal.BTComposite_Selector, "DashOrChase")
        opener = bt.node(tree, unreal.BTComposite_Sequence, "PhaseTwoMidrangeDash")
        rotate = bt.node(tree, bt.native("BTTask_RotateToFaceTarget", "Project_RPG"), "FaceDashTarget")
        rotate.set_editor_property("InTargetToFaceKey", bt.selector("TargetActor", bt.native("BlackboardKeyType_Object")))
        execute = bt.node(tree, bt.native("BTTask_RPGBossAbility", "Project_RPG"), "ExecuteDashOpener")
        execute.set_editor_property("bUsePhasePatterns", False)
        execute.set_editor_property("AbilityClass", dash_class)
        execute.set_editor_property("TimeoutSeconds", 5.0)
        opener.set_editor_property("children", [bt.child(rotate), bt.child(execute)])
        phase_gate = bt.node(tree, bt.native("BTDecorator_RPGBossPhase", "Project_RPG"), "DashPhaseTwo")
        phase_gate.set_editor_property("required_phase", current_phases[1].get_editor_property("phase_tag"))
        phase_gate.set_editor_property("bExactMatch", True)
        phase_gate.set_editor_property("flow_abort_mode", unreal.BTFlowAbortMode.NONE)
        gates = [phase_gate]
        for name, radius, inverse in (("BeyondMeleeRange", 400.0, True), ("WithinDashRange", 800.0, False)):
            gate = bt.node(tree, bt.native("BTDecorator_IsAtLocation"), name)
            gate.set_editor_property("blackboard_key", bt.selector("TargetActor", bt.native("BlackboardKeyType_Object")))
            gate.set_editor_property("acceptable_radius", radius)
            # bUseParametrizedRadius is not Python-readable/editable. Keep the
            # native constructor's false default, using the editable radius.
            gate.set_editor_property("bInverseCondition", inverse)
            bt.scalar(gate, "bPathFindingBasedTest", False)
            gate.set_editor_property("geometric_distance_type", bt.distance_2d(gate))
            gate.set_editor_property("flow_abort_mode", unreal.BTFlowAbortMode.NONE)
            gates.append(gate)
        movement.set_editor_property("children", [bt.child(opener, gates), combat_entries[0]])
        combat_entries[0] = bt.child(movement)
        combat.modify()
        combat.set_editor_property("children", combat_entries)
        bt.verify(tree, blackboard, dash_class=dash_class)
        hazard.save(tree)
    if grants != desired_grants:
        startup.modify()
        startup.set_editor_property("NPCStartUpAbilities", desired_grants)
        hazard.save(startup)
    if any(list(phase.get_editor_property("ability_patterns")) != desired for phase, desired in zip(current_phases, desired_patterns)):
        definition.modify()
        for phase, desired in zip(current_phases, desired_patterns):
            phase.set_editor_property("ability_patterns", desired)
        definition.set_editor_property("phases", current_phases)
        hazard.save(definition)
    if not dash_styles:
        style = unreal.RPGBossTelegraphStyle()
        style.set_editor_property("action_tag", action)
        style.set_editor_property("radius", TUNING["hit_radius"])
        style.set_editor_property("material", hazard.material)
        style.set_editor_property("color", unreal.LinearColor(1.0, .5, .05, 1.0))
        hazard.component.modify()
        hazard.component.set_editor_property("styles", [*styles, style])
        hazard.compile_bp(boss)
        hazard.save(boss)

verify_attack(attack)
bt.verify(tree, blackboard, dash_class=dash_class)
check(list(startup.get_editor_property("NPCStartUpAbilities")) == desired_grants, "Dash not granted")
check(all(list(phase.get_editor_property("ability_patterns")) == desired for phase, desired in
          zip(definition.get_editor_property("phases"), desired_patterns)), "Dash phase priority differs")
check(sum(hazard.tag_name(style.get_editor_property("action_tag")) == ACTION for style in
          default(boss).get_editor_property("boss_telegraph_presentation").get_editor_property("styles")) == 1, "Missing dash style")
if not APPLY:
    import_local("CreateRaidSwordManVariant")
if APPLY_PHASE_GATE:
    movement = list(combat.get_editor_property("children"))[0].get_editor_property("child_composite")
    check(movement, "Phase-tag migration requires the existing dash opener")
    choices = list(movement.get_editor_property("children"))
    opener = choices[0]
    gates = list(opener.get_editor_property("decorators"))
    old = gates[0]
    phase_path = "/Script/Project_RPG.BTDecorator_RPGBossPhase"
    gate_changed = old.get_class().get_path_name() != phase_path
    if gate_changed:
        check(old.get_outer() == tree and not any(graph.get_outer() == tree for graph in unreal.ObjectIterator(unreal.EdGraph)),
              "Refusing a shared phase gate or a tree with an editor graph")
        replacement = bt.node(tree, bt.native("BTDecorator_RPGBossPhase", "Project_RPG"), "DashPhaseTwoTag")
        replacement.set_editor_property("required_phase", current_phases[1].get_editor_property("phase_tag"))
        replacement.set_editor_property("bExactMatch", True)
        replacement.set_editor_property("flow_abort_mode", old.get_editor_property("flow_abort_mode"))
        gates[0] = replacement
        opener.set_editor_property("decorators", gates)
        choices[0] = opener
        tree.modify()
        movement.modify()
        movement.set_editor_property("children", choices)
        bt.verify(tree, blackboard, dash_class=dash_class)
    # Keep the dependent summon preset's source interpretation valid without saving it.
    import_local("CreateRaidSwordManSummonMechanic")
    if gate_changed:
        hazard.save(tree)
    unreal.log("RAID_VARIANT_PHASE_TAG_GATE COMPLETE OnlyVariantBTSaved=1")
if APPLY_ACTIVATION_RANGE:
    cdo = default(attack)
    current_range = (cdo.get_editor_property("MinimumActivationDistance"), cdo.get_editor_property("MaximumActivationDistance"))
    if current_range != ACTIVATION_RANGE:
        # verify_attack() rejected unknown tuning before this point. Keep BT
        # gates and hit security unchanged; only gate selection/start in GAS.
        attack.modify()
        cdo.modify()
        cdo.set_editor_property("MinimumActivationDistance", ACTIVATION_RANGE[0])
        cdo.set_editor_property("MaximumActivationDistance", ACTIVATION_RANGE[1])
        hazard.compile_bp(attack)
        verify_attack(attack)
        updated = default(attack)
        check((updated.get_editor_property("MinimumActivationDistance"), updated.get_editor_property("MaximumActivationDistance")) == ACTIVATION_RANGE,
              "Dash activation range was not retained after compilation")
        hazard.save(attack)
    unreal.log("RAID_DASH_ACTIVATION_RANGE COMPLETE Min=400 Max=800 OnlyDashBPSaved=1")
unreal.log("RAID_SWORDMAN_VARIANT_DASH COMPLETE Phase2=1 Midrange=400..800 ChaseFallback=1 GASCooldown=14 Applied=" + str(int(APPLY)))
