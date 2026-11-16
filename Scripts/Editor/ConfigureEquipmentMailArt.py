"""Apply the bag's GUIParts skin to equipment and reward mail.

Run with Unreal's PythonScript commandlet. Read-only by default; the explicit
-RPGApplyEquipmentMailArt switch permits saving only the three listed WBPs.
An owned editor session may instead call main(apply_override=True) explicitly.
The output folder contains small evidence; Temp holds disposable asset backups.
"""
import copy
import hashlib
import json
import pathlib
import runpy
import shutil

import unreal
from toolset_registry.helpers import require_editable

ROOT = pathlib.Path(__file__).resolve().parents[2]
EQUIPMENT = '/Game/Blueprints/UI/Equipment/Projection/WBP_EquipmentProjection'
MAIL = '/Game/Blueprints/UI/Inventory/Projection/WBP_RewardMail'
ENTRY = '/Game/Blueprints/UI/Inventory/Projection/WBP_RewardMailEntry'
BAG = '/Game/Blueprints/UI/Inventory/Projection/WBP_InventoryProjection'
ART = '/Game/Assets/GUIParts/Gui_parts/'
HELPERS = runpy.run_path(str(ROOT / 'Scripts/Editor/ConfigureGreatSwordFourthSkillSlot.py'),
                         run_name='equipment_mail_art_helpers')
properties = lambda obj: HELPERS['object_properties'](unreal, obj)
infos = lambda bp: HELPERS['widget_infos'](unreal, bp)
snapshot = lambda bp: HELPERS['snapshot_widgets'](unreal, bp)
equivalent = HELPERS['equivalent']
tools = unreal.get_default_object(unreal.UMGToolSet)


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def merge(old, patch):
    value = copy.deepcopy(old)
    for key, item in patch.items():
        value[key] = merge(value[key], item) if isinstance(item, dict) and isinstance(value.get(key), dict) else item
    return value


def color(r, g, b, a=1):
    return dict(r=r, g=g, b=b, a=a)


def slate(r, g, b, a=1):
    return dict(specifiedColor=color(r, g, b, a), colorUseRule='UseColor_Specified')


def margin(left=0, top=0, right=0, bottom=0):
    return dict(left=left, top=top, right=right, bottom=bottom)


def brush(name, width, height, draw='Image', inset=0):
    path = ART + name
    require(unreal.load_asset(path) is not None, 'Missing art: ' + path)
    return dict(resourceObject={'refPath': path + '.' + name}, resourceName='None',
                imageSize=dict(x=width, y=height), drawAs=draw, imageType='FullColor',
                tiling='NoTile', margin=margin(inset, inset, inset, inset), tintColor=slate(1, 1, 1))


def configure(obj, patch):
    before = properties(obj)
    require(set(patch).issubset(before), 'Unknown property on ' + obj.get_name())
    values = {key: merge(before[key], value) if isinstance(value, dict) else value for key, value in patch.items()}
    require(unreal.ToolsetLibrary.set_object_properties(obj, json.dumps(values)), 'Could not style ' + obj.get_name())
    after = properties(obj)
    require(all(equivalent(after[key], value) for key, value in values.items()), 'Style readback: ' + obj.get_name())


def widget(bp, name):
    return infos(bp)[name].get_editor_property('widget')


def add(bp, cls, name, parent, index=-1):
    if name in infos(bp):
        return widget(bp, name)
    info = tools.call_method('AddWidget', (bp, cls, name, parent, index))
    return info.get_editor_property('widget')


def wrap(bp, name, wrapper_name):
    if wrapper_name in infos(bp):
        return widget(bp, wrapper_name)
    result = tools.call_method('WrapWidgets', (bp, [widget(bp, name)], unreal.Overlay))
    require(len(result) == 1, 'Wrap failed: ' + name)
    wrapper = result[0].get_editor_property('widget')
    tools.call_method('RenameWidget', (bp, wrapper, wrapper_name))
    return widget(bp, wrapper_name)


def fill(bp, name):
    configure(infos(bp)[name].get_editor_property('slot'),
              dict(HorizontalAlignment='HAlign_Fill', VerticalAlignment='VAlign_Fill'))


def style_window(bp, border_name, width, height, button_style):
    layer = wrap(bp, border_name, 'InventoryArtLayers')
    configure(layer, dict(Visibility='SelfHitTestInvisible'))
    background = add(bp, unreal.Image, 'InventoryArtBackground', layer, 0)
    configure(background, dict(Brush=brush('big_background', width, height), Visibility='HitTestInvisible'))
    fill(bp, 'InventoryArtBackground')
    configure(widget(bp, border_name), dict(Background=brush('Frame_big', width, height),
              BrushColor=color(1, 1, 1), Padding=margin(20, 20, 20, 20)))
    fill(bp, border_name)
    template = add(bp, unreal.Button, 'InventoryActionButtonStyle', layer)
    configure(template, dict(WidgetStyle=button_style, Visibility='Collapsed'))
    # Reuse the bag's authored close treatment, including hover/pressed states.
    configure(widget(bp, 'CloseWindowButton'), dict(WidgetStyle=close_style, BackgroundColor=color(1, 1, 1)))
    configure(widget(bp, 'TitleText'), dict(ColorAndOpacity=slate(.92, .77, .55), Font=dict(size=22, typefaceFontName='Bold')))


