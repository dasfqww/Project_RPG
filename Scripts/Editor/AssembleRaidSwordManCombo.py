"""Assemble a server GAS/BT boss using compatible BossyEnemy sword attacks.

Run main(apply=True) through the editor's Python console/MCP Slate tools.
Default main() verifies only. Creates only NPC/Raid/SwordManCombo assets; an
existing bundle is never rebuilt. Back up BP_BossBattleGameMode before select().
Original animation libraries, player assets and native code are never saved.
Generated runtime nodes are saved before opening a graph. For first graph
creation, use the editor's select-all/copy ownership reset before saving, then
reload and verify: otherwise generated child nodes can retain transient flags.
"""
import unreal

ROOT = "/Game/Blueprints/Character/NPC/Raid/SwordManCombo"
SOURCE = "/Game/Blueprints/Character/NPC/Raid/SwordManSummonMechanic"
BASE = "/Game/Blueprints/Character/NPC/SwordMan"
VARIANT = "/Game/Blueprints/Character/NPC/Raid/SwordManVariant"
MODE = "/Game/Blueprints/GameMode/BP_BossBattleGameMode"
ANIMATIONS = "/Game/Assets/BossyEnemy/Animations/RootMotion/Attacks/"
# Source hit times for Uppercut/SwingAndSlam match the existing raid presentation.
# Swing timing is an initial authoring value and needs visual tuning.
ATTACKS = (("Boss_Attack_Swing_RM", 2.05, 1.2),
           ("Boss_Attack_Uppercut_RM", 2.896176, 1.4),
           ("Boss_Attack_SwingAndSlam_RM", 1.644185, 1.6))
NAMES = {"boss": "BP_RaidSwordManCombo", "controller": "AIC_RaidSwordManCombo",
         "definition": "DA_RaidSwordManCombo", "startup": "DA_RaidSwordManComboStartUp",
         "summon": "GA_RaidSwordManComboSummon", "blackboard": "BB_RaidSwordManCombo",
         "tree": "BT_RaidSwordManCombo"}
SOURCES = {key: SOURCE + "/" + name for key, name in {
    "boss": "BP_RaidSwordManSummonMechanic", "controller": "AIC_RaidSwordManSummonMechanic",
    "definition": "DA_RaidSwordManSummonMechanic", "startup": "DA_RaidSwordManSummonMechanicStartUp",
    "summon": "GA_RaidSwordManSummonMechanic"}.items()}
SOURCES["blackboard"] = VARIANT + "/BB_RaidSwordManVariant"


def check(value, message):
    if not value:
        raise RuntimeError(message)


def load(path):
    value = unreal.load_asset(path)
    check(value, "Missing asset: " + path)
    return value


def default(bp):
    return unreal.get_default_object(bp.generated_class())


def save(asset):
    check(unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False), "Save failed: " + asset.get_path_name())


def compile_bp(bp):
    bp.modify()
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    check("ERROR" not in str(bp.get_editor_property("status")).upper(), "Compile failed: " + bp.get_path_name())


def native(name):
    result = unreal.load_class(None, "/Script/Project_RPG." + name)
    check(result, "Build the native boss classes first: " + name)
    return result


def key(name, kind):
    return unreal.BlackboardKeySelector(selected_key_name=name, selected_key_type=kind)


def bb_type(kind):
    return unreal.load_class(None, "/Script/AIModule.BlackboardKeyType_" + kind)


def scalar(obj, field, value):
    wrapped = obj.get_editor_property(field)
    if hasattr(wrapped, "set_editor_property"):
        wrapped.set_editor_property("default_value", value)
        obj.set_editor_property(field, wrapped)
    else:
        obj.set_editor_property(field, value)


def node(tree, cls, name):
    value = unreal.new_object(cls, outer=tree, name=name)
    value.set_editor_property("node_name", name)
    return value


def child(value, gates=()):
    result = unreal.BTCompositeChild()
    result.set_editor_property("child_composite" if isinstance(value, unreal.BTCompositeNode) else "child_task", value)
    result.set_editor_property("decorators", list(gates))
    return result


def phase_gate(tree, name, phase):
    gate = node(tree, native("BTDecorator_RPGBossPhase"), name)
    gate.set_editor_property("required_phase", phase)
    gate.set_editor_property("bExactMatch", True)
    gate.set_editor_property("flow_abort_mode", unreal.BTFlowAbortMode.BOTH)
    return gate


def ability_task(tree, name, cls):
    task = node(tree, native("BTTask_RPGBossAbility"), name)
    task.set_editor_property("AbilityClass", cls)
    task.set_editor_property("bUsePhasePatterns", False)
    task.set_editor_property("TimeoutSeconds", 15.0)
    return task


