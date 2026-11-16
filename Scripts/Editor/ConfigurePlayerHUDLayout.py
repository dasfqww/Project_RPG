"""Arrange existing HUD views without changing their graphs or state bindings.

Read-only by default. -RPGApplyPlayerHUDLayout saves only TARGETS. Run with an
idle owned editor/commandlet; caller must also check other editors' dirty assets.
Backups belong to the disposable proof/Temp directory, never to source control.
"""
import copy
import hashlib
import json
import pathlib
import runpy
import shutil

import unreal
from editor_toolset.toolsets.blueprint import BlueprintTools as BP
from toolset_registry.helpers import require_editable

ROOT = pathlib.Path(__file__).resolve().parents[2]
BASE = '/Game/Blueprints/UI/'
HUD = BASE + 'PlayerHUD/WBP_PlayerHUD'
VITALS = BASE + 'PlayerHUD/Modules/WBP_PlayerVitals'
SKILLS = BASE + 'PlayerHUD/Modules/WBP_PlayerSkillBar'
IDENTITY = BASE + 'PlayerHUD/Modules/WBP_PlayerIdentity'
SKILL_CELL = BASE + 'QuickSlot/WBP_SkillQuickSlot'
ITEM_CELL = BASE + 'QuickSlot/WBP_QuickSlot'
TARGETS = (SKILL_CELL, ITEM_CELL, VITALS, SKILLS, IDENTITY, HUD)
HELPERS = runpy.run_path(str(ROOT / 'Scripts/Editor/ConfigureGreatSwordFourthSkillSlot.py'), run_name='hud_layout_helpers')
TOOLS = unreal.get_default_object(unreal.UMGToolSet)
GOLD = dict(r=.83, g=.68, b=.46, a=1)
INK = dict(r=.025, g=.019, b=.016, a=.94)
OPERATIONS = []


def require(value, message):
    if not value:
        raise RuntimeError(message)


def merge(old, patch):
    result = copy.deepcopy(old)
    for key, value in patch.items():
        result[key] = merge(result[key], value) if isinstance(value, dict) and isinstance(result.get(key), dict) else value
    return result


def props(obj):
    return HELPERS['object_properties'](unreal, obj)


def infos(bp):
    return HELPERS['widget_infos'](unreal, bp)


def widget(bp, name):
    return infos(bp)[name].get_editor_property('widget')


def configure(obj, patch):
    before = props(obj)
    require(set(patch).issubset(before), 'Unknown property on ' + obj.get_name())
    desired = {key: merge(before[key], value) if isinstance(value, dict) else value for key, value in patch.items()}
    if all(HELPERS['equivalent'](before[key], value) for key, value in desired.items()):
        return
    is_slot = isinstance(obj, unreal.PanelSlot)
    target = obj.get_content() if is_slot else obj
    OPERATIONS.append(dict(kind='configure', package=target.get_outermost().get_name(),
                           name=target.get_name(), slot=is_slot, values=desired))
    require(unreal.ToolsetLibrary.set_object_properties(obj, json.dumps(desired)), 'Cannot configure ' + obj.get_name())
    after = props(obj)
    require(all(HELPERS['equivalent'](after[key], value) for key, value in desired.items()), 'Readback failed: ' + obj.get_name())


def slot(bp, name, patch):
    configure(infos(bp)[name].get_editor_property('slot'), patch)


def margin(left=0, top=0, right=0, bottom=0):
    return dict(left=left, top=top, right=right, bottom=bottom)


def place(bp, name, x, y, width, height, ax=0, ay=0, align_x=0, align_y=0, z=0):
    slot(bp, name, dict(LayoutData=dict(offsets=margin(x, y, width, height),
         anchors=dict(minimum=dict(x=ax, y=ay), maximum=dict(x=ax, y=ay)),
         alignment=dict(x=align_x, y=align_y)), bAutoSize=False, ZOrder=z))


def slate(color):
    return dict(specifiedColor=color, colorUseRule='UseColor_Specified')


