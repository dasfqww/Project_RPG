"""Create and verify raid SwordMan GAS/BT content with Editor closed.

Run after ConfigureRaidSwordMan.py and an Editor build. Apply with
-RPGApplyRaidSwordManCombat. Without that switch, only verify saved assets.
Existing combat assets are validated and reused, never silently rebuilt.
Original SwordMan assets are never saved by this script.
"""
import unreal

ROOT = "/Game/Blueprints/Character/NPC/SwordMan"
BOSS = ROOT + "/BP_RaidSwordMan"
STARTUP = ROOT + "/DA_RaidSwordManStartUp"
CONTROLLER = ROOT + "/AIC_RaidSwordMan"
BLACKBOARD = ROOT + "/BB_RaidSwordMan"
TREE = ROOT + "/BT_RaidSwordMan"
STRIKE = ROOT + "/GameplayAbility/GA_RaidSwordMan_Strike"
HEAVY = ROOT + "/GameplayAbility/GA_RaidSwordMan_HeavyStrike"
APPLY = "-RPGApplyRaidSwordManCombat"


def require(path, kind):
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if not isinstance(asset, kind):
        raise RuntimeError("Missing or incompatible asset: " + path)
    return asset


def require_startup(path):
    asset = require(path, unreal.DataAsset)
    if asset.get_class().get_path_name() != "/Script/Project_RPG.DataAsset_NPCStartUpData":
        raise RuntimeError("Expected NPC startup data: " + path)
    return asset


def save(asset):
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError("Failed to save " + asset.get_path_name())


def compile_bp(asset):
    unreal.BlueprintEditorLibrary.compile_blueprint(asset)
    if "ERROR" in str(asset.get_editor_property("status")).upper():
        raise RuntimeError("Blueprint compile failed: " + asset.get_path_name())


def native(name, module="AIModule"):
    cls = unreal.load_class(None, "/Script/{}.{}".format(module, name))
    if cls is None:
        raise RuntimeError("Required native class is unavailable: " + name)
    return cls


def is_native(obj, name, module="Project_RPG"):
    return obj is not None and obj.get_class() == native(name, module)


def key(name, kind):
    return unreal.BlackboardKeySelector(selected_key_name=name, selected_key_type=kind)


def scalar(obj, property_name, value):
    field = obj.get_editor_property(property_name)
    field.set_editor_property("default_value", value)
    obj.set_editor_property(property_name, field)


def node(tree, kind, name):
    result = unreal.new_object(kind, outer=tree, name=name)
    result.set_editor_property("node_name", name)
    return result


def child(value, decorators=()):
    result = unreal.BTCompositeChild()
    result.set_editor_property("child_composite" if isinstance(value, unreal.BTCompositeNode) else "child_task", value)
    result.set_editor_property("decorators", list(decorators))
    return result


def condition(tree, name, key_name, kind, phase=None):
    result = node(tree, unreal.BTDecorator_Blackboard, name)
    result.set_editor_property("blackboard_key", key(key_name, kind))
    result.set_editor_property("flow_abort_mode", unreal.BTFlowAbortMode.BOTH)
    if phase is None:
        result.set_editor_property("basic_operation", unreal.BasicKeyOperation.SET)
    else:
        result.set_editor_property("arithmetic_operation", unreal.ArithmeticKeyOperation.EQUAL)
        result.set_editor_property("int_value", phase)
    return result


def make_attack(path, action_name, windup, radius, damage):
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        asset = require(path, unreal.Blueprint)
        if asset.get_blueprint_parent_class() != unreal.RPGBossTelegraphedStrikeAbility.static_class():
            raise RuntimeError("Unexpected existing attack parent: " + path)
        return asset
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", unreal.RPGBossTelegraphedStrikeAbility.static_class())
    folder, name = path.rsplit("/", 1)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.Blueprint, factory)
    if asset is None:
        raise RuntimeError("Could not create attack: " + path)
    default = unreal.get_default_object(asset.generated_class())
    tag = unreal.GameplayTag()
    if not tag.import_text('(TagName="{}")'.format(action_name)):
        raise RuntimeError("Invalid action tag: " + action_name)
    default.set_editor_property("action_tag", tag)
    default.set_editor_property("windup_seconds", windup)
    default.set_editor_property("radius", radius)
    default.set_editor_property("base_damage", unreal.ScalableFloat(value=damage))
    damage_class = unreal.EditorAssetLibrary.load_blueprint_class(
        "/Game/Blueprints/Character/Shared/GameplayEffect/GE_Shared_ApplyDamage")
    if damage_class is None:
        raise RuntimeError("Shared damage effect is unavailable")
    default.set_editor_property("damage_effect_class", damage_class)
    default.set_editor_property("security_profile", unreal.RPGSkillSecurityProfile(
        maximum_server_hit_distance=600.0, hit_location_tolerance=80.0,
        maximum_targets_per_query=16, maximum_hits_per_activation=1,
        maximum_damage_per_hit=1000.0))
    asset.modify()
    compile_bp(asset)
    save(asset)
    return asset


