"""Move visual HUD ownership from a startup ability to the existing HUD class.

Read-only by default. -RPGMigrateHUDOwner saves BP_RPGHUD / DA_Player and deletes
the obsolete ability only after all its package references have disappeared.
Unrelated defaults, widget graphs and gameplay grants must remain unchanged.
"""
import copy
import hashlib
import json
import pathlib
import shutil
import unreal
from editor_toolset.toolsets.blueprint import BlueprintTools as BP
from toolset_registry.helpers import require_editable

HUD = '/Game/Blueprints/UI/PlayerHUD/BP_RPGHUD'
VIEW = '/Game/Blueprints/UI/PlayerHUD/WBP_PlayerHUD'
ABILITY = '/Game/Blueprints/Character/Player/Abilities/GA_Player_DrawHUD'
STARTUP = '/Game/Blueprints/Character/Player/DA_Player'


def properties(obj):
    schema = list(json.loads(unreal.ToolsetLibrary.list_struct_properties(obj.get_class())))
    names = [n if n.startswith('b') and len(n) > 1 and n[1].isupper() else n[0].upper()+n[1:] for n in schema]
    return json.loads(unreal.ToolsetLibrary.get_object_properties(obj, names))


def graph(bp):
    result = []
    for g in BP.list_graphs(bp):
        for n in BP.find_nodes(g):
            info = BP.get_node_infos([n])[0]
            result.append({'node': n.get_name(), 'graph': g.get_name(), 'type': info.type_id,
                           'inputs': {p.name: {'value': p.value, 'links': [[c.node.get_path_name(), str(c.direction), c.index_id] for c in p.connected_pins]}
                                      for p in info.input_pins}})
    return result


def require(value, message):
    if not value:
        raise RuntimeError(message)