def rounded(color, outline=GOLD):
    return dict(resourceObject='None', resourceName='None', drawAs='RoundedBox',
                tintColor=slate(color), outlineSettings=dict(cornerRadii=dict(x=4, y=4, z=4, w=4),
                roundingType='FixedRadius', width=1, color=slate(outline)))


def add(bp, cls, name, parent, index=-1):
    if name in infos(bp):
        return widget(bp, name)
    OPERATIONS.append(dict(kind='add', package=bp.get_path_name(), name=name,
                           cls=cls.static_class().get_path_name(), parent=parent.get_name(), index=index))
    return TOOLS.call_method('AddWidget', (bp, cls, name, parent, index)).get_editor_property('widget')


def graph(bp):
    def pins(items):
        return {p.name: dict(value=p.value, links=sorted([[c.node.get_name(), str(c.direction), c.index_id]
                for c in p.connected_pins])) for p in items}
    return {g.get_name() + '|' + n.get_name(): dict(type=i.type_id, inputs=pins(i.input_pins), outputs=pins(i.output_pins))
            for g in BP.list_graphs(bp) for n in BP.find_nodes(g) for i in BP.get_node_infos([n])}


def style_cell(bp, frame):
    configure(widget(bp, 'RPGSizeBox_C_31'), dict(WidthOverride=64, HeightOverride=64))
    configure(widget(bp, 'Image_80'), dict(Brush=rounded(INK), Visibility='HitTestInvisible'))
    slot(bp, 'SlotItemImage', dict(Padding=margin(5, 5, 5, 5)))
    if 'SkillCooldownImage' in infos(bp):
        slot(bp, 'SkillCooldownImage', dict(Padding=margin(5, 5, 5, 5)))
    art = add(bp, unreal.Image, 'HUDSlotFrame', widget(bp, 'Overlay_61'))
    configure(art, dict(Brush=frame, Visibility='HitTestInvisible'))
    slot(bp, 'HUDSlotFrame', dict(HorizontalAlignment='HAlign_Fill', VerticalAlignment='VAlign_Fill'))
    for name, size in (('InputKeyText', 13), ('SlotItemCountText', 12)):
        configure(widget(bp, name), dict(Font=dict(size=size, outlineSettings=dict(outlineSize=1)),
                  ColorAndOpacity=slate(dict(r=.98, g=.9, b=.73, a=1))))
    slot(bp, 'InputKeyText', dict(Padding=margin(6, 3, 0, 0), HorizontalAlignment='HAlign_Left', VerticalAlignment='VAlign_Top'))
    slot(bp, 'SlotItemCountText', dict(Padding=margin(0, 0, 6, 4)))


def style_vitals(bp):
    for name, x, label, fill in (('Health', 0, 'HP', dict(r=.62, g=.055, b=.04, a=1)),
                               ('Mana', 408, 'MP', dict(r=.035, g=.22, b=.58, a=1))):
        base = name + '_RPGSizeBox_Base'
        configure(widget(bp, base), dict(WidthOverride=296, HeightOverride=26))
        place(bp, base, x, 0, 296, 26)
        configure(widget(bp, name + '_ProgressBarBorder'), dict(BrushColor=GOLD, Padding=margin(1, 1, 1, 1)))
        # Native Percent/FieldNotify remains the source. A plain fill avoids the
        # legacy template material's independent UV/parameter assumptions.
        configure(widget(bp, name + 'Bar'), dict(FillColorAndOpacity=fill, BarFillStyle='Scale',
            WidgetStyle=dict(fillImage=dict(resourceObject='None', resourceName='None', imageType='NoImage',
            drawAs='Image', tintColor=slate(dict(r=1, g=1, b=1, a=1))))))
        configure(widget(bp, name + 'Text'), dict(Font=dict(size=13, outlineSettings=dict(outlineSize=1)),
                  ColorAndOpacity=slate(dict(r=.98, g=.94, b=.85, a=1))))
        configure(widget(bp, name + '_ProgressTimeText'), dict(Visibility='Collapsed'))
        caption = add(bp, unreal.TextBlock, name + 'ResourceLabel', widget(bp, name + '_Overlay_63'))
        configure(caption, dict(Text=label, Font=dict(size=10, typefaceFontName='Bold'),
                  ColorAndOpacity=slate(dict(r=.93, g=.85, b=.69, a=1)), Visibility='HitTestInvisible'))
        slot(bp, name + 'ResourceLabel', dict(Padding=margin(10, 0, 0, 0), HorizontalAlignment='HAlign_Left', VerticalAlignment='VAlign_Center'))
    place(bp, 'PlayerShield', 0, -32, 296, 28, z=3)
    place(bp, 'PlayerDebuffs', 408, -44, 296, 36, z=3)


