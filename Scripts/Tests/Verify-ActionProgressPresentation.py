"""Read-only reflection and saved PlayerHUD reload check, called by the PS runner."""
import hashlib
import json
import pathlib
import runpy

import unreal
from editor_toolset.toolsets.blueprint import BlueprintTools


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


args = unreal.SystemLibrary.parse_command_line(unreal.SystemLibrary.get_command_line())
parameters = {str(k).lower(): str(v) for k, v in args[2].items()}
output = pathlib.Path(parameters['rpgactionprogressproof'])
workspace = pathlib.Path(__file__).resolve().parents[2]
helpers = runpy.run_path(str(workspace / 'Scripts/Editor/ConfigureGreatSwordFourthSkillSlot.py'), run_name='progress_reload_helpers')

# These properties must come from the installed native module, not Python stand-ins.
spec = unreal.RPGSkillRuntimeSpec()
require(str(spec.get_editor_property('display_name')) == '', 'RuntimeSpec.DisplayName missing or not empty')
hud_defaults = unreal.get_default_object(unreal.RPGHUD)
widget_class = hud_defaults.get_editor_property('action_progress_widget_class')
require(widget_class == unreal.RPGActionProgressWidget.static_class(), 'Native progress default class differs')
defaults = {}
for name, expected in (
    ('action_progress_size', (420, 160)),
    ('action_progress_anchor', (.5, .75)),
    ('action_progress_alignment', (.5, 0)),
    ('action_progress_offset', (0, 0)),
):
    value = hud_defaults.get_editor_property(name)
    require(abs(value.x-expected[0]) < .0001 and abs(value.y-expected[1]) < .0001, 'Unexpected default: '+name)
    defaults[name] = [value.x, value.y]
require(hud_defaults.get_editor_property('action_progress_z_order') == 5, 'Unexpected default Z order')

disk = workspace / 'Content/Blueprints/UI/PlayerHUD/WBP_PlayerHUD.uasset'
before = hashlib.sha256(disk.read_bytes()).hexdigest()
hud = unreal.load_asset(helpers['HUD'])
require(isinstance(hud, unreal.WidgetBlueprint), 'Missing PlayerHUD')
widgets = helpers['snapshot_widgets'](unreal, hud)
require('WBP_ProgressBar' not in widgets and 'PlayerSkillBar' in widgets, 'PlayerHUD modular structure differs')
graphs = BlueprintTools().list_graphs(hud)
require('BrightProgressBar' not in {g.get_name() for g in graphs}, 'Legacy brightness function remains')
unreal.BlueprintEditorLibrary.compile_blueprint(hud)
require(hud.get_editor_property('status') == unreal.BlueprintStatus.BS_UP_TO_DATE, 'PlayerHUD compilation failed')
dirty = {p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
require(helpers['HUD'] not in dirty, 'Read-only compile dirtied PlayerHUD')
require(hashlib.sha256(disk.read_bytes()).hexdigest() == before, 'PlayerHUD changed on disk')
output.write_text(json.dumps({
    'passed': True,
    'runtime_name_property': True,
    'native_widget_class': widget_class.get_path_name(),
    'defaults': defaults,
    'z_order': 5,
    'widgets': len(widgets),
    'graphs': [g.get_name() for g in graphs],
    'compiled': True,
    'dirty': False,
    'saved_assets': 0,
    'asset_sha256': before,
}, indent=2), encoding='utf-8')
unreal.log('ACTION_PROGRESS_INSTALLED_PRESENTATION PASS')
