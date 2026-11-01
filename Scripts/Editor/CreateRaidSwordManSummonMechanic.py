"""Create an isolated, playable summon-clear preset from the existing variant.

Run after building the latest native boss code. Creation is opt-in with
-RPGCreateSwordManSummonMechanic; an existing bundle is only inspected, never
rewritten, except for the explicit -RPGApplySummonMechanicPhaseTagGate migration
of the existing preset's single BT phase gate. -RPGSelectSwordManSummonEncounter
selects the verified, existing preset in BP_BossBattleGameMode, saving only that
GameMode. Back up the affected asset before applying either option. Maps are
never saved by this script.
"""
import unreal

SOURCE = "/Game/Blueprints/Character/NPC/Raid/SwordManVariant"
ROOT = "/Game/Blueprints/Character/NPC/Raid/SwordManSummonMechanic"
APPLY = "-rpgcreateswordmansummonmechanic" in unreal.SystemLibrary.get_command_line().lower()
APPLY_PHASE_GATE = "-rpgapplysummonmechanicphasetaggate" in unreal.SystemLibrary.get_command_line().lower()
SELECT_ENCOUNTER = "-rpgselectswordmansummonencounter" in unreal.SystemLibrary.get_command_line().lower().split()
GAME_MODE = "/Game/Blueprints/GameMode/BP_BossBattleGameMode"
BASE_BOSS = "/Game/Blueprints/Character/NPC/SwordMan/BP_RaidSwordMan"
check_options = int(APPLY) + int(APPLY_PHASE_GATE) + int(SELECT_ENCOUNTER)
if check_options > 1:
    raise RuntimeError("Choose only one create, phase-gate migration or encounter-selection option")
PHASE_GATE_PATH = "/Script/Project_RPG.BTDecorator_RPGBossPhase"
MECHANIC_TAG = "Boss.Phase.SwordMan.SummonMechanic"
SOURCES = {
    "summon": SOURCE + "/GameplayAbility/GA_RaidSwordManVariant_Summon",
    "definition": SOURCE + "/DA_RaidSwordManVariant",
    "startup": SOURCE + "/DA_RaidSwordManVariantStartUp",
    "tree": SOURCE + "/BT_RaidSwordManVariant",
    "controller": SOURCE + "/AIC_RaidSwordManVariant",
    "boss": SOURCE + "/BP_RaidSwordManVariant",
}
NAMES = {
    "summon": "GA_RaidSwordManSummonMechanic",
    "definition": "DA_RaidSwordManSummonMechanic",
    "startup": "DA_RaidSwordManSummonMechanicStartUp",
    "tree": "BT_RaidSwordManSummonMechanic",
    "controller": "AIC_RaidSwordManSummonMechanic",
    "boss": "BP_RaidSwordManSummonMechanic",
}


def check(value, message):
    if not value:
        raise RuntimeError(message)


def load(path):
    asset = unreal.load_asset(path)
    check(asset, "Missing asset: " + path)
    return asset


def default(bp):
    return unreal.get_default_object(bp.generated_class())


def compile_bp(bp):
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    check("ERROR" not in str(bp.get_editor_property("status")).upper(), "BP compile failed: " + bp.get_path_name())


def tag(name):
    value = unreal.GameplayTag()
    check(value.import_text('(TagName="{}")'.format(name)), "Missing registered tag: " + name)
    return value


def copy_phase(source):
    value = unreal.RPGBossPhaseDefinition()
    check(value.import_text(source.export_text()), "Could not copy phase data")
    return value