def main():
    _, switches, params = unreal.SystemLibrary.parse_command_line(unreal.SystemLibrary.get_command_line())
    apply = 'rpgapplyplayerhudlayout' in {str(s).lower() for s in switches}
    plan_only = 'rpghudlayoutplanonly' in {str(s).lower() for s in switches}
    params = {str(k).lower(): str(v) for k, v in params.items()}
    output = pathlib.Path(params.get('rpghudlayoutproof', str(ROOT / 'Saved/BuildChecks/PlayerHUDLayout-20261117')))
    output.mkdir(parents=True, exist_ok=True)
    require(' / player-hud-layout-20261117 |' in (ROOT / 'Docs/Online_RPG_Development_Roadmap.md').read_text(encoding='utf-8-sig'), 'Missing reservation')
    require(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is None, 'Preserve PIE')
    assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    bps = {p: assets.load_asset(p) for p in TARGETS}
    require(all(isinstance(bp, unreal.WidgetBlueprint) for bp in bps.values()), 'Missing HUD module')
    dirty = {p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    require(not dirty.intersection(TARGETS), 'Preserve unsaved HUD assets')
    before = {p: HELPERS['snapshot_widgets'](unreal, bp) for p, bp in bps.items()}
    graphs = {p: graph(bp) for p, bp in bps.items()}
    bindings = {p: {n: i.get_editor_property('is_variable') for n, i in infos(bp).items()} for p, bp in bps.items()}
    proof = dict(passed=False, apply=apply, targets=list(TARGETS), saved=[], graph_preserved=False, variable_bindings_preserved=False)
    if not apply:
        (output / 'ReadOnly.json').write_text(json.dumps(before, ensure_ascii=False, indent=2), encoding='utf-8')
        unreal.log('PLAYER_HUD_LAYOUT INSPECT NoAssetsSaved=1')
        return
    backup = output / 'Temp/Backups'; backup.mkdir(parents=True, exist_ok=True)
    hashes = {}
    for path, bp in bps.items():
        require_editable(bp)
        disk = ROOT / ('Content/' + path.removeprefix('/Game/') + '.uasset')
        hashes[path] = hashlib.sha256(disk.read_bytes()).hexdigest()
        if not (backup / disk.name).exists():
            shutil.copy2(disk, backup / disk.name)
    bag = assets.load_asset(BASE + 'Inventory/Projection/WBP_InventoryProjection')
    frame = props(widget(bag, 'InventorySlotFrameStyle'))['Brush']
    button_style = props(widget(bag, 'InventoryActionButtonStyle'))['WidgetStyle']
    for path in (SKILL_CELL, ITEM_CELL):
        style_cell(bps[path], frame)
    style_vitals(bps[VITALS])
    for index in range(4):
        slot(bps[SKILLS], 'SkillQuickSlot_' + str(index), dict(Padding=margin(0, 0, 8 if index < 3 else 0, 0)))
    identity = bps[IDENTITY]
    gauge = widget(identity, 'WBP_IdentityGaugeBar')
    # InterpUp keys the gauge's own scale at 1. Scale the module from its host
    # instead, so the original animation keeps its bindings and channels.
    configure(gauge, {'In Width Override': 144, 'In Height Override': 36,
              'RenderTransform': dict(scale=dict(x=1, y=1)), 'RenderTransformPivot': dict(x=.5, y=.5)})
    place(identity, 'WBP_IdentityGaugeBar', 0, 0, 144, 36)
    icon = widget(identity, 'WBP_AbilityIconSlot_RageSkill')
    configure(icon, {'In Width Override': 96, 'In Height Override': 96})
    place(identity, 'WBP_AbilityIconSlot_RageSkill', 24, 48, 96, 96)
    hud = bps[HUD]
    place(hud, 'PlayerVitals', 0, -124, 704, 26, .5, 1, .5, 1, 2)
    place(hud, 'PlayerSkillBar', -352, -40, 280, 64, .5, 1, 0, 1, 2)
    place(hud, 'PlayerIdentity', 0, -24, 144, 144, .5, 1, .5, 1, 2)
    configure(widget(hud, 'PlayerIdentity'), dict(RenderTransform=dict(scale=dict(x=2/3, y=2/3)),
              RenderTransformPivot=dict(x=.5, y=1)))
    place(hud, 'HorizontalBox_QuickSlot', 72, -40, 280, 64, .5, 1, 0, 1, 2)
    for index in range(4):
        slot(hud, 'WBP_QuickSlot' + str(index + 1), dict(Padding=margin(0, 0, 8 if index < 3 else 0, 0)))
    place(hud, 'PlayerNotice', 0, -280, 480, 48, .5, 1, .5, 1, 10)
    panel = add(hud, unreal.Image, 'CombatHUDBackground', widget(hud, 'CanvasPanel_30'), 0)
    configure(panel, dict(Brush=rounded(dict(r=.025, g=.019, b=.016, a=.82), dict(r=.38, g=.29, b=.19, a=.85)), Visibility='HitTestInvisible'))
    place(hud, 'CombatHUDBackground', 0, -20, 752, 150, .5, 1, .5, 1, -1)
    configure(widget(hud, 'RewardMailButton'), dict(WidgetStyle=button_style))
    configure(widget(hud, 'RewardMailLabel'), dict(Font=dict(size=12), ColorAndOpacity=slate(GOLD)))
    # Compile dependencies first. This never edits a Model, ViewModel or graph.
    for path, bp in bps.items():
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        require(bp.get_editor_property('status') == unreal.BlueprintStatus.BS_UP_TO_DATE, 'Compile failed: ' + path)
        require(graph(bp) == graphs[path], 'Gameplay/binding graph changed: ' + path)
        current = infos(bp)
        require(all(n in current and current[n].get_editor_property('is_variable') == flag for n, flag in bindings[path].items()), 'Variable binding changed: ' + path)
        after = HELPERS['snapshot_widgets'](unreal, bp)
        for name, old in before[path].items():
            for key in ('SlotIndex', 'bIsSkillSlot', 'InputText', 'bCanDrag'):
                if key in old['properties']:
                    require(after[name]['properties'][key] == old['properties'][key], 'Input/slot identity changed: ' + name)
    for path in TARGETS:
        disk = ROOT / ('Content/' + path.removeprefix('/Game/') + '.uasset')
        require(hashlib.sha256(disk.read_bytes()).hexdigest() == hashes[path], 'Content changed before save: ' + path)
    for path, bp in bps.items():
        after = HELPERS['snapshot_widgets'](unreal, bp)
        if plan_only:
            continue
        if before[path] != after:
            require(assets.save_loaded_asset(bp, only_if_is_dirty=False), 'Save failed: ' + path)
            proof['saved'].append(path)
    proof.update(passed=True, graph_preserved=True, variable_bindings_preserved=True, before_hashes=hashes,
        after_hashes={p: hashlib.sha256((ROOT / ('Content/' + p.removeprefix('/Game/') + '.uasset')).read_bytes()).hexdigest() for p in TARGETS})
    (output / ('Plan.json' if plan_only else 'Applied.json')).write_text(json.dumps(dict(proof=proof, operations=OPERATIONS) if plan_only else proof, ensure_ascii=False, indent=2), encoding='utf-8')
    (output / 'After.json').write_text(json.dumps({p: HELPERS['snapshot_widgets'](unreal, bp) for p, bp in bps.items()}, ensure_ascii=False, indent=2), encoding='utf-8')
    unreal.log('PLAYER_HUD_LAYOUT PASS Saved=' + str(len(proof['saved'])))


if __name__ == '__main__':
    main()
