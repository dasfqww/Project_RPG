"""Read-only M05 inspection. -RPGCreateChapterFiveContent adds only owned missing content.
RPG_M05_REPORT must be an absolute JSON path. Existing Intro2 combat and rewards are preserved.
"""
import json
import os
from pathlib import Path
import unreal as u
INTRO1='/Game/Maps/Progression/L_ProgressionIntro1'
INTRO2='/Game/Maps/Progression/L_ProgressionIntro2'
MARKER='CH01.M05.v1'
def require(v,m):
    if not v: raise RuntimeError(m)
def main():
    output=Path(os.environ.get('RPG_M05_REPORT',''));require(output.is_absolute(),'Set RPG_M05_REPORT')
    result={'passed':False,'saved':[],'combat_modified':False}
    try:
        apply='-rpgcreatechapterfivecontent' in u.SystemLibrary.get_command_line().lower().split()
        dirty={p.get_name() for p in list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())+list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())}
        require(not {INTRO1,INTRO2}&dirty,'Preserving unsaved maps')
        editor=u.get_editor_subsystem(u.EditorActorSubsystem)
        w=u.EditorLoadingAndSavingUtils.load_map(INTRO1);require(w,'Intro1 missing')
        board=[a for a in editor.get_all_level_actors() if a.get_actor_label()=='CH01_NextObjective']
        require(len(board)==1 and isinstance(board[0],u.RPGNarrativeNPC),'Field board differs')
        entries=list(board[0].get_editor_property('dialogue_entry_ids'))
        require(entries in (['CH01.M04.Offer'],['CH01.M04.Offer','CH01.M05.Offer']),'Preserving another board definition')
        if entries==['CH01.M04.Offer'] and apply:
            board[0].set_editor_property('dialogue_entry_ids',entries+['CH01.M05.Offer'])
            require(u.EditorLoadingAndSavingUtils.save_map(w,INTRO1),'Field save failed');result['saved'].append(INTRO1)
        require('CH01.M05.Offer' in board[0].get_editor_property('dialogue_entry_ids'),'M05 field offer missing')
        w=u.EditorLoadingAndSavingUtils.load_map(INTRO2);require(w,'Intro2 missing')
        cls=u.load_class(None,'/Script/Project_RPG.RPGChapterOneDaon');require(cls,'Compatible M05 module required')
        owned=[a for a in editor.get_all_level_actors() if MARKER in [str(t) for t in a.tags]]
        if not owned and apply:
            def spawn(c,name,pos):
                a=editor.spawn_actor_from_class(c,u.Vector(*pos));require(a,'Spawn failed')
                a.set_actor_label('M05_'+name);a.tags=[MARKER];return a
            daon=spawn(cls,'Daon',(1400,1000,90))
            daon.get_editor_property('appearance').set_static_mesh(u.load_asset('/Engine/BasicShapes/Cylinder'))
            daon.get_editor_property('appearance').set_relative_scale3d(u.Vector(.65,.65,1.8))
            cube=u.load_asset('/Engine/BasicShapes/Cube')
            for name,pos,scale in [('RoomNorth',(1400,1350,150),(9,.3,3)),('RoomEast',(1900,1000,150),(.3,7,3)),('Supplies',(1700,1200,45),(1.5,1.5,.9))]:
                a=spawn(u.StaticMeshActor,name,pos);a.static_mesh_component.set_static_mesh(cube)
                a.static_mesh_component.set_collision_profile_name('BlockAll');a.set_actor_scale3d(u.Vector(*scale))
            require(u.EditorLoadingAndSavingUtils.save_map(w,INTRO2),'Room save failed');result['saved'].append(INTRO2)
            owned=[a for a in editor.get_all_level_actors() if MARKER in [str(t) for t in a.tags]]
        require(len(owned)==4,'M05 room ownership/count differs')
        daons=[a for a in owned if a.get_class()==cls];require(len(daons)==1,'Single Daon required')
        require(abs(daons[0].get_actor_location().x-1400)<.01 and abs(daons[0].get_actor_location().y-1000)<.01,'Daon placement differs')
        require(w.get_world_settings().get_editor_property('default_game_mode').get_path_name().endswith('BP_Intro2GameMode_C'),'Combat mode differs')
        require(len(u.GameplayStatics.get_all_actors_of_class(w,u.PlayerStart))==4,'Existing starts changed')
        result.update(passed=True,owned_actors=len(owned),read_only=not apply)
    finally:
        output.parent.mkdir(parents=True,exist_ok=True);output.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
main()