def dash_phase_gate(tree):
    # Legacy index gates need rebasing; phase-tag gates survive inserting a mechanic phase.
    combat = list(tree.get_editor_property("root_node").get_editor_property("children"))[0].get_editor_property("child_composite")
    movement = list(combat.get_editor_property("children"))[0].get_editor_property("child_composite")
    check(movement, "Expected the variant dash-or-chase selector")
    opener = list(movement.get_editor_property("children"))[0]
    gates = [gate for gate in opener.get_editor_property("decorators")
             if gate.get_class().get_path_name() == PHASE_GATE_PATH or
             (isinstance(gate, unreal.BTDecorator_Blackboard) and
              str(gate.get_editor_property("blackboard_key").get_editor_property("selected_key_name")) == "BossPhaseIndex")]
    check(len(gates) == 1, "Expected one dash phase gate")
    if isinstance(gates[0], unreal.BTDecorator_Blackboard):
        check(gates[0].get_editor_property("arithmetic_operation") == unreal.ArithmeticKeyOperation.EQUAL,
              "Unexpected dash phase comparison")
    check(gates[0].get_outer() == tree, "Refusing to edit a shared BT decorator")
    return gates[0]


def check_phase_gate(gate, required_phase, legacy_index):
    check(not gate.get_editor_property("bInverseCondition") and
          gate.get_editor_property("flow_abort_mode") == unreal.BTFlowAbortMode.NONE,
          "Unexpected dash phase abort/inversion")
    if gate.get_class().get_path_name() == PHASE_GATE_PATH:
        check(gate.get_editor_property("bExactMatch") and
              gate.get_editor_property("required_phase").export_text() == required_phase.export_text(),
              "Unexpected dash phase tag")
    else:
        check(gate.get_editor_property("int_value") == legacy_index, "Unexpected dash phase index")


def has_editor_graph(tree):
    # BTGraph is protected in Python. Its loaded, directly owned graph objects
    # are visible through the public object iterator without exposing that property.
    return any(graph.get_outer() == tree for graph in unreal.ObjectIterator(unreal.EdGraph))


def migrate_phase_gate(tree, required_phase):
    old = dash_phase_gate(tree)
    if old.get_class().get_path_name() == PHASE_GATE_PATH:
        return  # Already migrated; verify() checks its authored tag and abort mode.
    check(tree.get_path_name().startswith(ROOT + "/"), "Refusing to edit a source tree")
    check(not has_editor_graph(tree),
          "Tree has an editor graph; migrate its decorator in the editor instead of leaving a stale graph")
    check(old.get_editor_property("int_value") == 2 and not old.get_editor_property("bInverseCondition") and
          old.get_editor_property("flow_abort_mode") == unreal.BTFlowAbortMode.NONE,
          "Unexpected legacy phase/abort configuration; refusing overwrite")
    phase_class = unreal.load_class(None, PHASE_GATE_PATH)
    check(phase_class, "Build the native boss phase decorator before applying")
    combat = list(tree.get_editor_property("root_node").get_editor_property("children"))[0].get_editor_property("child_composite")
    movement = list(combat.get_editor_property("children"))[0].get_editor_property("child_composite")
    choices = list(movement.get_editor_property("children"))
    opener = choices[0]
    decorators = list(opener.get_editor_property("decorators"))
    replacement = unreal.new_object(phase_class, outer=tree, name="SummonResumePhaseTagGate")
    replacement.set_editor_property("required_phase", required_phase)
    replacement.set_editor_property("bExactMatch", True)
    # The dash keeps running when it crosses its entry distance. Preserve the old abort policy.
    replacement.set_editor_property("flow_abort_mode", old.get_editor_property("flow_abort_mode"))
    decorators[decorators.index(old)] = replacement
    tree.modify()
    movement.modify()
    opener.set_editor_property("decorators", decorators)
    choices[0] = opener
    movement.set_editor_property("children", choices)
    check(dash_phase_gate(tree) == replacement, "Phase gate replacement was not retained")


sources = {key: load(path) for key, path in SOURCES.items()}
check(isinstance(sources["definition"], unreal.RPGBossDefinition), "Expected boss definition")
check(isinstance(default(sources["summon"]), unreal.RPGBossSummonAbility), "Expected native summon ability")
source_phases = list(sources["definition"].get_editor_property("phases"))
check(len(source_phases) == 2 and source_phases[0].get_editor_property("health_threshold") == 1.0 and
      abs(source_phases[1].get_editor_property("health_threshold") - .7) < .0001,
      "Preset requires the existing 100%/70% variant")
