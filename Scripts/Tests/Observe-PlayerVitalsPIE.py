"""Opt-in, read-only observer of the authored Vitals widget in a real PIE world.

The Slate callback belongs only to this verification process. Product widgets do
not acquire a tick, timer, input hook, attribute setter or replacement source.
"""
import json
import math
import pathlib
import time

import unreal


tokens, switches, params = unreal.SystemLibrary.parse_command_line(unreal.SystemLibrary.get_command_line())
params = {str(k).lower(): str(v) for k, v in params.items()}
if not any(str(p).lower() == 'rpgobserveplayervitalspie' for p in switches):
    raise RuntimeError('Explicit owned Vitals PIE flag required')
output = pathlib.Path(params['rpgvitalspieoutput']).resolve()
if output.name != 'Vitals.json' or output.parent.parent.name != 'TrainingPlaytest':
    raise RuntimeError('Output must belong to this training verification run')

state = {'started': True, 'failure': None, 'samples': [], 'read_only': True,
         'pie_requested': False, 'pie_running': False, 'pie_stopped': False,
         'physical_input': False, 'last_status': 'WaitingForPIE'}
next_read = 0.0
last_key = None


def save():
    staging = output.with_suffix('.tmp')
    staging.write_text(json.dumps(state, indent=2), encoding='utf-8')
    try:
        staging.replace(output)
    except PermissionError:
        # A Windows reader can briefly hold the old JSON without delete sharing.
        # Keep that complete sample; the next callback retries publication.
        pass


def prop(obj, name):
    return obj.get_editor_property(name)


def inside(obj, parent):
    outer = obj.get_outer()
    while outer is not None:
        if outer == parent:
            return True
        outer = outer.get_outer()
    return False


def geometry(widget):
    cached = unreal.SlateLibrary.get_local_size(widget.get_cached_geometry())
    desired = widget.get_desired_size()
    result = {'name': widget.get_name(), 'cached': [cached.x, cached.y], 'desired': [desired.x, desired.y],
              'visibility': str(widget.get_visibility()), 'opacity': float(prop(widget, 'render_opacity'))}
    slot = prop(widget, 'slot')
    if isinstance(slot, unreal.CanvasPanelSlot):
        result['layout'] = prop(slot, 'layout_data').export_text()
        result['auto_size'] = bool(prop(slot, 'auto_size'))
    return result


