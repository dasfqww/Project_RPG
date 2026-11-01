"""Opt-in phase pattern setup and phase-tag gates for the existing raid tree.

Back up DA_RaidSwordMan and BT_RaidSwordMan before applying. No new attack,
balance change, startup grant, graph rebuild, player asset or original NPC save.
Without an apply flag, verify only. -RPGApplyRaidBossPhaseTagGates migrates
only the two existing BT phase conditions; it never changes the definition.
"""
import unreal

ROOT = "/Game/Blueprints/Character/NPC/SwordMan"
APPLY = "-rpgapplyraidbossphasepatterns" in unreal.SystemLibrary.get_command_line().lower()
APPLY_PHASE_GATES = "-rpgapplyraidbossphasetaggates" in unreal.SystemLibrary.get_command_line().lower()
PHASE_GATE_PATH = "/Script/Project_RPG.BTDecorator_RPGBossPhase"


def require(path, kind):
    obj = unreal.load_asset(path)
    if not isinstance(obj, kind):
        raise RuntimeError("Missing/incompatible raid phase asset: " + path)
    return obj


def save(obj):
    if not unreal.EditorAssetLibrary.save_loaded_asset(obj, only_if_is_dirty=False):
        raise RuntimeError("Could not save scoped raid phase asset")


def phase_gates(composite, tree, phase_tags):
    result = []
    for child_index, entry in enumerate(composite.get_editor_property("children")):
        decorators = list(entry.get_editor_property("decorators"))
        for decorator_index, gate in enumerate(decorators):
            if gate.get_class().get_path_name() == PHASE_GATE_PATH:
                tag_text = gate.get_editor_property("required_phase").export_text()
                if tag_text not in phase_tags or not gate.get_editor_property("bExactMatch"):
                    raise RuntimeError("Unexpected phase tag condition; refusing overwrite")
                phase_index = phase_tags.index(tag_text)
            elif isinstance(gate, unreal.BTDecorator_Blackboard) and str(
                    gate.get_editor_property("blackboard_key").get_editor_property("selected_key_name")) == "BossPhaseIndex":
                if gate.get_editor_property("arithmetic_operation") != unreal.ArithmeticKeyOperation.EQUAL:
                    raise RuntimeError("Unexpected phase comparison; refusing overwrite")
                phase_index = gate.get_editor_property("int_value")
            else:
                continue
            if (phase_index not in (0, 1) or len(decorators) != 1 or gate.get_outer() != tree or
                    gate.get_editor_property("bInverseCondition") or
                    gate.get_editor_property("flow_abort_mode") != unreal.BTFlowAbortMode.BOTH):
                raise RuntimeError("Unexpected phase branch/ownership/abort mode; refusing overwrite")
            result.append((composite, child_index, decorator_index, gate, phase_index))
        nested = entry.get_editor_property("child_composite")
        if nested:
            result.extend(phase_gates(nested, tree, phase_tags))
    return result


def migrate_phase_gates(tree, phases):
    phase_tags = [phase.get_editor_property("phase_tag").export_text() for phase in phases]
    if len(set(phase_tags)) != 2 or any(str(phase.get_editor_property("phase_tag").get_editor_property("TagName"))
                                       in ("", "None") for phase in phases):
        raise RuntimeError("Expected two distinct authored phase tags")
    root = tree.get_editor_property("root_node")
    gates = phase_gates(root, tree, phase_tags)
    if len(gates) != 2 or {gate[4] for gate in gates} != {0, 1}:
        raise RuntimeError("Expected exactly one branch per phase; refusing graph rebuild")
    legacy = [gate for gate in gates if gate[3].get_class().get_path_name() != PHASE_GATE_PATH]
    if legacy:
        # BTGraph is protected in Python. Conservatively reject any directly owned
        # editor graph instead of saving runtime nodes that disagree with it.
        if any(graph.get_outer() == tree for graph in unreal.ObjectIterator(unreal.EdGraph)):
            raise RuntimeError("BT has an editor graph; migrate its decorators in the editor")
        phase_class = unreal.load_class(None, PHASE_GATE_PATH)
        if not phase_class:
            raise RuntimeError("Build the native boss phase decorator first")
        tree.modify()
        for parent, child_index, decorator_index, old, phase_index in legacy:
            replacement = unreal.new_object(phase_class, outer=tree, name="RaidPhase{}TagGate".format(phase_index + 1))
            replacement.set_editor_property("required_phase", phases[phase_index].get_editor_property("phase_tag"))
            replacement.set_editor_property("bExactMatch", True)
            replacement.set_editor_property("flow_abort_mode", old.get_editor_property("flow_abort_mode"))
            children = list(parent.get_editor_property("children"))
            entry = children[child_index]
            decorators = list(entry.get_editor_property("decorators"))
            decorators[decorator_index] = replacement
            entry.set_editor_property("decorators", decorators)
            children[child_index] = entry
            parent.modify()
            parent.set_editor_property("children", children)
        updated = phase_gates(root, tree, phase_tags)
        if len(updated) != 2 or any(gate[3].get_class().get_path_name() != PHASE_GATE_PATH for gate in updated):
            raise RuntimeError("Both phase tag replacements must be retained before saving")
        save(tree)
    unreal.log("RAID_PHASE_TAG_GATES COMPLETE Branches=2 OnlyRaidBTSaved=1")


