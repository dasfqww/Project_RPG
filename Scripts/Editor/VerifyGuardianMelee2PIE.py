"""Own-editor PIE: activate the authored Guardian Melee2, observe real hit/physics.

No synthetic damage event, replacement grant, gameplay-code change, or asset save.
Launch only in a disposable editor on /Engine/Maps/Entry with -RPGGuardianMelee2Live.
"""
import json
import time
from pathlib import Path
import unreal

TASK = Path(unreal.Paths.project_dir()).resolve()/'Saved/CombatGuardianMelee2Live'
TASK.mkdir(parents=True, exist_ok=True)
STATE = dict(phase='startup', deadline=time.monotonic()+150, callback=None, owns_pie=False)
RESULT = dict(success=False, syntheticDamage=False, replacementGrant=False, savedAssets=[], samples=[])
NPC_PATH = '/Game/Blueprints/Character/NPC/Gruntling/Guardian/BP_Gruntling_Guardian.BP_Gruntling_Guardian_C'
GA_PATH = '/Game/Blueprints/Character/NPC/Gruntling/GameplayAbility/GA_Guardian_Melee2.GA_Guardian_Melee2_C'


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def hp(actor):
    return float(actor.get_editor_property('rpg_attribute_set').get_editor_property('current_health').get_editor_property('current_value'))


def phase(control):
    value = control.get_airborne_state().get_editor_property('phase')
    return int(value.value)


def spawn(world, cls, location, rotation=unreal.Rotator()):
    transform = unreal.Transform(location=location, rotation=rotation)
    library=unreal.get_default_object(unreal.GameplayStatics)
    actor=library.call_method('BeginDeferredActorSpawnFromClass',args=(world,cls,transform,unreal.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN))
    require(actor is not None, 'Spawn failed: '+str(cls))
    return library.call_method('FinishSpawningActor',args=(actor,transform))


def finish():
    unreal.unregister_slate_post_tick_callback(STATE['callback'])
    (TASK/'Result.json').write_text(json.dumps(RESULT,ensure_ascii=False,indent=2),encoding='utf-8')
    (unreal.log if RESULT['success'] else unreal.log_error)('GUARDIAN_MELEE2_LIVE_RESULT '+json.dumps({k:v for k,v in RESULT.items() if k!='samples'}))
    unreal.SystemLibrary.quit_editor()


