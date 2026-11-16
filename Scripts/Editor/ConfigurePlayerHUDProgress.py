"""Place the existing shared action-progress view above combat/context HUDs.

Read-only by default. -RPGApplyPlayerHUDProgress saves only BP_RPGHUD. Existing
progress source/state, class, dimensions and lifecycle remain unchanged.
"""
import hashlib
import json
import pathlib
import runpy
import shutil
import unreal
from editor_toolset.toolsets.blueprint import BlueprintTools as BP

ROOT=pathlib.Path(__file__).resolve().parents[2]
PATH='/Game/Blueprints/UI/PlayerHUD/BP_RPGHUD'
PATCH={'ActionProgressAnchor':dict(x=.5,y=1), 'ActionProgressAlignment':dict(x=.5,y=1),
       'ActionProgressOffset':dict(x=0,y=-288)}
H=runpy.run_path(str(ROOT/'Scripts/Editor/ConfigureGreatSwordFourthSkillSlot.py'),run_name='progress_layout_helpers')
G=runpy.run_path(str(ROOT/'Scripts/Editor/ConfigurePlayerHUDLayout.py'),run_name='progress_graph_helpers')['graph']

def main():
    _,switches,params=unreal.SystemLibrary.parse_command_line(unreal.SystemLibrary.get_command_line())
    switches={str(s).lower() for s in switches}
    apply='rpgapplyplayerhudprogress' in switches
    out=ROOT/'Saved/BuildChecks/PlayerHUDLayout-20261117'
    out.mkdir(parents=True,exist_ok=True)
    assert ' / player-hud-layout-20261117 |' in (ROOT/'Docs/Online_RPG_Development_Roadmap.md').read_text(encoding='utf-8-sig')
    assert unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is None, 'Preserve PIE'
    assets=unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    bp=assets.load_asset(PATH)
    assert PATH not in {p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    cdo=unreal.get_default_object(bp.generated_class())
    before=H['object_properties'](unreal,cdo); graphs=G(bp)
    desired=dict(before); desired.update(PATCH)
    changed=not H['equivalent'](before,desired)
    disk=ROOT/'Content/Blueprints/UI/PlayerHUD/BP_RPGHUD.uasset'
    digest=hashlib.sha256(disk.read_bytes()).hexdigest()
    if apply and changed:
        backup=out/'Temp/Backups/BP_RPGHUD.uasset'; backup.parent.mkdir(parents=True,exist_ok=True)
        if not backup.exists(): shutil.copy2(disk,backup)
        assert unreal.ToolsetLibrary.set_object_properties(cdo,json.dumps(PATCH))
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        assert bp.get_editor_property('status') == unreal.BlueprintStatus.BS_UP_TO_DATE
        assert H['equivalent'](H['object_properties'](unreal,unreal.get_default_object(bp.generated_class())),desired)
        assert G(bp)==graphs and hashlib.sha256(disk.read_bytes()).hexdigest()==digest
        assert assets.save_loaded_asset(bp,only_if_is_dirty=False)
    result=dict(passed=True,apply=apply,changed=changed,saved=[PATH] if apply and changed else [],before_hash=digest,
        after_hash=hashlib.sha256(disk.read_bytes()).hexdigest(),defaults=PATCH,graph_preserved=G(bp)==graphs)
    (out/('ProgressApplied.json' if apply else 'ProgressPlan.json')).write_text(json.dumps(result,indent=2),encoding='utf-8')
    unreal.log('HUD_PROGRESS_LAYOUT_PASS Saved='+str(len(result['saved'])))

if __name__=='__main__': main()