def make_blackboard():
    if unreal.EditorAssetLibrary.does_asset_exist(BLACKBOARD):
        return require(BLACKBOARD, unreal.BlackboardData)
    asset = unreal.EditorAssetLibrary.duplicate_asset(
        "/Game/Blueprints/Character/NPC/BB_NPC_Base", BLACKBOARD)
    if asset is None:
        raise RuntimeError("Could not duplicate the NPC blackboard")
    entries = list(asset.get_editor_property("keys"))
    for name, kind in (("BossInCombat", native("BlackboardKeyType_Bool")),
                       ("BossPhaseIndex", native("BlackboardKeyType_Int")),
                       ("BossEnraged", native("BlackboardKeyType_Bool"))):
        if any(str(entry.get_editor_property("entry_name")) == name for entry in entries):
            raise RuntimeError("Base blackboard already defines " + name)
        entry = unreal.BlackboardEntry()
        entry.set_editor_property("entry_name", name)
        entry.set_editor_property("key_type", unreal.new_object(kind, outer=asset))
        entries.append(entry)
    asset.set_editor_property("keys", entries)
    save(asset)
    return asset


def make_tree(blackboard, strike, heavy):
    if unreal.EditorAssetLibrary.does_asset_exist(TREE):
        return require(TREE, unreal.BehaviorTree)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "BT_RaidSwordMan", ROOT, unreal.BehaviorTree, unreal.BehaviorTreeFactory())
    if asset is None:
        raise RuntimeError("Could not create the raid behavior tree")
    asset.set_editor_property("blackboard_asset", blackboard)
    root = node(asset, unreal.BTComposite_Selector, "RaidRoot")
    root.set_editor_property("services", [node(asset, native("BTService_RPGBossState", "Project_RPG"), "ServerBossState")])
    combat = node(asset, unreal.BTComposite_Sequence, "Combat")
    move = node(asset, unreal.BTTask_MoveTo, "ChaseTarget")
    move.set_editor_property("blackboard_key", key("TargetActor", native("BlackboardKeyType_Object")))
    scalar(move, "acceptable_radius", 180.0)
    scalar(move, "project_goal_location", False)
    scalar(move, "require_navigable_end_location", False)
    rotate = node(asset, native("BTTask_RotateToFaceTarget", "Project_RPG"), "FaceTarget")
    rotate.set_editor_property("InTargetToFaceKey", key("TargetActor", native("BlackboardKeyType_Object")))
    patterns = node(asset, unreal.BTComposite_Selector, "PhasePatterns")
    branches = []
    for phase, ability, recovery in ((1, heavy, 1.5), (0, strike, 2.0)):
        sequence = node(asset, unreal.BTComposite_Sequence, "Phase{}Attack".format(phase + 1))
        task = node(asset, native("BTTask_RPGBossAbility", "Project_RPG"), "ExecutePhase{}Ability".format(phase + 1))
        task.set_editor_property("AbilityClass", ability.generated_class())
        task.set_editor_property("TimeoutSeconds", 5.0)
        wait = node(asset, unreal.BTTask_Wait, "Phase{}Recovery".format(phase + 1))
        scalar(wait, "wait_time", recovery)
        scalar(wait, "random_deviation", 0.0)
        sequence.set_editor_property("children", [child(task), child(wait)])
        gate = condition(asset, "Phase{}Gate".format(phase + 1), "BossPhaseIndex", native("BlackboardKeyType_Int"), phase)
        branches.append(child(sequence, [gate]))
    patterns.set_editor_property("children", branches)
    combat.set_editor_property("children", [child(move), child(rotate), child(patterns)])
    idle = node(asset, unreal.BTTask_Wait, "AwaitCombatOrTarget")
    scalar(idle, "wait_time", 0.2)
    scalar(idle, "random_deviation", 0.0)
    root.set_editor_property("children", [child(combat, [
        condition(asset, "CombatGate", "BossInCombat", native("BlackboardKeyType_Bool")),
        condition(asset, "TargetGate", "TargetActor", native("BlackboardKeyType_Object"))]), child(idle)])
    asset.set_editor_property("root_node", root)
    # With no saved editor graph, BehaviorTreeEditor::OnCreated reconstructs
    # the editable graph from these runtime nodes when it is first opened.
    save(asset)
    return asset


