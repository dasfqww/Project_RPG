"""Use replicated identity presentation while retaining the authored module skin.

Read-only by default. -RPGConfigureGreatSwordIdentityHUD saves only the identity
module after its old Rage event bindings, layout and animations are checked.
"""
import hashlib
import json
import pathlib
import runpy
import unreal
from editor_toolset.toolsets.blueprint import BlueprintTools as BP
from toolset_registry.helpers import require_editable

workspace = pathlib.Path(__file__).resolve().parents[2]
helpers = runpy.run_path(str(workspace / 'Scripts/Editor/ConfigureGreatSwordFourthSkillSlot.py'), run_name='identity_skin_helpers')
graph = runpy.run_path(str(workspace / 'Scripts/Editor/ExtractPlayerHUDModules.py'), run_name='identity_graph_helpers')['graph']


def require(value, reason):
    if not value:
        raise RuntimeError(reason)


def main():
    _, switches, params = unreal.SystemLibrary.parse_command_line(unreal.SystemLibrary.get_command_line())
    params = {str(k).lower(): str(v) for k, v in params.items()}
    apply = any(str(s).lower() == 'rpgconfiguregreatswordidentityhud' for s in switches)
    output = pathlib.Path(params.get('rpgidentityhudproof') or params['rpggreatswordproof'])
    path = '/Game/Blueprints/UI/PlayerHUD/Modules/WBP_PlayerIdentity'
    assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    require(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is None, 'Preserve PIE')
    require(path not in {p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}, 'Preserve unsaved identity module')
    module = assets.load_asset(path)
    parent = unreal.RPGGreatSwordIdentityWidget
    changed = not isinstance(unreal.get_default_object(module.generated_class()), parent)
    before = helpers['snapshot_widgets'](unreal, module)
    require(set(before) == {'CanvasPanel_30', 'WBP_IdentityGaugeBar', 'WBP_AbilityIconSlot_RageSkill'}, 'Review changed module hierarchy')
    before_graph = graph(module)
    approved = {'K2Node_Event_3', 'K2Node_VariableSet_0', 'K2Node_VariableGet_12', 'K2Node_CustomEvent_1',
                'K2Node_AssignDelegate_1', 'K2Node_VariableGet_2', 'K2Node_CallFunction_2', 'K2Node_VariableGet_29',
                'K2Node_VariableGet_28', 'K2Node_CustomEvent_12', 'K2Node_AssignDelegate_11', 'K2Node_VariableGet_33',
                'K2Node_VariableGet_34', 'K2Node_CallFunction_21', 'K2Node_CallFunction_24', 'K2Node_CustomEvent_13',
                'K2Node_AssignDelegate_12', 'K2Node_CallFunction_17', 'K2Node_CallFunction_6'}
    require(set(before_graph) == (approved if changed else set()), 'Review changed identity event graph')
    template = assets.load_asset('/Game/Blueprints/UI/TemplateWidget/TPWBP_StatusBar')
    controls = helpers['snapshot_widgets'](unreal, template)
    require('ProgressBar_Main' in controls and 'ProgressText' in controls, 'Native skin bindings changed')
    animations = list(unreal.RPGHUDModuleTools.describe_animations(module))
    require(animations == ['__PROPERTY_BINDINGS__|0', 'InterpUp|WBP_IdentityGaugeBar|None|0'], 'Review changed animation bindings')
    protected = [workspace / 'Content/Blueprints/UI/PlayerHUD/WBP_PlayerHUD.uasset',
                 workspace / 'Content/Blueprints/UI/TemplateWidget/TPWBP_StatusBar.uasset']
    hashes = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in protected}
    result = {'passed': False, 'apply': apply, 'changed': changed, 'saved_assets': [],
              'old_nodes': before_graph, 'animations': animations, 'template_controls': list(controls)}
    if apply and changed:
        require(any(' / greatsword-identity-hud-20261102 |' in row and '진행 중' in row for row in
                    (workspace / 'Docs/Online_RPG_Development_Roadmap.md').read_text(encoding='utf-8-sig').splitlines()), 'Missing owner reservation')
        require_editable(module)
        # Preserve the template/animation objects; retire only the obsolete subscriptions.
        for g in BP.list_graphs(module):
            for n in list(BP.find_nodes(g)):
                BP.delete_node(n)
        unreal.BlueprintEditorLibrary.reparent_blueprint(module, parent)
        unreal.BlueprintEditorLibrary.remove_unused_variables(module)
        unreal.BlueprintEditorLibrary.compile_blueprint(module)
        require(module.get_editor_property('status') == unreal.BlueprintStatus.BS_UP_TO_DATE, 'Native module compile failed')
        require(helpers['snapshot_widgets'](unreal, module) == before, 'Unrelated widget skin or layout changed')
        require(list(unreal.RPGHUDModuleTools.describe_animations(module)) == animations, 'Animation changed')
        require(not graph(module), 'Legacy callbacks remain')
        require(assets.save_loaded_asset(module, only_if_is_dirty=False), 'Could not save identity module')
        result['saved_assets'].append(path)
    require(all(hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest() == h for p, h in hashes.items()), 'Protected HUD or template changed')
    result['native_presentation_bound'] = isinstance(unreal.get_default_object(module.generated_class()), parent)
    result['layout_preserved'] = helpers['snapshot_widgets'](unreal, module) == before
    result['passed'] = True
    output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()
