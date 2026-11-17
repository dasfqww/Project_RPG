"""M06 map additions only; read-only without -RPGCreateChapterSixContent.
RPG_M06_REPORT must be an absolute JSON output. Existing combat assets are preserved.
"""
import json
import os
from pathlib import Path
import unreal as u
INTRO2='/Game/Maps/Progression/L_ProgressionIntro2'
INTRO3='/Game/Maps/Progression/L_ProgressionIntro3'
MARKER='CH01.M06.v1'
OLD=['CH01.M05.Offer','CH01.D05','CH01.D06.Preview']
ENTRIES=OLD+['CH01.M06.Offer','CH01.D06','CH01.D07.Preview']
def require(v,m):
    if not v:raise RuntimeError(m)
def main():
    out=Path(os.environ.get('RPG_M06_REPORT',''));require(out.is_absolute(),'Set RPG_M06_REPORT')
    result={'passed':False,'saved':[],'combat_config_modified':False}
    try:
        apply='-rpgcreatechaptersixcontent' in u.SystemLibrary.get_command_line().lower().split()
        dirty={p.get_name() for p in list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())+list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())}
        require(not {INTRO2,INTRO3}&dirty,'Preserving unsaved maps')
        editor=u.get_editor_subsystem(u.EditorActorSubsystem)
        w=u.EditorLoadingAndSavingUtils.load_map(INTRO2);require(w,'Intro2 required')
        daon=[a for a in editor.get_all_level_actors() if a.get_actor_label()=='M05_Daon'];require(len(daon)==1,'M05 Daon differs')
        entries=list(daon[0].get_editor_property('dialogue_entry_ids'));require(entries in (OLD,ENTRIES),'Preserving unknown entries')
        if entries==OLD and apply:
            daon[0].set_editor_property('dialogue_entry_ids',ENTRIES)
            require(u.EditorLoadingAndSavingUtils.save_map(w,INTRO2),'Offer save failed');result['saved'].append(INTRO2)
        require(list(daon[0].get_editor_property('dialogue_entry_ids'))==ENTRIES,'M06 offer/briefing missing')
        w=u.EditorLoadingAndSavingUtils.load_map(INTRO3);require(w,'Intro3 required')
        owned=[a for a in editor.get_all_level_actors() if MARKER in [str(t) for t in a.tags]]
        npc=u.load_class(None,'/Script/Project_RPG.RPGChapterSixDaon');patient=u.load_class(None,'/Script/Project_RPG.RPGChapterSixPatient');require(npc and patient,'Compatible M06 module required')
        if not owned and apply:
            def spawn(cls,name,pos):
                a=editor.spawn_actor_from_class(cls,u.Vector(*pos));require(a,'Spawn failed');a.set_actor_label('M06_'+name);a.tags=[MARKER];return a
            cyl=u.load_asset('/Engine/BasicShapes/Cylinder');cube=u.load_asset('/Engine/BasicShapes/Cube')
            n=spawn(npc,'Daon',(-300,650,90));n.get_editor_property('appearance').set_static_mesh(cyl);n.get_editor_property('appearance').set_relative_scale3d(u.Vector(.65,.65,1.8))
            for name,pos,recall in [('Patient',(850,-150,100),False),('ShelterRecall',(2200,1500,100),True)]:
                a=spawn(patient,name,pos);a.set_editor_property('bRecallPoint',recall)
                a.get_editor_property('appearance').set_static_mesh(cyl if recall else cube)
                a.get_editor_property('appearance').set_relative_scale3d(u.Vector(.6,.6,1.2 if recall else .5))
                if recall:a.get_editor_property('appearance').set_relative_rotation(u.Rotator(pitch=90,yaw=0,roll=0),False,False)
            for name,pos,scale in [('ShelterNorth',(2000,1850,150),(10,.3,3)),('ShelterEast',(2500,1500,150),(.3,7,3)),('DoorLeft',(1700,1150,150),(.5,.5,3)),('DoorRight',(2400,1150,150),(.5,.5,3)),('MedicalSupplies',(2300,1650,40),(1.8,.8,.8))]:
                a=spawn(u.StaticMeshActor,name,pos);a.static_mesh_component.set_static_mesh(cube);a.static_mesh_component.set_collision_profile_name('BlockAll');a.set_actor_scale3d(u.Vector(*scale))
            require(u.EditorLoadingAndSavingUtils.save_map(w,INTRO3),'Shelter save failed');result['saved'].append(INTRO3)
            owned=[a for a in editor.get_all_level_actors() if MARKER in [str(t) for t in a.tags]]
        require(len(owned)==8,'M06 scene ownership/count differs')
        require(len([a for a in owned if a.get_class()==npc])==1,'One M06 Daon required')
        pts=[a for a in owned if a.get_class()==patient];require(len(pts)==2 and sorted(a.get_editor_property('bRecallPoint') for a in pts)==[False,True],'Fresh/recall approvals differ')
        require(w.get_world_settings().get_editor_property('default_game_mode').get_path_name().endswith('BP_Intro3GameMode_C'),'Preserving combat mode')
        require(len(u.GameplayStatics.get_all_actors_of_class(w,u.PlayerStart))==4,'Preserving starts')
        require(not {INTRO2,INTRO3}&{p.get_name() for p in list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())+list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())},'Owned maps remain dirty')
        result.update(passed=True,owned_actors=len(owned),read_only=not apply)
    finally:
        out.parent.mkdir(parents=True,exist_ok=True);out.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
main()
