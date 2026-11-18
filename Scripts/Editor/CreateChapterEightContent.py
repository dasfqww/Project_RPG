"""Author the M08 reporting station in the existing return map.

Run in an isolated project with -ExecutePythonScript and
-RPGCreateChapterEightContent to save. Without the flag, verify only.
"""
import hashlib
import json
import os
from pathlib import Path

import unreal


MAP = "/Game/Maps/Progression/L_ChapterOneReturn"
TAG = "ChapterEight.ReportStation"
LABEL = "M08_MiraReportStation"
DISPLAY_NAME = "거점 연락소 · 미라"
LOCATION = unreal.Vector(-1510, -620, 90)
ENTRIES = ["CH01.M08.Offer", "CH01.D08", "CH01.D08.Report", "CH01.D09"]


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    report_path = Path(os.environ["RPG_M08_REPORT"])
    require(report_path.is_absolute(), "An absolute report path is required.")
    result = {"passed": False, "saved": False}
    try:
        editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
        require(editor.get_game_world() is None, "Preserving active play session.")
        dirty = unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
        require(not any(package.get_name() == MAP for package in dirty), "Preserving unsaved return map.")

        map_file = Path(unreal.Paths.project_content_dir()) / (MAP[6:] + ".umap")
        before = hashlib.sha256(map_file.read_bytes()).hexdigest()
        world = unreal.EditorLoadingAndSavingUtils.load_map(MAP)
        require(world is not None, "The existing M07 return map is required.")
        actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        existing = list(actors.get_all_level_actors())
        mode = world.get_world_settings().get_editor_property("default_game_mode")
        operations = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RPGChapterSevenOperation)
        require(len(operations) == 3 and mode.get_name() == "BP_ChapterOneReturnMode_C", "Unexpected M07 map.")
        stations = [actor for actor in existing if TAG in [str(tag) for tag in actor.tags]]
        apply = "-rpgcreatechaptereightcontent" in unreal.SystemLibrary.get_command_line().lower().split()
        if not stations and apply:
            mesh = unreal.load_asset("/Engine/BasicShapes/Cube")
            require(mesh is not None, "Reporting station mesh is missing.")
            station = actors.spawn_actor_from_class(unreal.RPGNarrativeNPC, LOCATION)
            require(station is not None, "Could not place the reporting station.")
            station.set_actor_label(LABEL)
            station.tags = [TAG]
            station.set_editor_property("display_name", unreal.Text(DISPLAY_NAME))
            station.set_editor_property("dialogue_entry_ids", ENTRIES)
            appearance = station.get_editor_property("appearance")
            appearance.set_static_mesh(mesh)
            appearance.set_relative_scale3d(unreal.Vector(0.85, 0.55, 1.65))
            stations = [station]
            require(set(existing).issubset(actors.get_all_level_actors()), "An existing actor was removed.")
            require(hashlib.sha256(map_file.read_bytes()).hexdigest() == before, "The map changed during authoring.")

        require(len(stations) == 1, "Expected one reporting station.")
        station = stations[0]
        require(station.get_class() == unreal.RPGNarrativeNPC.static_class(), "Unexpected reporting station class.")
        require(station.get_actor_label() == LABEL and station.get_actor_location() == LOCATION, "Unexpected station placement.")
        require(str(station.get_editor_property("display_name")) == DISPLAY_NAME, "Unexpected station name.")
        require(list(station.get_editor_property("dialogue_entry_ids")) == ENTRIES, "Unexpected dialogue entry points.")
        require(station.get_editor_property("appearance").get_editor_property("static_mesh") is not None, "Missing station mesh.")

        if not any(actor == station for actor in existing):
            require(unreal.EditorLoadingAndSavingUtils.save_map(world, MAP), "Return map save failed.")
            result["saved"] = True
        result.update(passed=True, entries=ENTRIES, map_sha256=hashlib.sha256(map_file.read_bytes()).hexdigest())
    finally:
        report_path.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")


main()