def create_tree(bundle, attacks, tree=None):
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    if tree is None:
        tree = tools.create_asset(NAMES["tree"], ROOT, unreal.BehaviorTree, unreal.BehaviorTreeFactory())
    check(tree, "Could not create combo BT")
    check(tree.get_editor_property("root_node") is None, "Refusing to rebuild an existing BT")
    tree.set_editor_property("blackboard_asset", bundle["blackboard"])
    root = node(tree, unreal.BTComposite_Selector, "GreatswordRaidRoot")
    root.set_editor_property("services", [node(tree, native("BTService_RPGBossState"), "ServerBossState")])
    combat = node(tree, unreal.BTComposite_Sequence, "Combat")
    gates = []
    for name, key_name, kind in (("InCombat", "BossInCombat", bb_type("Bool")),
                                 ("HasTarget", "TargetActor", bb_type("Object"))):
        gate = node(tree, unreal.BTDecorator_Blackboard, name)
        gate.set_editor_property("blackboard_key", key(key_name, kind))
        gate.set_editor_property("basic_operation", unreal.BasicKeyOperation.SET)
        gate.set_editor_property("flow_abort_mode", unreal.BTFlowAbortMode.BOTH)
        gates.append(gate)

    phases = list(bundle["definition"].get_editor_property("phases"))
    # New nodes belong to this tree; no source BT node object is shared.
    movement = node(tree, unreal.BTComposite_Selector, "Movement")
    dash = node(tree, unreal.BTComposite_Sequence, "PhaseTwoDashOpener")
    dash_phase = phase_gate(tree, "DashAfterMechanic", phases[2].get_editor_property("phase_tag"))
    dash_phase.set_editor_property("flow_abort_mode", unreal.BTFlowAbortMode.NONE)
    distance_gates = []
    for name, distance, inverse in (("DashAtLeast400", 400.0, True), ("DashAtMost800", 800.0, False)):
        gate = node(tree, unreal.BTDecorator_IsAtLocation, name)
        gate.set_editor_property("blackboard_key", key("TargetActor", bb_type("Object")))
        scalar(gate, "acceptable_radius", distance)
        scalar(gate, "bUseNavAgentGoalLocation", False)
        enum = type(gate.get_editor_property("geometric_distance_type"))
        gate.set_editor_property("geometric_distance_type", next(getattr(enum, n) for n in dir(enum) if n.replace("_", "").upper() == "DISTANCE2D"))
        gate.set_editor_property("bInverseCondition", inverse)
        gate.set_editor_property("flow_abort_mode", unreal.BTFlowAbortMode.NONE)
        distance_gates.append(gate)
    dash.set_editor_property("children", [child(ability_task(tree, "DashToTarget", load(VARIANT + "/GameplayAbility/GA_RaidSwordManVariant_Dash").generated_class()))])
    move = node(tree, unreal.BTTask_MoveTo, "ChaseTarget")
    move.set_editor_property("blackboard_key", key("TargetActor", bb_type("Object")))
    scalar(move, "acceptable_radius", 180.0)
    movement.set_editor_property("children", [child(dash, [dash_phase, *distance_gates]), child(move)])
    face = node(tree, native("BTTask_RotateToFaceTarget"), "FaceTarget")
    face.set_editor_property("InTargetToFaceKey", key("TargetActor", bb_type("Object")))
    actions = node(tree, unreal.BTComposite_Selector, "PhaseActions")
    first = node(tree, unreal.BTComposite_Sequence, "PhaseOneThreeHitCombo")
    first.set_editor_property("children", [child(ability_task(tree, "OpeningSlash" + str(i + 1), bp.generated_class())) for i, bp in enumerate(attacks)])
    second = node(tree, unreal.BTComposite_Selector, "PhaseTwoPatterns")
    enhanced = node(tree, unreal.BTComposite_Sequence, "ThreeSlashesThenHeavySlam")
    enhanced.set_editor_property("children", [*[child(ability_task(tree, "EnragedSlash" + str(i + 1), bp.generated_class())) for i, bp in enumerate(attacks)],
        child(ability_task(tree, "HeavySlam", load(BASE + "/GameplayAbility/GA_RaidSwordMan_HeavyStrike").generated_class()))])
    second.set_editor_property("children", [child(ability_task(tree, "PersistentHazard", load(VARIANT + "/GameplayAbility/GA_RaidSwordManVariant_GroundHazard").generated_class())), child(enhanced)])
    actions.set_editor_property("children", [child(second, [phase_gate(tree, "CombatPhaseTwo", phases[2].get_editor_property("phase_tag"))]),
        child(first, [phase_gate(tree, "CombatPhaseOne", phases[0].get_editor_property("phase_tag"))])])
    recovery = node(tree, unreal.BTTask_Wait, "ComboRecovery")
    scalar(recovery, "wait_time", .6)
    scalar(recovery, "random_deviation", 0.0)
    combat.set_editor_property("children", [child(movement), child(face), child(actions), child(recovery)])
    idle = node(tree, unreal.BTTask_Wait, "AwaitCombatOrTarget")
    scalar(idle, "wait_time", .2)
    root.set_editor_property("children", [child(combat, gates), child(idle)])
    tree.set_editor_property("root_node", root)
    return tree


