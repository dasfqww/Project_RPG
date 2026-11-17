"""M07 owned staging maps only. Read-only unless -RPGCreateChapterSevenContent.
Never edits the existing boss map, boss mode, rewards, player or M03-M06 maps.
"""
import json
import os
from pathlib import Path
import unreal as u
ROOT='/Game/System/Dungeons/Progression/'
BOSS='/Game/Maps/Progression/L_ChapterOneBoss'
RETURN='/Game/Maps/Progression/L_ChapterOneReturn'
MODE='/Game/Blueprints/GameMode/Progression/BP_ChapterOneReturnMode'
BMODE='/Game/Blueprints/GameMode/Progression/BP_ChapterOneBossMode'
SERVER=ROOT+'DA_DungeonServer_FirstBoss'
SELECT=ROOT+'DA_Dungeon_ChapterOneReturn'
TAG='CH01.M07.v1'
def need(v,m):
    if not v: raise RuntimeError(m)
def main():
    out=Path(os.environ.get('RPG_M07_REPORT',''));need(out.is_absolute(),'Set RPG_M07_REPORT')
    report={'passed':False,'saved':[]}
    try:
        apply='-rpgcreatechaptersevencontent' in u.SystemLibrary.get_command_line().lower().split()
        assets=u.get_editor_subsystem(u.EditorAssetSubsystem);editor=u.get_editor_subsystem(u.EditorActorSubsystem)
        targets=[BOSS,RETURN,MODE,BMODE,SERVER,SELECT]
        dirty={p.get_name() for p in list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())+list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())}
        need(not dirty.intersection(targets),'Preserving unsaved work')
        def save(a,p):
            u.EditorAssetLibrary.set_metadata_tag(a,'ChapterSeven',TAG)
            need(assets.save_loaded_asset(a,only_if_is_dirty=False),'Save '+p);report['saved'].append(p)
        def owned(p,create):
            if assets.does_asset_exist(p):
                a=assets.load_asset(p);need(u.EditorAssetLibrary.get_metadata_tag(a,'ChapterSeven')==TAG,'Unknown owner '+p);return a,False
            need(apply,'Missing '+p);return create(),True
        def data(p,cls):return u.AssetToolsHelpers.get_asset_tools().create_asset(p.rsplit('/',1)[1],p.rsplit('/',1)[0],cls,u.DataAssetFactory())
        def dup(src,dst):return assets.duplicate_asset(src,dst)
        server,new=owned(SERVER,lambda:data(SERVER,u.RPGDungeonServerDefinition))
        rewards={getattr(u.RPGGameDifficulty,n.upper()):assets.load_asset('/Game/Blueprints/GameData/DungeonReward/Progression/DA_DungeonReward_FirstBoss'+n) for n in ['Normal','Hard']}
        if new:
            server.set_editor_property('dungeon_id','Progression.FirstBoss')
            need(u.ToolsetLibrary.set_object_properties(server,json.dumps({'DungeonMap':BOSS+'.L_ChapterOneBoss'})),'Boss map path')
            server.set_editor_property('clear_rewards_by_difficulty',rewards);save(server,SERVER)
        need(server.get_dungeon_map_package()==BOSS and dict(server.get_editor_property('clear_rewards_by_difficulty'))==rewards,'Boss reward/map contract')
        bm,new=owned(BMODE,lambda:dup('/Game/Blueprints/GameMode/BP_BossBattleGameMode',BMODE))
        if new:
            u.get_default_object(bm.generated_class()).set_editor_property('dungeon_server_definitions',[server]);save(bm,BMODE)
        need(list(u.get_default_object(bm.generated_class()).get_editor_property('dungeon_server_definitions'))==[server],'Boss mapping')
        original=u.get_default_object(assets.load_asset('/Game/Blueprints/GameMode/BP_BossBattleGameMode').generated_class())
        duplicate=u.get_default_object(bm.generated_class())
        for prop in ['boss_class','battle_time_limit','death_count','player_controller_class','default_pawn_class','player_state_class','game_state_class','hud_class','bGiveReward']:
            need(duplicate.get_editor_property(prop)==original.get_editor_property(prop),'Preserving existing boss configuration: '+prop)
        # Saving an opened original as a new owned map avoids UE world duplication
        # retaining a second editor world during map teardown. Each map is authored
        # in its own process; the original map on disk is never saved.
        if '-rpgm07bossonly' in u.SystemLibrary.get_command_line().lower().split():
            exists=assets.does_asset_exist(BOSS);need(exists or apply,'Missing boss staging map')
            boss=u.EditorLoadingAndSavingUtils.load_map(BOSS if exists else '/Game/Maps/bossmap')
            if not exists:
                boss.get_world_settings().set_editor_property('default_game_mode',bm.generated_class())
                u.EditorAssetLibrary.set_metadata_tag(boss,'ChapterSeven',TAG)
                need(u.EditorLoadingAndSavingUtils.save_map(boss,BOSS),'Boss staging save');report['saved'].append(BOSS)
            # SaveAs can drop package metadata; an existing map is inspected only,
            # and must retain the owned mode. Existing maps are never overwritten.
            need(boss.get_world_settings().get_editor_property('default_game_mode')==bm.generated_class(),'Boss mode')
            report.update(passed=True,read_only=not apply);return
        need(assets.does_asset_exist(BOSS),'Author boss staging in a separate -RPGM07BossOnly process first')
        selection,new=owned(SELECT,lambda:data(SELECT,u.RPGDungeonDefinition))
        if new:
            selection.set_editor_property('dungeon_id','Story.ChapterOneReturn');selection.set_editor_property('display_name',u.Text('돌아갈 길 · 수락 / 반출 재개'))
            selection.set_editor_property('difficulties',[u.RPGGameDifficulty.NORMAL]);selection.set_editor_property('default_difficulty',u.RPGGameDifficulty.NORMAL);save(selection,SELECT)
        mode,new=owned(MODE,lambda:dup('/Game/Blueprints/GameMode/BP_GameModeBase',MODE))
        defaults=u.get_default_object(mode.generated_class())
        if new:
            defaults.set_editor_property('bGiveReward',False);defaults.set_editor_property('dungeon_server_definitions',[])
            save(mode,MODE)
        need(not defaults.get_editor_property('bGiveReward') and not list(defaults.get_editor_property('dungeon_server_definitions')),'Return has no reward path')
        # The base mode retains strict authenticated admission. There is no boss,
        # encounter objective, clear producer or reward-enabled mode in this map.
        new=not assets.does_asset_exist(RETURN);need(not new or apply,'Missing return map')
        world=u.EditorLoadingAndSavingUtils.new_blank_map(False) if new else u.EditorLoadingAndSavingUtils.load_map(RETURN)
        if new:
            world.get_world_settings().set_editor_property('default_game_mode',mode.generated_class())
            world.get_world_settings().set_editor_property('force_no_precomputed_lighting',True)
            def spawn(cls,name,pos):
                a=editor.spawn_actor_from_class(cls,u.Vector(*pos));need(a,'Spawn '+name);a.set_actor_label('M07_'+name);a.tags=[TAG];return a
            cube=assets.load_asset('/Engine/BasicShapes/Cube');cyl=assets.load_asset('/Engine/BasicShapes/Cylinder')
            for name,pos,scale in [('Floor',(0,0,-30),(50,35,.6)),('North',(0,1700,150),(50,.4,3)),('West',(-2500,0,150),(.4,35,3)),('East',(2500,0,150),(.4,35,3)),('ShelterWall',(1200,1100,140),(10,.4,2.8))]:
                a=spawn(u.StaticMeshActor,name,pos);a.static_mesh_component.set_static_mesh(cube);a.set_actor_scale3d(u.Vector(*scale));a.static_mesh_component.set_collision_profile_name('BlockAll')
            for i in range(4):spawn(u.PlayerStart,'Start'+str(i),(-1600,i*150,110))
            sun=spawn(u.DirectionalLight,'Sun',(0,0,1500));sun.set_actor_rotation(u.Rotator(pitch=-50,yaw=-30,roll=0),False)
            spawn(u.SkyLight,'Sky',(0,0,1000))
            daon=spawn(u.RPGChapterSevenDaon,'Daon',(-1150,300,90));daon.get_editor_property('appearance').set_static_mesh(cyl)
            for i,(name,pos) in enumerate([('C_Safety',(-400,0,70)),('B_Brace',(450,0,70)),('D_Patients',(1250,600,70))]):
                a=spawn(u.RPGChapterSevenOperation,name,pos);a.set_editor_property('step',i)
                a.get_editor_property('appearance').set_static_mesh(cube if i==1 else cyl)
                a.get_editor_property('appearance').set_relative_scale3d(u.Vector(2,.4,1) if i==1 else u.Vector(.7,.7,1.2))
            exp=spawn(u.RPGExperienceSelectionActor,'Experience',(0,0,0));exp.set_editor_property('experience',assets.load_asset('/Game/System/Experiences/DA_ClientPresentationAndOptionsHint'));exp.set_editor_property('selection_mode',u.RPGExperienceSelectionMode.LOAD_EXPERIENCE)
            u.EditorAssetLibrary.set_metadata_tag(world,'ChapterSeven',TAG)
            need(u.EditorLoadingAndSavingUtils.save_map(world,RETURN),'Return map save');report['saved'].append(RETURN)
        # The newly authored map is already the editor world. Do not reopen it
        # while Python still holds its authored actors.
        need(world.get_world_settings().get_editor_property('default_game_mode')==mode.generated_class(),'Return mode')
        need(len(u.GameplayStatics.get_all_actors_of_class(world,u.RPGChapterSevenOperation))==3,'Three operations')
        need(not u.GameplayStatics.get_all_actors_of_class(world,u.RPGBossCharacter),'No boss in return scene')
        report.update(passed=True,read_only=not apply)
    finally:
        out.parent.mkdir(parents=True,exist_ok=True);out.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
main()
