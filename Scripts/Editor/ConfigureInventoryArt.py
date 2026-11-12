"""Author the bag's existing GUIParts skin through ProgrammaticToolset.

Read-only by default. Execute with execute_tool_script after its environment and
the called tools' schemas have been inspected. Set APPLY=True explicitly to
compile/save only WBP_InventoryProjection. Back up that package before applying.
Collapsed style templates also supply art to the native bag cells and buttons;
their texture references are serialized into the WBP for cooking.
"""
import copy
import json

APPLY = False
PACKAGE = '/Game/Blueprints/UI/Inventory/Projection/WBP_InventoryProjection'
BP = {'refPath': PACKAGE + '.WBP_InventoryProjection'}
ART = '/Game/Assets/GUIParts/Gui_parts/'


def widgets():
    data = execute_tool('UMGToolSet.UMGToolSet.GetWidgets',
                        json.dumps({'widgetBlueprint': BP}))['returnValue']
    return {w['widgetName']: w for w in data['widgets']
            if isinstance(w['widget'], dict)}


def properties(obj):
    names = json.loads(execute_tool('editor_toolset.toolsets.object.ObjectTools.list_properties',
                                   json.dumps({'instance': obj}))['returnValue'])
    return json.loads(execute_tool('editor_toolset.toolsets.object.ObjectTools.get_properties',
                                  json.dumps({'instance': obj, 'properties': list(names)}))['returnValue'])


def merge(old, patch):
    result = copy.deepcopy(old)
    for key, value in patch.items():
        result[key] = merge(result[key], value) if isinstance(value, dict) and isinstance(result.get(key), dict) else value
    return result


def configure(obj, patch):
    old = properties(obj)  # Discover and read exact names before every write.
    if not set(patch).issubset(old):
        raise RuntimeError('Unknown property: ' + str(set(patch) - set(old)))
    values = {key: merge(old[key], value) if isinstance(value, dict) and isinstance(old[key], dict)
              else value for key, value in patch.items()}
    ok = execute_tool('editor_toolset.toolsets.object.ObjectTools.set_properties',
                      json.dumps({'instance': obj, 'values': json.dumps(values)}))['returnValue']
    if not ok:
        raise RuntimeError('Could not configure ' + str(obj))
    return properties(obj)


def add(cls, name, parent, index=-1):
    return execute_tool('UMGToolSet.UMGToolSet.AddWidget', json.dumps({
        'widgetBlueprint': BP, 'widgetClass': {'refPath': '/Script/UMG.' + cls},
        'widgetDisplayName': name, 'parentWidget': parent, 'childIndex': index}))['returnValue']


def wrap(widget):
    return execute_tool('UMGToolSet.UMGToolSet.WrapWidgets', json.dumps({
        'widgetBlueprint': BP, 'widgets': [widget],
        'wrapperClass': {'refPath': '/Script/UMG.Overlay'}}))['returnValue'][0]


def rename(widget, name):
    return execute_tool('UMGToolSet.UMGToolSet.RenameWidget', json.dumps({
        'widgetBlueprint': BP, 'widget': widget, 'newDisplayName': name}))['returnValue']


def colour(r, g, b, a=1):
    return dict(r=r, g=g, b=b, a=a)


def slate(r, g, b, a=1):
    return dict(specifiedColor=colour(r, g, b, a), colorUseRule='UseColor_Specified')


def margin(left=0, top=0, right=0, bottom=0):
    return dict(left=left, top=top, right=right, bottom=bottom)


def brush(name, width, height, draw='Box', inset=.12, tint=None):
    return dict(resourceObject={'refPath': ART + name + '.' + name}, resourceName='None',
                imageSize=dict(x=width, y=height), drawAs=draw, imageType='FullColor',
                tiling='NoTile', margin=margin(inset, inset, inset, inset),
                tintColor=tint or slate(1, 1, 1))


def fill(info):
    configure(info['slot'], dict(horizontalAlignment='HAlign_Fill', verticalAlignment='VAlign_Fill'))


