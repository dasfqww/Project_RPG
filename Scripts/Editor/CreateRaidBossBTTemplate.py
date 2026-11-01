"""Create a reusable, phase-count-independent boss BT/Blackboard pair.

Only new assets in NPC/Raid/Common are created with -RPGCreateRaidBossBTTemplate.
Existing assets are verified, never overwritten. No boss, controller, startup,
ability, phase definition or production tree is modified or automatically wired.
Duplicate these assets before adapting movement/recovery for an actual boss.
"""
import unreal

ROOT = "/Game/Blueprints/Character/NPC/Raid/Common"
BB_PATH = ROOT + "/BB_RaidBossTemplate"
BT_PATH = ROOT + "/BT_RaidBossTemplate"
APPLY = "-rpgcreateraidbossbttemplate" in unreal.SystemLibrary.get_command_line().lower()


def require(value, message):
    if not value:
        raise RuntimeError(message)


def native(name, module="AIModule"):
    value = unreal.load_class(None, "/Script/{0}.{1}".format(module, name))
    require(value, "Missing native class: " + name)
    return value


def selector(name, kind):
    return unreal.BlackboardKeySelector(selected_key_name=name, selected_key_type=kind)


def scalar(obj, field, value):
    wrapped = obj.get_editor_property(field)
    wrapped.set_editor_property("default_value", value)
    obj.set_editor_property(field, wrapped)


def distance_2d(gate):
    # Derive the reflected enum type; its Python name need not drop the F prefix.
    enum_type = type(gate.get_editor_property("geometric_distance_type"))
    return next(getattr(enum_type, name) for name in dir(enum_type)
                if name.replace("_", "").upper() == "DISTANCE2D")


def node(tree, kind, name):
    value = unreal.new_object(kind, outer=tree, name=name)
    value.set_editor_property("node_name", name)
    return value


def child(value, decorators=()):
    entry = unreal.BTCompositeChild()
    entry.set_editor_property("child_composite" if isinstance(value, unreal.BTCompositeNode) else "child_task", value)
    entry.set_editor_property("decorators", list(decorators))
    return entry


def create():
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    blackboard = tools.create_asset("BB_RaidBossTemplate", ROOT, unreal.BlackboardData, unreal.BlackboardDataFactory())
    require(blackboard, "Could not create template blackboard")
    entries = list(blackboard.get_editor_property("keys"))  # Retain the engine's SelfActor key, if already installed.
    for name, kind in (("TargetActor", "Object"), ("BossInCombat", "Bool"),
                       ("BossPhaseIndex", "Int"), ("BossEnraged", "Bool")):
        key_type = unreal.new_object(native("BlackboardKeyType_" + kind), outer=blackboard)
        if kind == "Object":
            key_type.set_editor_property("BaseClass", unreal.Pawn.static_class())
        entry = unreal.BlackboardEntry()
        entry.set_editor_property("entry_name", name)
        entry.set_editor_property("key_type", key_type)
        entries.append(entry)
    blackboard.set_editor_property("keys", entries)
    tree = tools.create_asset("BT_RaidBossTemplate", ROOT, unreal.BehaviorTree, unreal.BehaviorTreeFactory())
    require(tree, "Could not create template behavior tree")
    tree.set_editor_property("blackboard_asset", blackboard)
    root = node(tree, unreal.BTComposite_Selector, "RaidRoot")
    root.set_editor_property("services", [node(tree, native("BTService_RPGBossState", "Project_RPG"), "ServerBossState")])
    combat = node(tree, unreal.BTComposite_Sequence, "Combat")
    move = node(tree, unreal.BTTask_MoveTo, "ChaseTarget")
    move.set_editor_property("blackboard_key", selector("TargetActor", native("BlackboardKeyType_Object")))
    scalar(move, "acceptable_radius", 180.0)
    rotate = node(tree, native("BTTask_RotateToFaceTarget", "Project_RPG"), "FaceTarget")
    rotate.set_editor_property("InTargetToFaceKey", selector("TargetActor", native("BlackboardKeyType_Object")))
    execute = node(tree, native("BTTask_RPGBossAbility", "Project_RPG"), "ExecuteCurrentPhasePattern")
    execute.set_editor_property("bUsePhasePatterns", True)
    execute.set_editor_property("AbilityClass", None)
    execute.set_editor_property("TimeoutSeconds", 15.0)
    recovery = node(tree, unreal.BTTask_Wait, "Recovery")
    scalar(recovery, "wait_time", 0.5)
    scalar(recovery, "random_deviation", 0.0)
    combat.set_editor_property("children", [child(move), child(rotate), child(execute), child(recovery)])
    gates = []
    for name, key_name, kind in (("CombatGate", "BossInCombat", "Bool"), ("TargetGate", "TargetActor", "Object")):
        gate = node(tree, unreal.BTDecorator_Blackboard, name)
        gate.set_editor_property("blackboard_key", selector(key_name, native("BlackboardKeyType_" + kind)))
        gate.set_editor_property("basic_operation", unreal.BasicKeyOperation.SET)
        gate.set_editor_property("flow_abort_mode", unreal.BTFlowAbortMode.BOTH)
        gates.append(gate)
    idle = node(tree, unreal.BTTask_Wait, "AwaitCombatOrPattern")
    scalar(idle, "wait_time", 0.2)
    scalar(idle, "random_deviation", 0.0)
    root.set_editor_property("children", [child(combat, gates), child(idle)])
    tree.set_editor_property("root_node", root)
    # As with the existing raid tree, the editor reconstructs the editable graph
    # from runtime nodes when first opened. No C++ test classes are introduced.
    verify(tree, blackboard)
    for asset in (blackboard, tree):
        require(unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False),
                "Could not save new template asset: " + asset.get_path_name())
    return tree, blackboard