def observe(delta):
    global next_read, last_key
    now = time.monotonic()
    if now < next_read or state['failure']:
        return
    next_read = now + 0.15
    try:
        editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        state['pie_running'] = editor.is_in_play_in_editor()
        if state['pie_running']:
            state['pie_requested'] = True
            state['play_mode'] = 'InEditorFloating'
        if (output.parent / 'vitals-stop.ready').exists():
            if state['pie_running']:
                editor.editor_request_end_play()
            else:
                state['pie_stopped'] = True
            save()
            return
        worlds = unreal.EditorLevelLibrary.get_pie_worlds(False)
        if state['pie_running'] and state['last_status'] == 'WaitingForPIE':
            state['last_status'] = 'PIERunning'
            save()
        for world in worlds:
            if 'L_ProgressionTraining' not in world.get_path_name():
                continue
            pc = unreal.GameplayStatics.get_player_controller(world, 0)
            pawn = pc.get_controlled_pawn() if pc else None
            if not isinstance(pawn, unreal.RPGPlayer):
                continue
            modules = [w for w in unreal.WidgetLibrary.get_all_widgets_of_class(world, unreal.RPGPlayerVitalsWidget, False)
                       if w.get_owning_player() == pc and w.get_class().get_name() == 'WBP_PlayerVitals_C']
            if not modules:
                state['last_status'] = 'WaitingForAuthoredVitals'
                save()
                continue
            if len(modules) != 1:
                raise RuntimeError('Expected exactly one authored Vitals module')
            module = modules[0]
            parents = [w for w in unreal.WidgetLibrary.get_all_widgets_of_class(world, unreal.UserWidget, True)
                       if w.get_class().get_name() == 'WBP_PlayerHUD_C' and inside(module, w)]
            if len(parents) != 1 or not parents[0].is_in_viewport():
                raise RuntimeError('Vitals must be inside the displayed original PlayerHUD')
            hud = pc.get_hud()
            owner = None
            if isinstance(hud, unreal.RPGHUD) and hasattr(hud, 'get_player_hud_widget'):
                if hud.get_player_hud_widget() != parents[0] or parents[0].get_owning_player() != pc:
                    raise RuntimeError('Displayed PlayerHUD must belong to its native HUD and local player')
                owner = hud.get_path_name()
            shown = module.get_displayed_vitals()
            if not prop(shown, 'has_source'):
                state['last_status'] = 'WaitingForVitalsSource'
                save()
                continue
            # The authored character exposes its own GAS attribute set. The
            # ASC's private spawned-set list is not part of Python's API.
            stats = prop(pawn, 'rpg_attribute_set')
            if not isinstance(stats, unreal.RPGAttributeSet) or stats.get_outer() != pawn:
                raise RuntimeError('Pawn GAS attribute set missing or has another owner')
            values = {key: float(prop(prop(stats, name), 'current_value')) for key, name in
                      (('health', 'current_health'), ('max_health', 'max_health'),
                       ('mana', 'current_mana'), ('max_mana', 'max_mana'))}
            for key, value in values.items():
                if not math.isfinite(value) or not math.isclose(float(prop(shown, key)), value, abs_tol=0.01):
                    raise RuntimeError('Displayed snapshot differs from actual GAS: '+key)
            controls = {}
            for kind in ('health', 'mana'):
                current, maximum = values[kind], values['max_'+kind]
                expected = current / maximum if maximum > 0 else 0.0
                bar, label = prop(module, kind+'_bar'), prop(module, kind+'_text')
                timer_path = bar.get_outer().get_path_name()+'.'+kind.title()+'_ProgressTimeText'
                timer = unreal.find_object(None, timer_path)
                if not isinstance(timer, unreal.TextBlock) or timer.get_visibility() != unreal.SlateVisibility.COLLAPSED:
                    raise RuntimeError('Unused resource timer is visible: '+kind)
                fraction, text = float(prop(bar, 'percent')), str(label.get_text())
                if not math.isclose(fraction, expected, abs_tol=0.0001):
                    raise RuntimeError('Actual progress bar differs from GAS: '+kind)
                expected_text = f'{current:.0f}/{maximum:.0f}'
                if text != expected_text:
                    raise RuntimeError('Actual text differs from GAS: '+kind+' '+text)
                size = unreal.SlateLibrary.get_local_size(bar.get_cached_geometry())
                controls[kind] = {'text': text, 'percent': fraction, 'width': size.x, 'height': size.y,
                                  'path': bar.get_path_name(), 'visible': bar.is_visible()}
                chain, current_widget = [], bar
                while current_widget is not None:
                    chain.append(geometry(current_widget))
                    current_widget = current_widget.get_parent()
                controls[kind]['ancestors'] = chain
            parent_size = unreal.SlateLibrary.get_local_size(parents[0].get_cached_geometry())
            module_size = unreal.SlateLibrary.get_local_size(module.get_cached_geometry())
            viewport_size = unreal.SlateLibrary.get_local_size(unreal.WidgetLayoutLibrary.get_viewport_widget_geometry(world))
            player_size = unreal.SlateLibrary.get_local_size(unreal.WidgetLayoutLibrary.get_player_screen_widget_geometry(pc))
            sample = {'elapsed': round(now, 3), 'world': world.get_path_name(), 'pawn': pawn.get_name(),
                      'local_role': str(pawn.get_local_role()),
                      'module': module.get_path_name(), 'parent': parents[0].get_path_name(),
                      'hud_owner': owner,
                      'visible': module.is_visible(), 'values': values, 'controls': controls,
                      'template_timers_hidden': True,
                      'parent_visible': parents[0].is_visible(),
                      'viewport_size': [viewport_size.x, viewport_size.y],
                      'player_screen_size': [player_size.x, player_size.y],
                      'parent_viewport_anchors': parents[0].get_anchors_in_viewport().export_text(),
                      'module_layout': geometry(module), 'host_canvas': geometry(module.get_parent()),
                      'parent_size': [parent_size.x, parent_size.y], 'module_size': [module_size.x, module_size.y]}
            key = json.dumps({'values': values, 'controls': controls, 'visible': sample['visible']}, sort_keys=True)
            if key != last_key and len(state['samples']) < 60:
                state['samples'].append(sample)
                last_key = key
            state['last_status'] = 'ObservedAuthoredVitals'
            save()
    except Exception as error:
        state['failure'] = str(error)
        save()
        unreal.log_error('PLAYER_VITALS_PIE_OBSERVER '+str(error))


save()
callback = unreal.register_slate_post_tick_callback(observe)
unreal.log('PLAYER_VITALS_PIE_OBSERVER STARTED ReadOnly=1')
