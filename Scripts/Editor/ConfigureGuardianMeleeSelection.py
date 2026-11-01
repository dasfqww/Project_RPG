"""Disambiguate Guardian melee selection without weakening the shared GAS lookup.

Default is read-only. Back up the two melee BPs and Guardian BT before running
with -RPGApplyGuardianMeleeSelection. Only those three assets may be saved.
The parent Melee tag still matches both attacks for cancellation/blocking.
"""
import unreal

ROOT = '/Game/Blueprints/Character/NPC/Gruntling'
TREE = ROOT + '/Guardian/BT_Gruntling'
PREFIX = 'RPG_GUARDIAN_MELEE_SELECTION'
APPLY = '-rpgapplyguardianmeleeselection' in unreal.SystemLibrary.get_command_line().lower()
PARENT = '(TagName="NPC.Ability.Melee")'


def require(value, message):
    if not value:
        raise RuntimeError(message)


def tag(name):
    result = unreal.GameplayTag()
    require(result.import_text('(TagName="' + name + '")'), 'Could not import registered tag: ' + name)
    return result


tree = unreal.load_asset(TREE)
require(isinstance(tree, unreal.BehaviorTree), 'Guardian BT missing')
tasks = {}


def visit(node):
    if not node:
        return
    if isinstance(node, unreal.BTCompositeNode):
        for child in node.get_editor_property('children'):
            visit(child.get_editor_property('child_composite'))
            visit(child.get_editor_property('child_task'))
    elif node.get_class().get_name() == 'BTTask_ActivateAbilityByTag_C':
        tasks[node.get_name()] = node


visit(tree.get_editor_property('root_node'))
require(set(tasks) == {'BTTask_ActivateAbilityByTag_C_0', 'BTTask_ActivateAbilityByTag_C_1'},
        'Custom Guardian attack tasks; refusing rewrite')
plans = []
for index, suffix in enumerate(('Primary', 'Secondary')):
    name = 'NPC.Ability.Melee.Guardian.' + suffix
    desired_tag = tag(name)
    container = unreal.GameplayTagContainer()
    require(container.import_text('(GameplayTags=((TagName="' + name + '")))'), 'Could not import ability tags')
    path = ROOT + '/GameplayAbility/GA_Guardian_Melee' + str(index + 1)
    blueprint = unreal.load_asset(path)
    require(isinstance(blueprint, unreal.Blueprint), 'Guardian melee BP missing: ' + path)
    cdo = unreal.get_default_object(blueprint.generated_class())
    existing = cdo.get_editor_property('ability_tags').export_text()
    require(existing in ('(GameplayTags=((TagName="NPC.Ability.Melee")))', container.export_text()),
            'Custom Guardian ability tags; refusing rewrite: ' + existing)
    task = tasks['BTTask_ActivateAbilityByTag_C_' + str(index)]
    property_name = None
    for candidate in ('Ability Tag to Activate', 'AbilityTagToActivate', 'ability_tag_to_activate'):
        try:
            current_tag = task.get_editor_property(candidate)
            property_name = candidate
            break
        except Exception:
            continue
    require(property_name, 'Cannot find authored Guardian task tag property')
    require(current_tag.export_text() in (PARENT, desired_tag.export_text()),
            'Custom Guardian task tag; refusing rewrite: ' + current_tag.export_text())
    plans.append((blueprint, cdo, container, task, property_name, desired_tag))
    unreal.log(f'{PREFIX} PREFLIGHT Ability={path} Task={task.get_name()} Tag={name}')

changed_blueprints = []
tree_changed = False
for blueprint, cdo, container, task, property_name, desired_tag in plans:
    ability_changed = cdo.get_editor_property('ability_tags').export_text() != container.export_text()
    task_changed = task.get_editor_property(property_name).export_text() != desired_tag.export_text()
    if not APPLY:
        continue
    if ability_changed:
        blueprint.modify()
        cdo.modify()
        cdo.set_editor_property('ability_tags', container)
        changed_blueprints.append(blueprint)
    if task_changed:
        tree.modify()
        task.modify()
        task.set_editor_property(property_name, desired_tag)
        tree_changed = True
for blueprint in changed_blueprints:
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    require('ERROR' not in str(blueprint.get_editor_property('status')).upper(), 'Guardian melee compile failed')
for blueprint, cdo, container, task, property_name, desired_tag in plans:
    if APPLY:
        recompiled = unreal.get_default_object(blueprint.generated_class())
        require(recompiled.get_editor_property('ability_tags').export_text() == container.export_text(), 'Ability tag verification failed')
        require(task.get_editor_property(property_name).export_text() == desired_tag.export_text(), 'BT tag verification failed')
for asset in [*changed_blueprints, *([tree] if tree_changed else [])]:
    require(unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False), 'Guardian asset save failed')
unreal.log(f'{PREFIX} COMPLETE Apply={int(APPLY)} Saved={len(changed_blueprints) + int(tree_changed)} SharedGASUnchanged=1')