try:
    for phase in source_phases:
        check(not phase.get_editor_property("bRequiresExplicitTransition") and
              not phase.get_editor_property("bSuspendCombatAI") and not phase.get_editor_property("bInvulnerable") and
              phase.get_editor_property("TimeLimitSeconds") == 0.0 and
              phase.get_editor_property("PhaseEntryAbility") is None and
              str(phase.get_editor_property("SummonWaveCompletionPhase").get_editor_property("TagName")) == "None",
              "Source has custom mechanic settings; refusing to reinterpret them")
    default(sources["summon"]).get_editor_property("bRequireThreatTarget")
except Exception as error:
    raise RuntimeError("Build/reload the latest native boss code and use an unmodified combat variant: " + str(error)) from error

source_boss = default(sources["boss"])
check(source_boss.get_editor_property("character_start_up_data") == sources["startup"] and
      source_boss.get_editor_property("boss_ai_component").get_editor_property("definition") == sources["definition"] and
      source_boss.get_editor_property("ai_controller_class") == sources["controller"].generated_class() and
      default(sources["controller"]).get_editor_property("BTAsset") == sources["tree"], "Source bundle is not connected")
check_phase_gate(dash_phase_gate(sources["tree"]), source_phases[1].get_editor_property("phase_tag"), 1)
source_summon_class = sources["summon"].generated_class()
source_summon = default(sources["summon"])
minion_class = source_summon.get_editor_property("summon_class")
check(minion_class, "Source summon has no NPC class")
minion = unreal.get_default_object(minion_class)
check(isinstance(minion, unreal.RPGNonPlayerCharacter) and not isinstance(minion, unreal.RPGBossCharacter) and
      minion.get_editor_property("replicates"), "Summon requires a replicated ordinary NPC")
minion_controller_class = minion.get_editor_property("ai_controller_class")
check(minion_controller_class, "Summon NPC has no AIController")
minion_controller = unreal.get_default_object(minion_controller_class)
check(isinstance(minion_controller, unreal.AIController), "Summon requires a native AIController base")
minion_tree = minion_controller.get_editor_property("BTAsset")
check(isinstance(minion_tree, unreal.BehaviorTree) and minion_tree.get_editor_property("root_node") and
      minion_tree.get_editor_property("blackboard_asset"), "Summon NPC has no loadable combat BT/Blackboard")
unreal.log("RAID_SUMMON_MECHANIC_MINION_BT=" + minion_tree.get_path_name())
check(len(source_summon.get_editor_property("local_spawn_offsets")) == 2 and
      source_summon.get_editor_property("maximum_owned_summons") == 2 and
      source_summon.get_editor_property("startup_timeout") <= 10.0,
      "Preset requires the authored two-member summon with startup timeout at most 10 seconds")
source_grants = list(sources["startup"].get_editor_property("NPCStartUpAbilities"))
check(source_summon_class in source_grants and all(pattern in source_grants for phase in source_phases
      for pattern in phase.get_editor_property("ability_patterns")), "Source patterns must already be granted")
mechanic_tag = tag(MECHANIC_TAG)


def desired_phases(summon_class):
    initial = copy_phase(source_phases[0])
    resume = copy_phase(source_phases[1])
    # This threshold only preserves strict ordering; explicit completion enters
    # the resume phase immediately, without waiting for 69% HP.
    resume.set_editor_property("health_threshold", .69)
    resume.set_editor_property("bRequiresExplicitTransition", True)
    resume.set_editor_property("ability_patterns", [pattern for pattern in resume.get_editor_property("ability_patterns")
                                                   if pattern != source_summon_class])
    mechanic = unreal.RPGBossPhaseDefinition()
    for field, value in {"phase_tag": mechanic_tag, "health_threshold": .7,
                         "bInvulnerable": True, "bSuspendCombatAI": True, "TimeLimitSeconds": 30.0,
                         "PhaseEntryAbility": summon_class,
                         "SummonWaveCompletionPhase": resume.get_editor_property("phase_tag")}.items():
        mechanic.set_editor_property(field, value)
    return [initial, mechanic, resume]


