"""Place and verify the S01 workshop in the existing Intro1 arena.

Run in an isolated project with -ExecutePythonScript and
-RPGCreateChapterSideOneContent to save. Without the flag, verify only.
"""
import hashlib
import json
import os
from pathlib import Path

import unreal


MAP = "/Game/Maps/Progression/L_ProgressionIntro1"
TAG = "ChapterOne.S01.Workshop"
ENTRIES = [
    "CH01.S01.Offer", "CH01.S01.Offer.After", "CH01.S01.Offer.Skipped", "CH01.D12",
    "CH01.S01.Epilogue", "CH01.S01.Epilogue.After", "CH01.S01.Epilogue.Skipped",
]
GLOVE = "/Game/Characters/Player/Armor/Gloves/Leather/PC_Armor_Hands_Leather001/SM_PC_Armor_Hands_Leather001"
CUBE = "/Engine/BasicShapes/Cube"
CYLINDER = "/Engine/BasicShapes/Cylinder"
CLUES = {
    "S01_Glove": ("CH01.S01.Glove", "작업 장갑 · 이중 매듭", (-2280, -1650, 112), GLOVE, (1, 1, 1)),
    "S01_Nameplate": ("CH01.S01.Nameplate", "작업대 이름표", (-1680, -1650, 103), CUBE, (0.33, 0.22, 0.05)),
}
PROPS = {
    "S01_GloveStand": ((-2280, -1650, 45), (0.85, 0.75, 0.90)),
    "S01_Workbench": ((-1680, -1650, 90), (2.60, 1.05, 0.12)),
    "S01_WorkbenchLeftLeg": ((-1785, -1650, 42), (0.20, 0.90, 0.84)),
    "S01_WorkbenchRightLeg": ((-1575, -1650, 42), (0.20, 0.90, 0.84)),
}
YUNA = (-2700, -720, 90)


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def tagged(actors):
    return [actor for actor in actors if TAG in [str(tag) for tag in actor.tags]]


def spawn(actors, actor_class, label, location):
    actor = actors.spawn_actor_from_class(actor_class, unreal.Vector(*location))
    require(actor is not None, "Spawn failed: " + label)
    actor.set_actor_label(label)
    actor.tags = [TAG]
    return actor


def main():
    report_path = Path(os.environ["RPG_S01_REPORT"])
    require(report_path.is_absolute(), "An absolute report path is required.")
    result = {"passed": False, "saved": False}
    try:
        editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
        require(editor.get_game_world() is None, "Preserving active play session.")
        require(not any(p.get_name() == MAP for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()),
                "Preserving unsaved Intro1 map.")
        map_file = Path(unreal.Paths.project_content_dir()) / (MAP[6:] + ".umap")
        before = hashlib.sha256(map_file.read_bytes()).hexdigest()
        world = unreal.EditorLoadingAndSavingUtils.load_map(MAP)
        require(world is not None, "The existing Intro1 map is required.")
        mode = world.get_world_settings().get_editor_property("default_game_mode")
        require(mode is not None and mode.get_name() == "BP_Intro1GameMode_C", "Unexpected Intro1 game mode.")
        require(len(unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RPGChapterOneClue)) == 3,
                "The three existing M03 clues are required.")
        actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        existing = list(actors.get_all_level_actors())
        owned = tagged(existing)
        apply = "-rpgcreatechaptersideonecontent" in unreal.SystemLibrary.get_command_line().lower().split()
        new = not owned and apply
        meshes = {path: unreal.load_asset(path) for path in (GLOVE, CUBE, CYLINDER)}
        require(all(meshes.values()), "Missing S01 meshes.")
        if new:
            for label, (location, scale) in PROPS.items():
                prop = spawn(actors, unreal.StaticMeshActor, label, location)
                prop.static_mesh_component.set_static_mesh(meshes[CUBE])
                prop.set_actor_scale3d(unreal.Vector(*scale))
                prop.static_mesh_component.set_collision_profile_name("BlockAll")
            for label, (target, display, location, mesh, scale) in CLUES.items():
                clue = spawn(actors, unreal.RPGNarrativeInvestigation, label, location)
                clue.set_editor_property("target_id", target)
                clue.set_editor_property("display_name", unreal.Text(display))
                appearance = clue.get_editor_property("appearance")
                appearance.set_static_mesh(meshes[mesh])
                appearance.set_relative_scale3d(unreal.Vector(*scale))
            yuna = spawn(actors, unreal.RPGNarrativeNPC, "S01_Yuna", YUNA)
            yuna.set_editor_property("display_name", unreal.Text("유나"))
            yuna.set_editor_property("dialogue_entry_ids", ENTRIES)
            appearance = yuna.get_editor_property("appearance")
            appearance.set_static_mesh(meshes[CYLINDER])
            appearance.set_relative_scale3d(unreal.Vector(0.55, 0.55, 1.7))
            all_actors = list(actors.get_all_level_actors())
            require(set(existing).issubset(all_actors), "An existing Intro1 actor was removed.")
            owned = tagged(all_actors)

        by_label = {actor.get_actor_label(): actor for actor in owned}
        require(len(owned) == len(by_label) and by_label.keys() == (set(PROPS) | set(CLUES) | {"S01_Yuna"}),
                "Missing or duplicate S01 placements.")
        for label, (location, scale) in PROPS.items():
            prop = by_label[label]
            require(prop.get_actor_location() == unreal.Vector(*location)
                    and prop.get_actor_scale3d() == unreal.Vector(*scale)
                    and prop.static_mesh_component.static_mesh == meshes[CUBE], "Wrong work area prop: " + label)
        for label, (target, display, location, mesh, _) in CLUES.items():
            clue = by_label[label]
            require(clue.get_class() == unreal.RPGNarrativeInvestigation.static_class()
                    and clue.get_actor_location() == unreal.Vector(*location)
                    and clue.get_editor_property("target_id") == target
                    and str(clue.get_editor_property("display_name")) == display
                    and clue.get_editor_property("appearance").static_mesh == meshes[mesh], "Wrong clue: " + label)
        yuna = by_label["S01_Yuna"]
        require(yuna.get_class() == unreal.RPGNarrativeNPC.static_class()
                and yuna.get_actor_location() == unreal.Vector(*YUNA)
                and str(yuna.get_editor_property("display_name")) == "유나"
                and list(yuna.get_editor_property("dialogue_entry_ids")) == ENTRIES, "Wrong Yuna placement.")
        if new:
            require(hashlib.sha256(map_file.read_bytes()).hexdigest() == before, "The map changed during authoring.")
            require(unreal.EditorLoadingAndSavingUtils.save_map(world, MAP), "Intro1 map save failed.")
            result["saved"] = True
        result.update(passed=True, targets=[value[0] for value in CLUES.values()], entries=ENTRIES,
                      map_sha256=hashlib.sha256(map_file.read_bytes()).hexdigest())
    finally:
        report_path.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")


main()