def run():
    pie = execute_tool('EditorToolset.EditorAppToolset.IsPIERunning', '{}')['returnValue']
    if pie:
        raise RuntimeError('Finish PIE before authoring')
    before = widgets()
    required = {'RootSizeBox', 'WindowBorder', 'MainColumn', 'TitleText', 'CloseWindowButton',
                'LoadStateText', 'InventoryGrid', 'InventoryScroll', 'InventoryCapacityText', 'CommandResultText'}
    if not required.issubset(before):
        raise RuntimeError('Review changed bag bindings')
    size = properties(before['RootSizeBox']['widget'])
    if (size['widthOverride'], size['heightOverride']) != (720, 640):
        raise RuntimeError('Review changed bag size')
    if not APPLY:
        return dict(apply=False, package=PACKAGE, widget_names=list(before), size=[720, 640])
    dirty = execute_tool('editor_toolset.toolsets.asset.AssetTools.is_dirty',
                         json.dumps({'asset_path': PACKAGE}))['returnValue']
    if dirty:
        raise RuntimeError('Preserve unsaved bag edits')

    if 'InventoryArtLayers' not in before:
        wrapped = wrap(before['WindowBorder']['widget'])
        rename(wrapped['widget'], 'InventoryArtLayers')
    info = widgets()
    root = info['InventoryArtLayers']['widget']
    configure(root, dict(visibility='SelfHitTestInvisible'))
    background = info.get('InventoryArtBackground') or add('Image', 'InventoryArtBackground', root, 0)
    configure(background['widget'], dict(brush=brush('big_background', 720, 640, 'Image', 0),
                                         visibility='HitTestInvisible'))
    fill(background)
    configure(info['WindowBorder']['widget'], dict(
        background=brush('Frame_big', 720, 640, 'Image', 0), brushColor=colour(1, 1, 1),
        padding=margin(20, 20, 20, 20)))
    fill(info['WindowBorder'])

    slot_template = info.get('InventorySlotFrameStyle') or add('Image', 'InventorySlotFrameStyle', root)
    # This existing UI texture has a transparent center. The GUIParts square
    # has an opaque center and produces seams when tiled as a Slate border.
    slot_brush = brush('Mini_frame0', 64, 64, 'Image', 0, tint=slate(.82, .62, .43))
    slot_brush['resourceObject'] = {'refPath': '/Game/Assets/Textures/UI/T_UI_Item_Border.T_UI_Item_Border'}
    configure(slot_template['widget'], dict(brush=slot_brush, visibility='Collapsed'))
    button_template = info.get('InventoryActionButtonStyle') or add('Button', 'InventoryActionButtonStyle', root)
    button_style = properties(button_template['widget'])['widgetStyle']
    button_style.update(dict(
        normal=merge(button_style['normal'], brush('button', 160, 44, inset=.16)),
        hovered=merge(button_style['hovered'], brush('button', 160, 44, inset=.16, tint=slate(1.45, 1.3, 1.15))),
        pressed=merge(button_style['pressed'], brush('button', 160, 44, inset=.16, tint=slate(.65, .65, .65))),
        disabled=merge(button_style['disabled'], brush('button', 160, 44, inset=.16, tint=slate(.5, .5, .5))),
        normalForeground=slate(.92, .81, .64), hoveredForeground=slate(1, .93, .77),
        pressedForeground=slate(.95, .85, .7), disabledForeground=slate(.58, .52, .44),
        normalPadding=margin(14, 6, 14, 6), pressedPadding=margin(14, 7, 14, 5)))
    configure(button_template['widget'], dict(widgetStyle=button_style, visibility='Collapsed'))
    close_style = copy.deepcopy(button_style)
    for state, tint in (('normal', slate(1, 1, 1)), ('hovered', slate(1.5, 1.3, 1.2)),
                        ('pressed', slate(.65, .65, .65)), ('disabled', slate(.5, .5, .5))):
        close_style[state] = merge(close_style[state], brush('button2_ready_on', 32, 32, inset=.2, tint=tint))
    close_style['normalPadding'] = margin()
    close_style['pressedPadding'] = margin(0, 1, 0, -1)
    configure(info['CloseWindowButton']['widget'], dict(widgetStyle=close_style, backgroundColor=colour(1, 1, 1)))
    configure(info['CloseWindowLabel']['widget'], dict(colorAndOpacity=slate(.98, .87, .7)))
    configure(info['TitleText']['widget'], dict(colorAndOpacity=slate(.92, .77, .55),
              font=dict(size=22, typefaceFontName='Bold'), shadowColorAndOpacity=colour(0, 0, 0, 1)))
    configure(info['HorizontalBox_0']['slot'], dict(padding=margin(72, 8, 8, 12)))
    configure(info['InventoryHeaderText']['widget'], dict(text='소지품', colorAndOpacity=slate(.83, .72, .56)))
    for name in ('InventoryInteractionHint', 'LoadStateText'):
        configure(info[name]['widget'], dict(colorAndOpacity=slate(.65, .6, .52), font=dict(size=10)))
    configure(info['InventoryScroll']['widget'], dict(scrollbarThickness=dict(x=5, y=5)))
    compiled = execute_tool('UMGToolSet.UMGToolSet.CompileWidgetBlueprint',
                            json.dumps({'widgetBlueprint': BP}))['returnValue']
    if not compiled:
        raise RuntimeError('Bag compile failed; package not saved')
    after = widgets()
    if not set(before).issubset(after):
        raise RuntimeError('A preexisting widget was removed')
    for name in before:
        if before[name]['bIsVariable'] != after[name]['bIsVariable']:
            raise RuntimeError('Variable binding changed: ' + name)
        if name != 'WindowBorder' and before[name]['parent'] != after[name]['parent']:
            raise RuntimeError('Unexpected hierarchy change: ' + name)
    saved = execute_tool('editor_toolset.toolsets.asset.AssetTools.save_assets',
                         json.dumps({'asset_paths': [PACKAGE]}))['returnValue']
    clean = not execute_tool('editor_toolset.toolsets.asset.AssetTools.is_dirty',
                             json.dumps({'asset_path': PACKAGE}))['returnValue']
    if not saved or not clean:
        raise RuntimeError('Bag save failed')
    return dict(apply=True, package=PACKAGE, compiled=compiled, saved=saved, clean=clean,
                widget_names=list(after), preserved_bindings=True, size=[720, 640],
                art='GUIParts dark background / bronze frames / red action buttons')