def verify(tree, blackboard, dash_class=None):
    require(isinstance(tree, unreal.BehaviorTree) and isinstance(blackboard, unreal.BlackboardData), "Template asset types differ")
    require(tree.get_editor_property("blackboard_asset") == blackboard, "Template blackboard reference differs")
    raw_entries = list(blackboard.get_editor_property("keys"))
    entries = {str(entry.get_editor_property("entry_name")): entry.get_editor_property("key_type") for entry in raw_entries}
    require(len(entries) == len(raw_entries) and all(not entry.get_editor_property("bInstanceSynced") for entry in raw_entries),
            "Blackboard keys must be unique and per boss, never instance-synced")
    authored_keys = {"TargetActor", "BossInCombat", "BossPhaseIndex", "BossEnraged"}
    # UBlackboardData::UpdateParentKeys installs SelfActor during PostLoad.
    require(set(entries) in (authored_keys, authored_keys | {"SelfActor"}) and
            blackboard.get_editor_property("parent") is None, "Template blackboard layout differs")
    if "SelfActor" in entries:
        require(entries["SelfActor"].get_class() == native("BlackboardKeyType_Object"), "Invalid engine SelfActor key")
    for name, kind in (("TargetActor", "Object"), ("BossInCombat", "Bool"), ("BossPhaseIndex", "Int"), ("BossEnraged", "Bool")):
        require(name in entries and entries[name].get_class() == native("BlackboardKeyType_" + kind), "Wrong key type: " + name)
    require(entries["TargetActor"].get_editor_property("BaseClass") == unreal.Pawn.static_class(), "Target key must accept Pawn")
    root = tree.get_editor_property("root_node")
    require(root and root.get_class() == unreal.BTComposite_Selector.static_class(), "Missing root selector")
    services = list(root.get_editor_property("services"))
    require(len(services) == 1 and services[0].get_class() == native("BTService_RPGBossState", "Project_RPG"), "Missing root boss-state service")
    for field, name in (("InCombatKey", "BossInCombat"), ("PhaseIndexKey", "BossPhaseIndex"), ("EnragedKey", "BossEnraged")):
        require(str(services[0].get_editor_property(field).get_editor_property("selected_key_name")) == name, "Boss-state key differs")
    branches = list(root.get_editor_property("children"))
    require(len(branches) == 2, "Expected combat and idle branches")
    combat = branches[0].get_editor_property("child_composite")
    require(combat and combat.get_class() == unreal.BTComposite_Sequence.static_class(), "Missing combat sequence")
    gates = list(branches[0].get_editor_property("decorators"))
    require(len(gates) == 2, "Expected combat/target gates, not fixed phase-index gates")
    gate_keys = set()
    for gate in gates:
        require(gate.get_class() == unreal.BTDecorator_Blackboard.static_class() and
                gate.get_editor_property("flow_abort_mode") == unreal.BTFlowAbortMode.BOTH and
                gate.get_editor_property("basic_operation") == unreal.BasicKeyOperation.SET, "Invalid abort gate")
        gate_keys.add(str(gate.get_editor_property("blackboard_key").get_editor_property("selected_key_name")))
    require(gate_keys == {"BossInCombat", "TargetActor"}, "Invalid combat gate keys")
    tasks = [entry.get_editor_property("child_task") for entry in combat.get_editor_property("children")]
    if dash_class:
        # Only a duplicated boss tree may opt into the authored phase-two opener.
        require(tree.get_path_name().startswith("/Game/Blueprints/Character/NPC/Raid/SwordManVariant/"),
                "Do not customize the common template")
        movement = list(combat.get_editor_property("children"))[0].get_editor_property("child_composite")
        require(movement and movement.get_class() == unreal.BTComposite_Selector.static_class(), "Missing dash/chase selector")
        choices = list(movement.get_editor_property("children"))
        require(len(choices) == 2, "Expected dash opener and normal chase")
        opener = choices[0].get_editor_property("child_composite")
        require(opener and opener.get_class() == unreal.BTComposite_Sequence.static_class(), "Missing dash opener")
        conditions = list(choices[0].get_editor_property("decorators"))
        require(len(conditions) == 3, "Expected phase and two distance gates")
        phase = conditions[0]
        if phase.get_class().get_path_name() == "/Script/Project_RPG.BTDecorator_RPGBossPhase":
            definition = unreal.load_asset("/Game/Blueprints/Character/NPC/Raid/SwordManVariant/DA_RaidSwordManVariant")
            require(isinstance(definition, unreal.RPGBossDefinition), "Missing variant definition")
            phases = list(definition.get_editor_property("phases"))
            require(len(phases) == 2 and phase.get_editor_property("bExactMatch") and
                    phase.get_editor_property("required_phase").export_text() == phases[1].get_editor_property("phase_tag").export_text(),
                    "Wrong dash phase tag")
        else:
            require(phase.get_class() == unreal.BTDecorator_Blackboard.static_class() and
                    str(phase.get_editor_property("blackboard_key").get_editor_property("selected_key_name")) == "BossPhaseIndex" and
                    phase.get_editor_property("arithmetic_operation") == unreal.ArithmeticKeyOperation.EQUAL and
                    phase.get_editor_property("int_value") == 1, "Wrong dash phase gate")
        require(not phase.get_editor_property("bInverseCondition"), "Dash phase gate must not be inverted")
        for gate, radius, inverse in zip(conditions[1:], (400.0, 800.0), (True, False)):
            require(gate.get_class() == native("BTDecorator_IsAtLocation") and
                    str(gate.get_editor_property("blackboard_key").get_editor_property("selected_key_name")) == "TargetActor" and
                    gate.get_editor_property("acceptable_radius") == radius and
                    gate.get_editor_property("bInverseCondition") == inverse and
                    not gate.get_editor_property("bPathFindingBasedTest").get_editor_property("default_value") and
                    gate.get_editor_property("geometric_distance_type") == distance_2d(gate),
                    "Wrong geometric dash distance gate")
        require(all(gate.get_editor_property("flow_abort_mode") == unreal.BTFlowAbortMode.NONE for gate in conditions),
                "Do not abort a dash when it crosses its entry distance")
        dash_tasks = [entry.get_editor_property("child_task") for entry in opener.get_editor_property("children")]
        require(len(dash_tasks) == 2 and dash_tasks[0] and
                dash_tasks[0].get_class() == native("BTTask_RotateToFaceTarget", "Project_RPG") and
                str(dash_tasks[0].get_editor_property("InTargetToFaceKey").get_editor_property("selected_key_name")) == "TargetActor" and
                dash_tasks[1] and dash_tasks[1].get_class() == native("BTTask_RPGBossAbility", "Project_RPG") and
                not dash_tasks[1].get_editor_property("bUsePhasePatterns") and
                dash_tasks[1].get_editor_property("AbilityClass") == dash_class and
                dash_tasks[1].get_editor_property("TimeoutSeconds") == 5.0, "Wrong dash task")
        tasks[0] = choices[1].get_editor_property("child_task")
    kinds = [unreal.BTTask_MoveTo.static_class(), native("BTTask_RotateToFaceTarget", "Project_RPG"),
             native("BTTask_RPGBossAbility", "Project_RPG"), unreal.BTTask_Wait.static_class()]
    require(len(tasks) == 4 and all(task and task.get_class() == kind for task, kind in zip(tasks, kinds)), "Invalid combat tasks")
    for task, field in ((tasks[0], "blackboard_key"), (tasks[1], "InTargetToFaceKey")):
        require(str(task.get_editor_property(field).get_editor_property("selected_key_name")) == "TargetActor", "Movement target key differs")
    require(tasks[2].get_editor_property("bUsePhasePatterns") and tasks[2].get_editor_property("AbilityClass") is None and
            tasks[2].get_editor_property("TimeoutSeconds") == 15.0, "Task must use frozen phase patterns with a finite timeout")
    require(branches[1].get_editor_property("child_task") and
            branches[1].get_editor_property("child_task").get_class() == unreal.BTTask_Wait.static_class(), "Missing idle fallback")


exists = [unreal.EditorAssetLibrary.does_asset_exist(path) for path in (BT_PATH, BB_PATH)]
if all(exists):
    pair = (unreal.load_asset(BT_PATH), unreal.load_asset(BB_PATH))
elif any(exists):
    raise RuntimeError("Partial template already exists; refusing to overwrite or repair user assets")
elif APPLY:
    pair = create()
else:
    raise RuntimeError("Templates are absent. Use -RPGCreateRaidBossBTTemplate to create only the new pair")
verify(*pair)
unreal.log("RAID_BT_TEMPLATE COMPLETE PhaseAgnostic=1 ServerState=1 GASSelection=1 ExistingBossesUnchanged=1")