def create_attacks(tools):
    attacks = []
    for i, (source_name, hit_time, windup) in enumerate(ATTACKS, 1):
        folder = ROOT + "/Presentation"
        seq = tools.duplicate_asset("A_GreatswordSlash" + str(i), folder, load(ANIMATIONS + source_name))
        check(seq, "Could not copy compatible sword attack")
        seq.set_editor_property("enable_root_motion", False)
        seq.set_editor_property("force_root_lock", True)
        unreal.AnimationLibrary.remove_all_animation_notify_tracks(seq)
        factory = unreal.AnimMontageFactory()
        factory.set_editor_property("source_animation", seq)
        factory.set_editor_property("target_skeleton", seq.get_editor_property("skeleton"))
        montage = tools.create_asset("AM_GreatswordSlash" + str(i), folder, unreal.AnimMontage, factory)
        check(montage, "Could not create combo montage")
        bp = tools.duplicate_asset("GA_GreatswordSlash" + str(i), ROOT + "/GameplayAbility", load(BASE + "/GameplayAbility/GA_RaidSwordMan_Strike"))
        check(bp, "Could not create combo ability")
        cdo = default(bp)
        for name, value in {"presentation_montage": montage, "presentation_play_rate": hit_time / windup,
                            "windup_seconds": windup, "recovery_seconds": .35,
                            "PatternCooldownSeconds": 0.0, "MinimumActivationDistance": 0.0,
                            "MaximumActivationDistance": 400.0, "base_damage": unreal.ScalableFloat(value=12.0 + (i - 1) * 3.0)}.items():
            cdo.set_editor_property(name, value)
        compile_bp(bp)
        for asset in (seq, montage, bp):
            save(asset)
        attacks.append(bp)
    return attacks


def materialize_editor_graph(bundle):
    """Prepare a graph, not its final save. Native CopySelectedNodes must reset
    generated instance ownership/flags before the first graph save and reload."""
    tree = bundle["tree"]
    unreal.get_editor_subsystem(unreal.AssetEditorSubsystem).open_editor_for_assets([tree])
    roots = [value for value in unreal.ObjectIterator(unreal.BehaviorTreeGraphNode_Root)
             if value.get_outer().get_outer() == tree]
    check(len(roots) == 1, "Expected one editable BT root")
    # UE's new root graph initially chooses the project default Blackboard,
    # overriding the runtime field. Set the editor root so both remain coherent.
    roots[0].set_editor_property("blackboard_asset", bundle["blackboard"])
    check(tree.get_editor_property("blackboard_asset") == bundle["blackboard"], "Editor root did not retain the boss Blackboard")