definition = require(ROOT + "/DA_RaidSwordMan", unreal.RPGBossDefinition)
tree = require(ROOT + "/BT_RaidSwordMan", unreal.BehaviorTree)
phases = list(definition.get_editor_property("phases"))
attacks = [require(ROOT + "/GameplayAbility/GA_RaidSwordMan_" + name, unreal.Blueprint).generated_class()
           for name in ("Strike", "HeavyStrike")]
if len(phases) != 2 or any(abs(phase.get_editor_property("health_threshold") - value) > 0.0001
                           for phase, value in zip(phases, (1.0, 0.7))):
    raise RuntimeError("Unexpected raid phase layout; refusing migration")

tasks = []


def walk(composite):
    for entry in composite.get_editor_property("children"):
        nested = entry.get_editor_property("child_composite")
        task = entry.get_editor_property("child_task")
        if nested:
            walk(nested)
        elif isinstance(task, unreal.BTTask_RPGBossAbility):
            tasks.append(task)


walk(tree.get_editor_property("root_node"))
if len(tasks) != 2:
    raise RuntimeError("Expected the two authored raid attack tasks; refusing graph rebuild")
for task in tasks:
    if not task.get_editor_property("bUsePhasePatterns") and task.get_editor_property("AbilityClass") not in attacks:
        raise RuntimeError("Unexpected explicit attack; refusing to overwrite custom BT data")
allowed_patterns = [[ [attacks[0]] ], [ [attacks[1]], [attacks[1], attacks[0]] ]]
for phase, allowed in zip(phases, allowed_patterns):
    existing = list(phase.get_editor_property("ability_patterns"))
    if existing and existing not in allowed:
        raise RuntimeError("Custom phase patterns already authored; refusing overwrite")
if APPLY:
    definition_dirty = any(not list(phase.get_editor_property("ability_patterns")) for phase in phases)
    if definition_dirty:
        definition.modify()
        for phase, attack in zip(phases, attacks):
            if not list(phase.get_editor_property("ability_patterns")):
                phase.set_editor_property("ability_patterns", [attack])
        definition.set_editor_property("phases", phases)
        save(definition)
    tree_dirty = any(not task.get_editor_property("bUsePhasePatterns") or
                     task.get_editor_property("AbilityClass") is not None for task in tasks)
    if tree_dirty:
        tree.modify()
        for task in tasks:
            task.modify()
            task.set_editor_property("bUsePhasePatterns", True)
            task.set_editor_property("AbilityClass", None)
        save(tree)
for phase, allowed in zip(definition.get_editor_property("phases"), allowed_patterns):
    if list(phase.get_editor_property("ability_patterns")) not in allowed:
        raise RuntimeError("Raid phase patterns not configured")
if any(not task.get_editor_property("bUsePhasePatterns") or
       task.get_editor_property("AbilityClass") is not None for task in tasks):
    raise RuntimeError("Raid BT still selects an explicit attack")
if APPLY_PHASE_GATES:
    migrate_phase_gates(tree, phases)
unreal.log("RAID_PHASE_PATTERNS_CONFIG COMPLETE FrozenPhaseSelection=1 GASPriority=1 OnlyRaidDefinitionAndBTSaved=1 Applied=" + str(int(APPLY)))
