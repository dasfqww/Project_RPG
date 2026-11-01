"""Move the authored HUD's single viewport call into its owning player layer.

Read-only by default. -RPGScopePlayerHUD saves only GA_Player_DrawHUD.
"""
import copy
import hashlib
import json
import pathlib
import shutil

import unreal
from editor_toolset.toolsets.blueprint import BlueprintTools as BP
from toolset_registry.helpers import require_editable


def require(value, message):
    if not value:
        raise RuntimeError(message)


def pin_key(pin):
    return [pin.node.get_path_name(), str(pin.direction), pin.index_id]


def node_snapshot(info):
    def pins(items):
        return {p.name: {'value': p.value, 'type': p.type_id,
                         'links': sorted([pin_key(c) for c in p.connected_pins])} for p in items}
    return {'type': info.type_id, 'position': [info.position.x, info.position.y],
            'inputs': pins(info.input_pins), 'outputs': pins(info.output_pins)}


def snapshot(bp):
    return {n.get_path_name(): node_snapshot(BP.get_node_infos([n])[0])
            for graph in BP.list_graphs(bp) for n in BP.find_nodes(graph)}


_, switches, params = unreal.SystemLibrary.parse_command_line(unreal.SystemLibrary.get_command_line())
params = {str(k).lower(): str(v) for k, v in params.items()}
apply = any(str(s).lower() == 'rpgscopeplayerhud' for s in switches)
workspace = pathlib.Path(__file__).resolve().parents[2]
output = pathlib.Path(params['rpgvitalsanchorproof'])
path = '/Game/Blueprints/Character/Player/Abilities/GA_Player_DrawHUD'
disk = workspace / 'Content/Blueprints/Character/Player/Abilities/GA_Player_DrawHUD.uasset'
require(any(' / vitals-pie-20261101 |' in line for line in
            (workspace / 'Docs/Online_RPG_Development_Roadmap.md').read_text(encoding='utf-8-sig').splitlines()), 'Missing owner')
require((pathlib.Path(unreal.Paths.project_content_dir()) / 'Blueprints/Character/Player/Abilities/GA_Player_DrawHUD.uasset').resolve()
        == disk.resolve(), 'Original caller mount required')
require(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is None, 'Preserve PIE')
assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
bp = assets.load_asset(path)
require(isinstance(bp, unreal.Blueprint), 'Missing actual HUD caller')
require(path not in {p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}, 'Preserve dirty caller')
before_hash = hashlib.sha256(disk.read_bytes()).hexdigest()
before = snapshot(bp)
old = [n for graph in BP.list_graphs(bp) for n in BP.find_nodes(graph)
       if BP.get_node_infos([n])[0].type_id.endswith('|AddToViewport')]
existing = [n for graph in BP.list_graphs(bp) for n in BP.find_nodes(graph)
            if BP.get_node_infos([n])[0].type_id.endswith('|AddToPlayerScreen')]
require((len(old), len(existing)) in ((1, 0), (0, 1)), 'Review changed viewport call structure')
proof = {'passed': False, 'apply': apply, 'changed': bool(old), 'saved_assets': [], 'before_hash': before_hash,
         'before_nodes': before}
if apply and old:
    require_editable(bp)
    shutil.copy2(disk, output.parent / 'DrawHUD-before.uasset.backup')
    original = old[0]
    info = BP.get_node_infos([original])[0]
    require(set(p.name for p in info.input_pins) == {'execute', 'self', 'ZOrder'}
            and set(p.name for p in info.output_pins) == {'then'}, 'Review viewport call pins')
    old_path = original.get_path_name()
    new_node = BP.create_node(original.get_outer(), info.type_id.rsplit('|', 1)[0]+'|AddToPlayerScreen',
                              info.position, unreal.UserWidget.static_class())
    new_info = BP.get_node_infos([new_node])[0]
    require(new_info.type_id.endswith('|AddToPlayerScreen'), 'Wrong replacement function')
    new_inputs = {p.name: p for p in new_info.input_pins}
    new_outputs = {p.name: p for p in new_info.output_pins}
    require(set(new_outputs) == {'then', 'ReturnValue'}, 'Unexpected replacement outputs')
    BP.delete_node(original)
    for pin in info.input_pins:
        if pin.connected_pins:
            for source in pin.connected_pins:
                BP.connect_pins(source, new_inputs[pin.name].pin_id)
        elif pin.value:
            BP.set_pin_value(new_inputs[pin.name].pin_id, pin.value)
    for pin in info.output_pins:
        for target in pin.connected_pins:
            BP.connect_pins(new_outputs[pin.name].pin_id, target)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    require(bp.get_editor_property('status') == unreal.BlueprintStatus.BS_UP_TO_DATE, 'Caller compile failed')
    after = snapshot(bp)
    expected = json.loads(json.dumps(before).replace(old_path, new_node.get_path_name()))
    new_path = new_node.get_path_name()
    expected[new_path]['type'] = after[new_path]['type']
    require(not after[new_path]['outputs']['ReturnValue']['links'], 'Return must remain unconnected')
    expected[new_path]['outputs']['ReturnValue'] = after[new_path]['outputs']['ReturnValue']
    require(after == expected, 'Unexpected nodes, values or connections changed')
    require(hashlib.sha256(disk.read_bytes()).hexdigest() == before_hash, 'Caller changed on disk')
    require(assets.save_loaded_asset(bp, only_if_is_dirty=False), 'Could not save caller')
    proof['saved_assets'] = [path]
    proof['after_nodes'] = after
proof['sha256'] = hashlib.sha256(disk.read_bytes()).hexdigest()
proof['passed'] = True
output.write_text(json.dumps(proof, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.log('PLAYER_VITALS_HOST PASS Changed='+str(bool(old))+' Saved='+str(len(proof['saved_assets'])))