def tick(_delta):
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    try:
        require(time.monotonic()<STATE['deadline'], 'Timeout in '+STATE['phase'])
        if STATE['phase']=='startup':
            if unreal.AssetRegistryHelpers.get_asset_registry().is_loading_assets():
                return
            require('-RPGGuardianMelee2Live' in unreal.SystemLibrary.get_command_line(), 'Explicit own-editor flag required')
            require(not level.is_in_play_in_editor(), 'Do not take over an existing PIE')
            world=editor.get_editor_world()
            require(world is not None and world.get_name()=='Entry', 'Disposable Entry map required')
            # This disposable world uses the engine game mode; explicit test
            # actors below avoid content-dependent lobby/player startup.
            mode=unreal.load_class(None,'/Script/Engine.GameModeBase')
            world.get_world_settings().set_editor_property('default_game_mode',mode)
            level.editor_request_begin_play()
            STATE.update(phase='starting',owns_pie=True)
        elif STATE['phase']=='starting':
            world=editor.get_game_world()
            if world is None:
                return
            controller=STATE.get('receiver_controller') or unreal.GameplayStatics.get_player_controller(world,0)
            player=controller.get_controlled_pawn() if controller else None
            if player is None or controller is None:
                return
            if player.get_class().get_path_name()!='/Script/Project_RPG.RPGStunTestCharacter':
                require('receiver_controller' not in STATE,'Native test receiver was not spawned')
                RESULT['initialPawnClass']=player.get_class().get_path_name()
                RESULT['initialControllerClass']=controller.get_class().get_path_name()
                # Configure the own PIE game-mode INSTANCE, then let the engine
                # create/initialize a genuine local player and its pawn.
                mode=unreal.GameplayStatics.get_game_mode(world)
                mode.set_editor_property('default_pawn_class',unreal.load_class(None,'/Script/Project_RPG.RPGStunTestCharacter'))
                mode.set_editor_property('player_controller_class',unreal.load_class(None,'/Script/Project_RPG.RPGPlayerController'))
                receiver=unreal.GameplayStatics.create_player(world,-1,True)
                require(receiver is not None,'Engine local-player creation failed')
                STATE['receiver_controller']=receiver
                player.destroy_actor()
                return
            require(controller.is_local_controller() and player.is_locally_controlled(),'Receiver must use the own PIE local player')
            RESULT['receiverClass']=player.get_class().get_path_name()
            RESULT['receiverControllerClass']=controller.get_class().get_path_name()
            # Player-controlled airborne requires the real movement security
            # component, just as the production player constructor supplies it.
            security=player.call_method('AddComponentByClass',args=(unreal.RPGSecurityValidationComponent,False,unreal.Transform(),False))
            require(security is not None,'Missing normal player movement security')
            RESULT['receiverSecurityClass']=security.get_class().get_path_name()
            attrs=player.get_editor_property('rpg_attribute_set')
            for field,value in [('max_health',1000.),('current_health',1000.)]:
                data=attrs.get_editor_property(field)
                data.set_editor_property('base_value',value)
                data.set_editor_property('current_value',value)
                attrs.set_editor_property(field,data)
            require(hp(player)==1000.,'Fixture attribute initialization failed')
            controller.set_ignore_move_input(True)
            controller.set_ignore_look_input(True)
            pos=player.get_actor_location()
            half=player.get_editor_property('capsule_component').get_scaled_capsule_half_height()
            floor=spawn(world,unreal.StaticMeshActor,unreal.Vector(pos.x,pos.y,pos.z-half-50.))
            mesh=floor.get_editor_property('static_mesh_component')
            mesh.set_mobility(unreal.ComponentMobility.MOVABLE)
            mesh.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
            mesh.set_collision_profile_name('BlockAll')
            floor.set_actor_scale3d(unreal.Vector(40.,40.,1.))
            npc=spawn(world,unreal.load_class(None,NPC_PATH),unreal.Vector(pos.x-160.,pos.y,pos.z+20.))
            control=player.get_component_by_class(unreal.RPGCombatControlComponent)
            STATE.update(phase='prepare',world=world,player=player,npc=npc,control=control,security=security,prepare_at=time.monotonic())
        elif STATE['phase']=='prepare':
            npc=STATE['npc']; ai=npc.get_controller()
            if ai:
                brain=ai.get_editor_property('brain_component')
                if brain:
                    brain.stop_logic('Own Guardian Melee2 test')
                ai.stop_movement()
            if not npc.is_npc_start_up_data_ready():
                return
            combat=npc.get_component_by_class(unreal.PawnCombatComponent)
            if combat is None or combat.get_character_current_equipped_weapon() is None:
                return
            anim=npc.get_editor_property('mesh').get_anim_instance()
            npc.get_editor_property('mesh').set_editor_property('visibility_based_anim_tick_option',unreal.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES)
            if anim is None or anim.is_any_montage_playing() or time.monotonic()-STATE['prepare_at']<3.:
                return
            player=STATE['player']; p=player.get_actor_location()
            npc.set_actor_location(unreal.Vector(p.x-160.,p.y,p.z+6.),False,True)
            npc.set_actor_rotation(unreal.Rotator(0.,0.,0.),True)
            require(phase(STATE['control'])==0 and hp(player)==1000.,'Unexpected pre-attack control/damage')
            asc=npc.get_component_by_class(unreal.RPGAbilitySystemComponent)
            accepted=asc.try_activate_ability_by_class(unreal.load_class(None,GA_PATH),False)
            require(accepted,'Original Melee2 grant activation rejected')
            RESULT.update(activationAccepted=accepted,attacker=npc.get_class().get_path_name(),originalAbility=GA_PATH,
                          initialHealth=hp(player),initialZ=p.z,maxZ=p.z,observedPhases=[])
            STATE.update(phase='observe',attack_at=time.monotonic(),last_sample=0.,anim=anim,had_hit=False)
        elif STATE['phase']=='observe':
            player=STATE['player']; c=STATE['control']; current=phase(c); now=time.monotonic()
            pos=player.get_actor_location(); health=hp(player)
            RESULT['lastAirborneEndReason']=str(c.get_airborne_state().get_editor_property('end_reason'))
            RESULT['maxZ']=max(RESULT['maxZ'],pos.z)
            if current not in RESULT['observedPhases']:
                RESULT['observedPhases'].append(current)
            if now-STATE['last_sample']>.05:
                montage=STATE['anim'].get_current_active_montage()
                RESULT['samples'].append(dict(seconds=now-STATE['attack_at'],health=health,phase=current,x=pos.x,y=pos.y,z=pos.z,
                                               attackerMontage=montage.get_path_name() if montage else None))
                STATE['last_sample']=now
            if current==1:
                STATE['had_hit']=True
                snapshot=c.get_airborne_state()
                for field,expected in [('distance',240.),('height',180.),('down_duration',.6),('get_up_duration',.8)]:
                    require(abs(float(snapshot.get_editor_property(field))-expected)<.001,'Original grant lost authored '+field)
                RESULT['authoredSettingsObserved']=True
            if STATE['had_hit'] and current==0:
                require(all(x in RESULT['observedPhases'] for x in [1,2,3]),'Missing airborne/down/getup transition')
                require(0.<health<RESULT['initialHealth'],'Expected real nonlethal weapon damage')
                require(RESULT['maxZ']>RESULT['initialZ']+50.,'No physical airborne rise')
                require(int(c.get_airborne_state().get_editor_property('end_reason').value)==1,'Recovery did not complete normally')
                require(STATE['security'].get_total_violation_count()==0,'Movement security reported a violation')
                require(not STATE['security'].is_movement_authorization_active(),'Movement authorization leaked after recovery')
                RESULT.update(success=True,finalHealth=health,finalZ=pos.z,cleanRecovery=True,
                              endReason=str(c.get_airborne_state().get_editor_property('end_reason')),securityViolations=0,movementAuthorizationReleased=True)
                level.editor_request_end_play(); STATE['phase']='ending'
            elif now-STATE['attack_at']>12.:
                raise RuntimeError('No complete natural hit/recovery; phases='+str(RESULT['observedPhases']))
        elif STATE['phase'] in ('ending','failed'):
            if level.is_in_play_in_editor() or editor.get_game_world() is not None:
                return
            finish()
    except Exception as error:
        RESULT.update(success=False,error=str(error),failedPhase=STATE['phase'])
        STATE['phase']='failed'
        if STATE['owns_pie'] and level.is_in_play_in_editor():
            level.editor_request_end_play()
        else:
            finish()


STATE['callback']=unreal.register_slate_post_tick_callback(tick)