def verify(bundle):
    summon_class = bundle["summon"].generated_class()
    summon = default(bundle["summon"])
    for field, value in {"bRequireThreatTarget": False, "PatternCooldownSeconds": 0.0,
                         "summon_lifetime": 45.0, "windup_seconds": 2.0}.items():
        check(summon.get_editor_property(field) == value, "Wrong mechanic summon tuning: " + field)
    check(summon.get_editor_property("cooldown_gameplay_effect_class") is None, "Mechanic has an authored cooldown")
    for field in ("summon_class", "local_spawn_offsets", "startup_timeout", "maximum_owned_summons", "bRequireNavigation", "action_tag"):
        actual = summon.get_editor_property(field)
        expected = default(sources["summon"]).get_editor_property(field)
        # Struct arrays compare via their reflected text, not Python wrapper identity.
        if field == "local_spawn_offsets":
            actual, expected = [item.export_text() for item in actual], [item.export_text() for item in expected]
        elif field == "action_tag":
            actual, expected = actual.export_text(), expected.export_text()
        check(actual == expected, "Changed summon contract: " + field)
    actual_phases = list(bundle["definition"].get_editor_property("phases"))
    check([phase.export_text() for phase in actual_phases] ==
          [phase.export_text() for phase in desired_phases(summon_class)], "Unexpected mechanic phases")
    check(list(bundle["startup"].get_editor_property("NPCStartUpAbilities")) == [*source_grants, summon_class], "Wrong mechanic grants")
    for field in ("ActivateOnGivenAbilities", "ReactiveAbilities", "StartUpGameplayEffects"):
        check(list(bundle["startup"].get_editor_property(field)) == list(sources["startup"].get_editor_property(field)), "Changed startup effects/policy")
    for field in ("enrage_seconds", "enraged_attack_multiplier"):
        check(bundle["definition"].get_editor_property(field) == sources["definition"].get_editor_property(field), "Changed enrage tuning")
    boss = default(bundle["boss"])
    check(boss.get_editor_property("character_start_up_data") == bundle["startup"] and
          boss.get_editor_property("boss_ai_component").get_editor_property("definition") == bundle["definition"] and
          boss.get_editor_property("ai_controller_class") == bundle["controller"].generated_class() and
          default(bundle["controller"]).get_editor_property("BTAsset") == bundle["tree"], "Mechanic bundle is not connected")
    check(boss.get_editor_property("bStartEncounterOnReady") == source_boss.get_editor_property("bStartEncounterOnReady"), "Changed encounter startup policy")
    check(boss.get_editor_property("boss_summon_objective_component"), "Native objective component is missing")
    check(bundle["tree"].get_editor_property("blackboard_asset") == sources["tree"].get_editor_property("blackboard_asset"),
          "Wrong resumed combat blackboard")
    gate = dash_phase_gate(bundle["tree"])
    check_phase_gate(gate, source_phases[1].get_editor_property("phase_tag"), 2)