def verify():
    boss = require(BOSS, unreal.Blueprint)
    controller = require(CONTROLLER, unreal.Blueprint)
    startup = require_startup(STARTUP)
    tree = require(TREE, unreal.BehaviorTree)
    blackboard = require(BLACKBOARD, unreal.BlackboardData)
    attacks = [require(path, unreal.Blueprint) for path in (STRIKE, HEAVY)]
    default = unreal.get_default_object(boss.generated_class())
    controller_default = unreal.get_default_object(controller.generated_class())
    if default.get_editor_property("ai_controller_class") != controller.generated_class():
        raise RuntimeError("Boss is not using the raid controller")
    if default.get_editor_property("character_start_up_data").get_path_name() != startup.get_path_name():
        raise RuntimeError("Boss is not using the raid startup data")
    if controller_default.get_editor_property("BTAsset") != tree or tree.get_editor_property("blackboard_asset") != blackboard:
        raise RuntimeError("Controller/tree/blackboard references do not match")
    if list(startup.get_editor_property("NPCStartUpAbilities")) != [a.generated_class() for a in attacks]:
        raise RuntimeError("Startup must grant exactly the two raid attacks")
    root = tree.get_editor_property("root_node")
    if root is None or not any(is_native(s, "BTService_RPGBossState") for s in root.get_editor_property("services")):
        raise RuntimeError("Tree lacks the root boss-state service")
    found = []
    phase_tasks = []

    def walk(composite):
        for entry in composite.get_editor_property("children"):
            nested = entry.get_editor_property("child_composite")
            task = entry.get_editor_property("child_task")
            if nested:
                walk(nested)
            elif is_native(task, "BTTask_RPGBossAbility"):
                if task.get_editor_property("bUsePhasePatterns"):
                    phase_tasks.append(task)
                else:
                    found.append(task.get_editor_property("AbilityClass"))

    walk(root)
    task_count = len(found) + len(phase_tasks)
    if phase_tasks:
        definition = require(ROOT + "/DA_RaidSwordMan", unreal.RPGBossDefinition)
        for phase in definition.get_editor_property("phases"):
            found.extend(phase.get_editor_property("ability_patterns"))
    if set(found) != set(a.generated_class() for a in attacks) or task_count != 2:
        raise RuntimeError("Tree must execute both granted raid abilities")
    unreal.log_warning("RAIDCOMBAT saved content verification passed")


def main():
    if APPLY.lower() not in unreal.SystemLibrary.get_command_line().lower():
        verify()
        return
    boss = require(BOSS, unreal.Blueprint)
    require_startup(ROOT + "/DA_SwordMan")
    require(ROOT + "/AIC_SwordMan", unreal.Blueprint)
    strike = make_attack(STRIKE, "Boss.Action.SwordMan.Strike", 1.2, 200.0, 20.0)
    heavy = make_attack(HEAVY, "Boss.Action.SwordMan.HeavyStrike", 1.6, 300.0, 35.0)
    blackboard = make_blackboard()
    tree = make_tree(blackboard, strike, heavy)
    if not unreal.EditorAssetLibrary.does_asset_exist(STARTUP):
        startup = unreal.EditorAssetLibrary.duplicate_asset(ROOT + "/DA_SwordMan", STARTUP)
        if startup is None:
            raise RuntimeError("Could not duplicate startup data")
        startup.set_editor_property("NPCStartUpAbilities", [strike.generated_class(), heavy.generated_class()])
        save(startup)
    startup = require_startup(STARTUP)
    if not unreal.EditorAssetLibrary.does_asset_exist(CONTROLLER):
        controller = unreal.EditorAssetLibrary.duplicate_asset(ROOT + "/AIC_SwordMan", CONTROLLER)
        if controller is None:
            raise RuntimeError("Could not duplicate the controller")
        unreal.get_default_object(controller.generated_class()).set_editor_property("BTAsset", tree)
        controller.modify()
        compile_bp(controller)
        save(controller)
    controller = require(CONTROLLER, unreal.Blueprint)
    default = unreal.get_default_object(boss.generated_class())
    default.set_editor_property("ai_controller_class", controller.generated_class())
    default.set_editor_property("character_start_up_data", startup)
    boss.modify()
    compile_bp(boss)
    save(boss)
    verify()


main()
