"""Inspect Guardian's existing second melee grant; author only with explicit opt-in.

Uses the existing NPC startup map and original GA. No character/ability BP changes.
"""
import hashlib
import json
from pathlib import Path

import unreal

DATA = '/Game/Blueprints/Character/NPC/Gruntling/Guardian/DA_Guardian'
ATTACK = '/Game/Blueprints/Character/NPC/Gruntling/GameplayAbility/GA_Guardian_Melee2.GA_Guardian_Melee2_C'
FIRST = ATTACK.replace('Melee2', 'Melee1')
PROPERTIES = ('activate_on_given_abilities', 'reactive_abilities', 'start_up_gameplay_effects',
              'npc_start_up_abilities', 'attack_hit_reaction_policies', 'attack_stun_durations',
              'attack_knockbacks', 'attack_slows', 'attack_burns', 'attack_staggers', 'ability_immunities')
SETTINGS = dict(distance=240., duration=.4, height=180., down_duration=.6, get_up_duration=.8)


def configured(value):
    return value is not None and all(abs(float(value.get_editor_property(k))-v)<.00001 for k,v in SETTINGS.items())


def preserve(properties, original):
    for name in PROPERTIES:
        if name != 'attack_knockbacks':
            assert properties[name] == original[name], 'Preserve '+name
    assert {k:v for k,v in properties['attack_knockbacks'].items() if k != ATTACK} == original['attack_knockbacks'], 'Preserve other knockback entries'


def encode(value):
    if isinstance(value, unreal.Object):
        return value.get_path_name()
    if isinstance(value, unreal.Map):
        return {str(encode(k)): encode(v) for k, v in value.items()}
    if isinstance(value, (unreal.Array, unreal.Set, list, tuple)):
        return [encode(v) for v in value]
    if hasattr(value, 'export_text'):
        return value.export_text()
    return value if isinstance(value, (str, int, float, bool)) or value is None else str(value)


def main():
    root = Path(unreal.Paths.project_dir()).resolve()
    output = root / 'Saved/CombatGuardianMelee2'
    output.mkdir(parents=True, exist_ok=True)
    assert unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is None, 'Preserve PIE'
    assert DATA not in {str(p.get_name()) for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}, 'Preserve dirty target'
    data = unreal.load_asset(DATA)
    attack = unreal.load_class(None, ATTACK)
    first = unreal.load_class(None, FIRST)
    assert isinstance(data, unreal.DataAsset_NPCStartUpData) and attack and first
    values = {name: encode(data.get_editor_property(name)) for name in PROPERTIES}
    character = unreal.load_asset('/Game/Blueprints/Character/NPC/Gruntling/Guardian/BP_Gruntling_Guardian')
    default = unreal.get_default_object(character.generated_class())
    startup = encode(default.get_editor_property('character_start_up_data'))
    command = unreal.SystemLibrary.get_command_line().split()
    apply = '-RPGGuardianMelee2Apply' in command
    reload = '-RPGGuardianMelee2Reload' in command
    if apply or reload:
        baseline = json.loads((output/'Before.json').read_text(encoding='utf-8'))
        assert DATA in startup and ATTACK in values['npc_start_up_abilities'], 'Original NPC startup grant required'
        assert not unreal.MathLibrary.class_is_child_of(first, attack) and not unreal.MathLibrary.class_is_child_of(attack, first), 'Preserve sibling attack'
        preserve(values, baseline['properties'])
        entries = dict(data.get_editor_property('attack_knockbacks'))
        current = entries.get(attack)
        changed = not configured(current)
        saves = 0
        if changed:
            assert apply and not reload, 'Reload must find the authored entry'
            assert current is None and not entries, 'Preserve existing or inherited configuration'
            file = root/'Content'/ (DATA.removeprefix('/Game/')+'.uasset')
            assert hashlib.sha256(file.read_bytes()).hexdigest().upper() == baseline['assetSHA256'], 'Asset changed since inspection'
            assert DATA not in {str(p.get_name()) for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}, 'Preserve unsaved target changes'
            backup = output/'Backups/DA_Guardian-Before.uasset'
            backup.parent.mkdir(parents=True, exist_ok=True)
            if not backup.exists():
                backup.write_bytes(file.read_bytes())
            assert hashlib.sha256(backup.read_bytes()).hexdigest().upper() == baseline['assetSHA256']
            entries[attack] = unreal.RPGKnockbackSettings(**SETTINGS)
            data.set_editor_property('attack_knockbacks', entries)
            assert unreal.EditorAssetLibrary.save_loaded_asset(data, only_if_is_dirty=False), 'Save target only'
            saves = 1
        after = {name: encode(data.get_editor_property(name)) for name in PROPERTIES}
        preserve(after, baseline['properties'])
        assert configured(dict(data.get_editor_property('attack_knockbacks')).get(attack))
        assert DATA not in {str(p.get_name()) for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}, 'Target must be clean after save/reload'
        result = dict(data=DATA, attack=ATTACK, settings=SETTINGS, applied=changed, saveCalls=saves,
                      properties=after, protectedPropertiesPreserved=True, originalGrantPreserved=True,
                      originalGAClasses=True, targetDirty=False, wouldChangeAgain=False,
                      netExecutionPolicy=str(unreal.get_default_object(attack).get_editor_property('net_execution_policy')),
                      assetSHA256=hashlib.sha256((root/'Content'/(DATA.removeprefix('/Game/')+'.uasset')).read_bytes()).hexdigest().upper())
        name = 'ReloadResult.json' if reload else 'ApplyResult.json'
        (output/name).write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
        unreal.log('GUARDIAN_MELEE2_AUTHORED '+json.dumps({k:v for k,v in result.items() if k != 'properties'}))
        return
    graphs = []
    bp = unreal.load_asset(ATTACK.rsplit('.', 1)[0])
    seen = set()
    while isinstance(bp, unreal.Blueprint) and bp.get_path_name() not in seen:
        seen.add(bp.get_path_name())
        for graph in unreal.BlueprintEditorLibrary.list_graphs(bp):
            for node in unreal.BlueprintGraphEditor.get_graph_editor(graph).list_all_nodes():
                pins = [{'name': str(p.get_pin_name()), 'value': str(p.get_pin_value()),
                         'links': [x.get_owning_node().get_name()+':'+str(x.get_pin_name()) for x in p.list_connected_pins()]}
                        for p in node.list_all_pins()]
                graphs.append(dict(asset=bp.get_path_name(), graph=graph.get_name(),
                                   node=node.get_name(), title=node.get_node_title(), pins=pins))
        parent = bp.get_blueprint_parent_class().get_path_name()
        bp = unreal.load_asset(parent.rsplit('.', 1)[0]) if parent.startswith('/Game/') else None
    result = dict(data=DATA, attack=ATTACK, properties=values, characterStartup=startup,
                  targetGranted=ATTACK in values['npc_start_up_abilities'],
                  siblings=(not unreal.MathLibrary.class_is_child_of(attack, first)
                            and not unreal.MathLibrary.class_is_child_of(first, attack)),
                  graphs=graphs, assetsSaved=[])
    (output/'Inspection.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    unreal.log('GUARDIAN_MELEE2_INSPECTED '+json.dumps({k:v for k,v in result.items() if k not in ('graphs','properties')}))


if __name__ == '__main__':
    main()