def verify(bundle, attacks):
    boss = default(bundle["boss"])
    check(boss.get_editor_property("character_start_up_data") == bundle["startup"] and
          boss.get_editor_property("ai_controller_class") == bundle["controller"].generated_class() and
          boss.get_editor_property("boss_ai_component").get_editor_property("definition") == bundle["definition"] and
          default(bundle["controller"]).get_editor_property("BTAsset") == bundle["tree"], "Boss bundle is not connected")
    skeleton = boss.get_editor_property("mesh").get_editor_property("skeletal_mesh_asset").get_editor_property("skeleton")
    for bp in attacks:
        cdo = default(bp)
        montage = cdo.get_editor_property("presentation_montage")
        seq = montage.get_first_anim_reference()
        check(seq.get_editor_property("skeleton") == skeleton and not seq.get_editor_property("enable_root_motion") and
              seq.get_editor_property("force_root_lock") and not unreal.AnimationLibrary.get_animation_notify_events(seq) and
              not unreal.AnimationLibrary.get_animation_notify_events(montage), "Unsafe combo animation")
        check(abs(montage.get_play_length() - seq.get_play_length()) < .02 and cdo.get_editor_property("PatternCooldownSeconds") == 0.0,
              "Combo length/cooldown mismatch")
    check(bundle["tree"].get_editor_property("blackboard_asset") == bundle["blackboard"], "Wrong combo blackboard")
    grants = list(bundle["startup"].get_editor_property("NPCStartUpAbilities"))
    phases = list(bundle["definition"].get_editor_property("phases"))
    check(len(phases) == 3 and phases[1].get_editor_property("bSuspendCombatAI") and
          phases[1].get_editor_property("PhaseEntryAbility") == bundle["summon"].generated_class(), "Summon mechanic was lost")
    check(all(a.generated_class() in grants for a in attacks) and all(p in grants for phase in phases for p in phase.get_editor_property("ability_patterns")),
          "BT patterns were not granted by startup")
    root = bundle["tree"].get_editor_property("root_node")
    found = []
    def walk(composite):
        check(composite and composite.get_outer() == bundle["tree"], "Missing/shared BT composite")
        check(all(service and service.get_outer() == bundle["tree"] for service in composite.get_editor_property("services")), "Missing/shared BT service")
        for entry in composite.get_editor_property("children"):
            check(all(gate and gate.get_outer() == bundle["tree"] for gate in entry.get_editor_property("decorators")), "Missing/shared BT decorator")
            nested, task = entry.get_editor_property("child_composite"), entry.get_editor_property("child_task")
            check(nested or task, "Missing BT child")
            if nested:
                walk(nested)
            elif task and task.get_class() == native("BTTask_RPGBossAbility"):
                check(task.get_outer() == bundle["tree"] and task.get_editor_property("AbilityClass") in grants, "Shared/ungranted BT task")
                found.append(task)
    walk(root)
    check(len(found) == 9, "Expected dash, hazard, two three-hit combos and heavy finisher")


def main(apply=False):
    exists = unreal.EditorAssetLibrary.does_asset_exist(ROOT + "/" + NAMES["boss"])
    if exists:
        bundle = {key: load(ROOT + "/" + name) for key, name in NAMES.items()}
        attacks = [load(ROOT + "/GameplayAbility/GA_GreatswordSlash" + str(i)) for i in (1, 2, 3)]
    else:
        check(apply, "Use main(apply=True) to create the new combo preset")
        check(not unreal.EditorAssetLibrary.list_assets(ROOT, recursive=True, include_folder=False), "Partial/custom combo folder exists; refusing overwrite")
        tools = unreal.AssetToolsHelpers.get_asset_tools()
        sources = {key: load(path) for key, path in SOURCES.items()}
        skeleton = default(sources["boss"]).get_editor_property("mesh").get_editor_property("skeletal_mesh_asset").get_editor_property("skeleton")
        check(all(load(ANIMATIONS + name).get_editor_property("skeleton") == skeleton for name, _, _ in ATTACKS), "Attack skeleton mismatch")
        bundle = {key: tools.duplicate_asset(NAMES[key], ROOT, source) for key, source in sources.items()}
        check(all(bundle.values()), "Partial bundle: inspect new folder before retrying")
        compile_bp(bundle["summon"])
        attacks = create_attacks(tools)
        phases = list(bundle["definition"].get_editor_property("phases"))
        phases[0].set_editor_property("ability_patterns", [a.generated_class() for a in attacks])
        phases[1].set_editor_property("PhaseEntryAbility", bundle["summon"].generated_class())
        phases[2].set_editor_property("ability_patterns", [*phases[2].get_editor_property("ability_patterns"), *[a.generated_class() for a in attacks]])
        bundle["definition"].set_editor_property("phases", phases)
        grants = list(bundle["startup"].get_editor_property("NPCStartUpAbilities"))
        old_summon = sources["summon"].generated_class()
        bundle["startup"].set_editor_property("NPCStartUpAbilities", [bundle["summon"].generated_class() if c == old_summon else c for c in grants] + [a.generated_class() for a in attacks])
        bundle["tree"] = create_tree(bundle, attacks)
        default(bundle["controller"]).set_editor_property("BTAsset", bundle["tree"])
        compile_bp(bundle["controller"])
        boss = default(bundle["boss"])
        boss.set_editor_property("ai_controller_class", bundle["controller"].generated_class())
        boss.set_editor_property("character_start_up_data", bundle["startup"])
        component = boss.get_editor_property("boss_ai_component")
        check(component.get_outer() == boss, "Cannot modify an inherited source component")
        component.set_editor_property("definition", bundle["definition"])
        compile_bp(bundle["boss"])
        verify(bundle, attacks)
        for asset in bundle.values():
            save(asset)
    verify(bundle, attacks)
    unreal.log("RAID_SWORDMAN_COMBO_READY ThreeHitCombo=1 SummonMechanic=1 ServerGAS=1 SourcesUnchanged=1")
    return bundle


if __name__ == "__main__":
    main()