def migrate(apply, assets, registry, options, bp, hud, startup, output):
    target = {'refPath': VIEW + '.WBP_PlayerHUD_C'}
    before_hud = properties(unreal.get_default_object(hud.generated_class()))
    before_startup = properties(startup)
    require('PlayerHUDWidgetClass' in before_hud, 'Build native HUD lifecycle support before migration')
    require(before_hud['PlayerHUDWidgetClass'] in ('None', target), 'Preserve custom root configuration')
    require(before_hud['PlayerHUDZOrder'] == 0, 'Review authored root Z order')
    expected_hud = copy.deepcopy(before_hud)
    expected_hud['PlayerHUDWidgetClass'] = target
    expected_startup = copy.deepcopy(before_startup)
    grants = expected_startup['ActivateOnGivenAbilities']
    old_grant = {'refPath': ABILITY + '.GA_Player_DrawHUD_C'}
    require(grants.count(old_grant) <= 1, 'Review duplicate HUD ability grants')
    expected_startup['ActivateOnGivenAbilities'] = [g for g in grants if g != old_grant]
    refs = sorted(str(x) for x in (registry.get_referencers(ABILITY, options) or [])) if bp else []
    require(set(refs).issubset({STARTUP}), 'Review additional HUD ability references')
    if bp:
        nodes = graph(bp)
        require(len(nodes) == 5 and sum(n['type'].endswith('|AddToPlayerScreen') for n in nodes) == 1,
                'Review changed HUD ability graph')
        creators = [n for n in nodes if 'Class' in n['inputs']]
        require(len(creators) == 1 and creators[0]['inputs']['Class']['value'] == target['refPath'],
                'Review changed authored widget class')
    result = {'passed': False, 'mode': 'apply' if apply else 'verify', 'saved_assets': [], 'deleted_assets': [],
              'before_hud': before_hud, 'before_startup': before_startup,
              'before_referencers': refs}
    changed = before_hud != expected_hud or before_startup != expected_startup or bp is not None
    if apply and changed:
        workspace = pathlib.Path(__file__).resolve().parents[2]
        require(any(' / hud-owner-lifecycle-20261102 |' in line and '진행 중' in line for line in
                    (workspace / 'Docs/Online_RPG_Development_Roadmap.md').read_text(encoding='utf-8-sig').splitlines()),
                'Missing active owner reservation')
        require(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is None, 'Preserve PIE')
        dirty = {p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
        require(not dirty.intersection({HUD, STARTUP, ABILITY}), 'Preserve unsaved edits')
        for path in (HUD, STARTUP, ABILITY):
            disk = workspace / ('Content/' + path.removeprefix('/Game/') + '.uasset')
            mounted = pathlib.Path(unreal.Paths.project_content_dir()) / (path.removeprefix('/Game/') + '.uasset')
            require(mounted.resolve() == disk.resolve(), 'Original asset mount required for migration')
            if disk.exists():
                shutil.copy2(disk, output.parent / (disk.name + '.backup'))
        for obj in (hud, startup):
            require_editable(obj)
        if before_startup != expected_startup:
            require(unreal.ToolsetLibrary.set_object_properties(startup, json.dumps({
                'ActivateOnGivenAbilities': expected_startup['ActivateOnGivenAbilities']})), 'Cannot remove startup grant')
            require(properties(startup) == expected_startup, 'Unrelated startup defaults changed')
            require(assets.save_loaded_asset(startup, only_if_is_dirty=False), 'Cannot save startup data')
            result['saved_assets'].append(STARTUP)
        if before_hud != expected_hud:
            require(unreal.ToolsetLibrary.set_object_properties(unreal.get_default_object(hud.generated_class()),
                                                               json.dumps({'PlayerHUDWidgetClass': target})), 'Cannot configure HUD root')
            unreal.BlueprintEditorLibrary.compile_blueprint(hud)
            require(hud.get_editor_property('status') == unreal.BlueprintStatus.BS_UP_TO_DATE, 'HUD compile failed')
            require(properties(unreal.get_default_object(hud.generated_class())) == expected_hud,
                    'Unrelated HUD defaults changed')
            require(assets.save_loaded_asset(hud, only_if_is_dirty=False), 'Cannot save HUD configuration')
            result['saved_assets'].append(HUD)
        if bp:
            # Package names stay valid when /Game is mounted through a private
            # verification project's Content junction. The original OS filename
            # is outside that project's registered content mount.
            registry.scan_modified_asset_files([STARTUP])
            remaining = sorted(str(x) for x in (registry.get_referencers(ABILITY, options) or []))
            require(not remaining, 'Ability still referenced after grant removal: ' + repr(remaining))
            require_editable(bp)
            require(assets.delete_loaded_asset(bp), 'Cannot retire unreferenced HUD ability')
            result['deleted_assets'].append(ABILITY)
    result['after_hud'] = properties(unreal.get_default_object(hud.generated_class()))
    result['after_startup'] = properties(startup)
    result['changed'] = changed
    result['obsolete_ability_exists'] = assets.does_asset_exist(ABILITY)
    result['after_referencers'] = sorted(str(x) for x in (registry.get_referencers(ABILITY, options) or []))
    result['passed'] = (result['after_hud'] == expected_hud and result['after_startup'] == expected_startup
                        and not result['obsolete_ability_exists'] and not result['after_referencers'])
    return result


def main():
    _, switches, params = unreal.SystemLibrary.parse_command_line(unreal.SystemLibrary.get_command_line())
    params = {str(k).lower(): str(v) for k, v in params.items()}
    apply = any(str(s).lower() == 'rpgmigratehudowner' for s in switches)
    output = pathlib.Path(params['rpghudownerproof'])
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.search_all_assets(True)
    options = unreal.AssetRegistryDependencyOptions(True, True, True, True, True)
    assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    bp = assets.load_asset(ABILITY) if assets.does_asset_exist(ABILITY) else None
    hud = unreal.load_asset(HUD)
    view = unreal.load_asset(VIEW)
    startup = unreal.load_asset(STARTUP)
    protected = pathlib.Path(unreal.Paths.project_content_dir()) / 'Blueprints/UI/PlayerHUD/WBP_PlayerHUD.uasset'
    protected_hash = hashlib.sha256(protected.read_bytes()).hexdigest()
    if apply or 'PlayerHUDWidgetClass' in properties(unreal.get_default_object(hud.generated_class())):
        result = migrate(apply, assets, registry, options, bp, hud, startup, output)
        for blueprint in (hud, view):
            unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
            require(blueprint.get_editor_property('status') == unreal.BlueprintStatus.BS_UP_TO_DATE,
                    'Saved HUD configuration or visual Blueprint failed compilation')
        result['hud_and_visual_compile'] = True
    else:
        result = {'passed': True, 'mode': 'audit',
              'ability_referencers': sorted(str(x) for x in (registry.get_referencers(ABILITY, options) or [])),
              'hud_referencers': sorted(str(x) for x in (registry.get_referencers(HUD, options) or [])),
              'startup': properties(startup), 'hud': properties(unreal.get_default_object(hud.generated_class())),
                  'ability_nodes': graph(bp) if bp else [], 'view_nodes': graph(view)}
    require(hashlib.sha256(protected.read_bytes()).hexdigest() == protected_hash, 'Visual HUD changed on disk')
    result['visual_hud_sha256'] = protected_hash
    output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    require(result['passed'], 'Migration incomplete; inspect read-only result or apply explicitly')


if __name__ == '__main__':
    main()