exists = [unreal.EditorAssetLibrary.does_asset_exist(ROOT + "/" + name) for name in NAMES.values()]
check(all(exists) or not any(exists), "Partial mechanic bundle exists; inspect it manually, nothing will be overwritten")
if all(exists):
    bundle = {key: load(ROOT + "/" + name) for key, name in NAMES.items()}
    verify(bundle)
    unreal.log("RAID_SUMMON_MECHANIC_BT_GRAPH_PRESENT=" + str(int(has_editor_graph(bundle["tree"]))))
    if APPLY_PHASE_GATE:
        before = dash_phase_gate(bundle["tree"])
        migrate_phase_gate(bundle["tree"], source_phases[1].get_editor_property("phase_tag"))
        verify(bundle)
        if dash_phase_gate(bundle["tree"]) != before:
            check(unreal.EditorAssetLibrary.save_loaded_asset(bundle["tree"], only_if_is_dirty=False), "Could not save migrated BT")
        unreal.log("RAID_SUMMON_MECHANIC_PHASE_TAG_GATE READY OnlyPresetBTSaved=1")
elif APPLY:
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    bundle = {key: tools.duplicate_asset(NAMES[key], ROOT, source) for key, source in sources.items()}
    check(all(bundle.values()), "Could not duplicate mechanic bundle; inspect partial assets before retrying")
    summon = default(bundle["summon"])
    for field, value in {"bRequireThreatTarget": False, "PatternCooldownSeconds": 0.0,
                         "cooldown_gameplay_effect_class": None, "summon_lifetime": 45.0, "windup_seconds": 2.0}.items():
        summon.set_editor_property(field, value)
    compile_bp(bundle["summon"])
    summon_class = bundle["summon"].generated_class()
    bundle["definition"].set_editor_property("phases", desired_phases(summon_class))
    bundle["startup"].set_editor_property("NPCStartUpAbilities", [*source_grants, summon_class])
    gate = dash_phase_gate(bundle["tree"])
    if gate.get_class().get_path_name() != PHASE_GATE_PATH:
        gate.modify()
        gate.set_editor_property("int_value", 2)
    default(bundle["controller"]).set_editor_property("BTAsset", bundle["tree"])
    compile_bp(bundle["controller"])
    boss = default(bundle["boss"])
    boss.set_editor_property("ai_controller_class", bundle["controller"].generated_class())
    boss.set_editor_property("character_start_up_data", bundle["startup"])
    component = boss.get_editor_property("boss_ai_component")
    check(component.get_outer() == boss, "Refusing to edit a shared source component")
    component.modify()
    component.set_editor_property("definition", bundle["definition"])
    compile_bp(bundle["boss"])
    verify(bundle)
    for asset in bundle.values():
        check(unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False), "Could not save " + asset.get_path_name())
else:
    raise RuntimeError("Mechanic preset is absent; build native code, then use -RPGCreateSwordManSummonMechanic to create only the new bundle")

if SELECT_ENCOUNTER:
    mode = load(GAME_MODE)
    check(isinstance(mode, unreal.Blueprint), "Expected the authored boss battle GameMode Blueprint")
    mode_default = default(mode)
    check(isinstance(mode_default, unreal.RPGBossBattleGameMode), "Unexpected boss battle GameMode parent")
    check(mode_default.get_editor_property("game_state_class") == unreal.RPGBossBattleGameState.static_class(),
          "Boss battle requires the native replicated GameState")
    selected = bundle["boss"].generated_class()
    current = mode_default.get_editor_property("boss_class")
    check(current in (load(BASE_BOSS).generated_class(), selected),
          "Custom encounter selection; refusing to overwrite it")
    if current != selected:
        mode.modify()
        mode_default.set_editor_property("boss_class", selected)
        compile_bp(mode)
        check(default(mode).get_editor_property("boss_class") == selected, "Boss selection was lost during compile")
        check(unreal.EditorAssetLibrary.save_loaded_asset(mode, only_if_is_dirty=False), "Could not save boss battle GameMode")
    unreal.log("RAID_SUMMON_ENCOUNTER_SELECTED Class={} OnlyGameModeSaved=1".format(selected.get_path_name()))

unreal.log("RAID_SWORDMAN_SUMMON_MECHANIC READY Phases=3 TriggerHP=70 Guardians=2 Deadline=30 TargetIndependent=1 MapsUnchanged=1")