def main(apply_override=None):
    global close_style
    _, switches, params = unreal.SystemLibrary.parse_command_line(unreal.SystemLibrary.get_command_line())
    apply = 'rpgapplyequipmentmailart' in {str(s).lower() for s in switches}
    if apply_override is not None:
        apply = bool(apply_override)
    params = {str(k).lower(): str(v) for k, v in params.items()}
    output = pathlib.Path(params.get('rpgequipmentmailartproof', str(ROOT / 'Saved/BuildChecks/EquipmentMailArt-20261117')))
    output.mkdir(parents=True, exist_ok=True)
    require(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is None, 'Preserve PIE')
    require('equipment-mail-art-20261117' in (ROOT / 'Docs/Online_RPG_Development_Roadmap.md').read_text(encoding='utf-8-sig'), 'Missing reservation')
    assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    blueprints = {path: assets.load_asset(path) for path in (EQUIPMENT, MAIL, ENTRY)}
    require(all(isinstance(bp, unreal.WidgetBlueprint) for bp in blueprints.values()), 'Missing target WBP')
    dirty = {str(p.get_name()) for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    require(not dirty.intersection(blueprints), 'Preserve unsaved target assets')
    before = {path: snapshot(bp) for path, bp in blueprints.items()}
    (output / ('BeforeApply.json' if apply else 'Inspect.json')).write_text(json.dumps(before, ensure_ascii=False, indent=2), encoding='utf-8')
    if not apply:
        unreal.log('EQUIPMENT_MAIL_ART INSPECT NoAssetsSaved=1')
        return
    eq, mail, entry = [blueprints[p] for p in (EQUIPMENT, MAIL, ENTRY)]
    require(properties(widget(eq, 'RootSizeBox'))['WidthOverride'] == 560, 'Changed equipment dimensions')
    require(properties(widget(mail, 'MailWindowRoot'))['WidthOverride'] == 720, 'Changed mail dimensions')
    bag = assets.load_asset(BAG)
    button_style = properties(widget(bag, 'InventoryActionButtonStyle'))['WidgetStyle']
    close_style = properties(widget(bag, 'CloseWindowButton'))['WidgetStyle']
    slot_brush = properties(widget(bag, 'InventorySlotFrameStyle'))['Brush']
    bag_disk = ROOT / ('Content/' + BAG.removeprefix('/Game/') + '.uasset')
    bag_hash = hashlib.sha256(bag_disk.read_bytes()).hexdigest()
    bindings = {p: {n: i.get_editor_property('is_variable') for n, i in infos(bp).items()} for p, bp in blueprints.items()}
    backup = output / 'Temp/Backups'
    backup.mkdir(parents=True, exist_ok=True)
    hashes = {}
    for path, bp in blueprints.items():
        require_editable(bp)
        disk = ROOT / ('Content/' + path.removeprefix('/Game/') + '.uasset')
        hashes[path] = hashlib.sha256(disk.read_bytes()).hexdigest()
        if not (backup / disk.name).exists():
            shutil.copy2(disk, backup / disk.name)

    style_window(eq, 'WindowBorder', 560, 620, button_style)
    configure(infos(eq)['HorizontalBox_0'].get_editor_property('slot'), dict(Padding=margin(64, 8, 8, 12)))
    configure(widget(eq, 'CloseWindowLabel'), dict(ColorAndOpacity=slate(.98, .87, .7)))
    configure(widget(eq, 'EquipmentInteractionHint'), dict(ColorAndOpacity=slate(.65, .6, .52), Font=dict(size=10)))
    configure(widget(eq, 'LoadStateText'), dict(ColorAndOpacity=slate(.65, .6, .52), Font=dict(size=10)))
    template = add(eq, unreal.Image, 'InventorySlotFrameStyle', widget(eq, 'InventoryArtLayers'))
    configure(template, dict(Brush=slot_brush, Visibility='Collapsed'))
    for side in ('Left', 'Right'):
        for index in range(7):
            configure(widget(eq, f'{side}PreviewFrame{index}'), dict(Background=slot_brush, BrushColor=color(1, 1, 1)))
            configure(widget(eq, f'{side}PreviewLabel{index}'), dict(ColorAndOpacity=slate(.83, .72, .56)))
    # Decoration only: keep the truthful placeholder and the original layout.
    silhouette_layers = wrap(eq, 'PreviewText', 'EquipmentSilhouetteLayers')
    silhouette = add(eq, unreal.Image, 'EquipmentSilhouette', silhouette_layers, 0)
    configure(silhouette, dict(Brush=brush('warrior_silhouette_man', 220, 400), ColorAndOpacity=color(.6, .44, .28, .38), Visibility='HitTestInvisible'))
    fill(eq, 'EquipmentSilhouette')
    configure(widget(eq, 'PreviewBorder'), dict(BrushColor=color(.012, .01, .008, .9)))
    configure(widget(eq, 'PreviewText'), dict(ColorAndOpacity=slate(.65, .58, .47), Font=dict(size=12)))

    style_window(mail, 'MailWindowBackground', 720, 540, button_style)
    configure(widget(mail, 'CloseButtonText'), dict(ColorAndOpacity=slate(.98, .87, .7)))
    configure(widget(mail, 'MailHeaderBackground'), dict(BrushColor=color(0, 0, 0, 0), Padding=margin(64, 8, 8, 12)))
    for name in ('MailHintText', 'LoadStateText'):
        configure(widget(mail, name), dict(ColorAndOpacity=slate(.65, .6, .52), Font=dict(size=10)))
    plain = dict(resourceObject='None', resourceName='None', drawAs='Image', imageType='NoImage',
                 margin=margin(), tintColor=slate(1, 1, 1))
    configure(widget(mail, 'MailListBackground'), dict(Background=plain, BrushColor=color(.012, .01, .008, .9)))
    configure(widget(mail, 'MailScroll'), dict(ScrollbarThickness=dict(x=5, y=5)))
    row_frame = copy.deepcopy(slot_brush)
    row_frame.update(drawAs='Box', margin=margin(.2, .2, .2, .2))
    configure(widget(entry, 'MailEntryBackground'), dict(Background=row_frame, BrushColor=color(1, 1, 1), Padding=margin(14, 10, 14, 10)))
    configure(widget(entry, 'ClaimButton'), dict(WidgetStyle=button_style, BackgroundColor=color(1, 1, 1)))
    configure(widget(entry, 'ClaimLabelPadding'), dict(BrushColor=color(0, 0, 0, 0), Padding=margin(0, 0, 0, 0)))
    configure(widget(entry, 'ClaimButtonText'), dict(ColorAndOpacity=slate(.92, .81, .64), Font=dict(size=12)))
    configure(widget(entry, 'MailItemName'), dict(ColorAndOpacity=slate(.92, .81, .64)))
    configure(widget(entry, 'MailClaimFeedback'), dict(ColorAndOpacity=slate(.7, .62, .5)))

    after = {}
    allowed_parent_changes = {EQUIPMENT: {'WindowBorder', 'PreviewText'}, MAIL: {'MailWindowBackground'}, ENTRY: set()}
    for path, bp in blueprints.items():
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        require(bp.get_editor_property('status') == unreal.BlueprintStatus.BS_UP_TO_DATE, 'Compile failed: ' + path)
        current = infos(bp)
        after[path] = snapshot(bp)
        for name, original in before[path].items():
            require(name in current, 'Removed existing widget: ' + name)
            require(current[name].get_editor_property('is_variable') == bindings[path][name], 'Changed binding: ' + name)
            require(after[path][name]['class'] == original['class'], 'Changed widget class: ' + name)
            if name not in allowed_parent_changes[path]:
                require(after[path][name]['parent'] == original['parent'], 'Changed parent: ' + name)
            for key in ('Text', 'Visibility', 'IsEnabled'):
                if key in original['properties']:
                    require(equivalent(after[path][name]['properties'][key], original['properties'][key]), 'Changed behavior/text: ' + name + '.' + key)
        require(hashlib.sha256((ROOT / ('Content/' + path.removeprefix('/Game/') + '.uasset')).read_bytes()).hexdigest() == hashes[path], 'Content changed before save')
    for path, bp in blueprints.items():
        require(assets.save_loaded_asset(bp, only_if_is_dirty=False), 'Save failed: ' + path)
    require(not set(blueprints).intersection(str(p.get_name()) for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()), 'Dirty after save')
    require(hashlib.sha256(bag_disk.read_bytes()).hexdigest() == bag_hash, 'Bag changed')
    (output / 'Authoring.json').write_text(json.dumps(dict(passed=True, saved=list(blueprints), before_hashes=hashes,
        preserved_bindings=True, preserved_text_and_visibility=True, bag_unchanged=True, widgets=after), ensure_ascii=False, indent=2), encoding='utf-8')
    unreal.log('EQUIPMENT_MAIL_ART PASS Saved=3 BindingsPreserved=1 BagUnchanged=1')


if __name__ == '__main__':
    main()
