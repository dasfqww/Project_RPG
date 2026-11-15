"""Add the M03 blockout to the existing Intro1 arena; preserve its practice actors.

Run in an owned editor with -ExecutePythonScript=... -RPGCreateChapterOne.
Without the apply flag, inspect only. Never save unrelated packages.
"""
import hashlib
import json
import os
from pathlib import Path
import unreal

MAP = '/Game/Maps/Progression/L_ProgressionIntro1'
TAG = 'ChapterOne.M03.v1'
POINTS = [(-2100, -1200, 90), (-650, -1500, 100), (750, -1600, 100)]

def require(ok, message):
    if not ok:
        raise RuntimeError(message)

def main():
    report_path = Path(os.environ['RPG_CHAPTER_ONE_REPORT'])
    require(report_path.is_absolute(), 'Absolute report required')
    result = {'passed': False, 'saved': False, 'map': MAP}
    try:
        require(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is None, 'PIE is active')
        dirty = list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
        require(not any(p.get_name() == MAP for p in dirty), 'Preserving unsaved map')
        path = Path(unreal.Paths.project_content_dir()) / (MAP[6:] + '.umap')
        before = hashlib.sha256(path.read_bytes()).hexdigest()
        world = unreal.EditorLoadingAndSavingUtils.load_map(MAP)
        require(world is not None, 'Existing Intro1 map required')
        actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        existing = list(actors.get_all_level_actors())
        original_names = {a.get_path_name() for a in existing}
        owned = [a for a in existing if TAG in [str(t) for t in a.tags]]
        apply = '-rpgcreatechapterone' in unreal.SystemLibrary.get_command_line().lower().split()
        clue_class = unreal.load_class(None, '/Script/Project_RPG.RPGChapterOneClue')
        npc_class = unreal.load_class(None, '/Script/Project_RPG.RPGNarrativeNPC')
        require(clue_class and npc_class, 'Build the chapter module first')
        if not owned and apply:
            cube = unreal.load_asset('/Engine/BasicShapes/Cube')
            cylinder = unreal.load_asset('/Engine/BasicShapes/Cylinder')
            def box(name, pos, scale, rotation=(0,0,0), mesh=cube):
                a = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*pos), unreal.Rotator(*rotation))
                a.set_actor_label('CH01_' + name)
                a.tags = [TAG]
                a.static_mesh_component.set_static_mesh(mesh)
                a.set_actor_scale3d(unreal.Vector(*scale))
                a.static_mesh_component.set_collision_profile_name('BlockAll')
                return a
            # A recognisable loaded cart: bed, rails, four wheels and intact ore sacks.
            box('CartBed', (-2100,-1370,55), (3.3,1.8,.2))
            for y in (-1450,-1290):
                box('CartRail', (-2100,y,100),(3.3,.12,.8))
            for x in (-2210,-1990):
                for y in (-1470,-1270):
                    box('Wheel',(x,y,40),(.65,.65,.18),(90,0,0),cylinder)
            for x,y in [(-2200,-1360),(-2110,-1410),(-2010,-1360)]:
                box('OreSack',(x,y,95),(.7,.65,.6),mesh=cylinder)
            # Sealed main passage; diagonal props visibly support the INNER (south) face.
            for x in (-880,-650,-420):
                box('SealedGate',(x,-1760,150),(2.2,.3,3))
            for x in (-820,-480):
                box('InnerBrace',(x,-1850,100),(.22,.22,2.4),(0,0,30))
            box('WorkMark',(-650,-1738,130),(.5,.06,.15))
            # Separate narrow inspection entrance, not a removed main blockade.
            for x in (540,960):
                box('InspectionPost',(x,-1780,145),(.3,.5,2.9))
            box('InspectionLintel',(750,-1780,285),(4.5,.5,.3))
            box('InspectionDarkEnd',(750,-1950,140),(4.2,.2,2.8))
            for i,pos in enumerate(POINTS,1):
                a=actors.spawn_actor_from_class(clue_class, unreal.Vector(*pos))
                a.set_actor_label('CH01_Clue'+str(i)); a.tags=[TAG]
                a.set_editor_property('clue_number',i)
            for name,pos,entries in [
                ('InvestigationOrder',(-2430,-1050,90),['CH01.M03.Offer']),
                ('NextObjective',(1100,-1550,90),['CH01.M04.Offer'])]:
                a=actors.spawn_actor_from_class(npc_class,unreal.Vector(*pos))
                a.set_actor_label('CH01_'+name); a.tags=[TAG]
                a.set_editor_property('display_name','현장 목표')
                a.set_editor_property('dialogue_entry_ids',entries)
                a.get_editor_property('appearance').set_static_mesh(cube)
                a.get_editor_property('appearance').set_relative_scale3d(unreal.Vector(.4,.4,1.2))
            require(hashlib.sha256(path.read_bytes()).hexdigest()==before,'Map changed during authoring')
            require(original_names.issubset({a.get_path_name() for a in actors.get_all_level_actors()}),'Existing actors removed')
            require(unreal.EditorLoadingAndSavingUtils.save_map(world,MAP),'Map save failed')
            result['saved']=True
            owned=[a for a in actors.get_all_level_actors() if TAG in [str(t) for t in a.tags]]
        clues=[a for a in owned if a.get_class()==clue_class]
        require(sorted(a.get_editor_property('clue_number') for a in clues)==[1,2,3],'Missing or duplicate clues')
        require(len([a for a in owned if a.get_class()==npc_class])==2,'Missing objective entry boards')
        result.update(passed=True, authored_actors=len(owned),sha256=hashlib.sha256(path.read_bytes()).hexdigest())
    finally:
        report_path.parent.mkdir(parents=True,exist_ok=True)
        report_path.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')

main()
