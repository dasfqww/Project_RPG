"""Place the S02 transport records and Mira's contact point in Intro1."""
import hashlib
import json
import os
from pathlib import Path

import unreal


MAP = "/Game/Maps/Progression/L_ProgressionIntro1"
TAG = "ChapterOne.S02.TransportRecords"
ENTRIES = [
    "CH01.S02.Offer", "CH01.S02.Offer.After", "CH01.S02.Offer.Skipped",
    "CH01.S02.Report", "CH01.S02.Epilogue",
]
RECORDS = {
    "S02_Roster": ("CH01.S02.Roster", "운송 인원 명단", (-1240, -1475, 101)),
    "S02_Ledger": ("CH01.S02.Ledger", "광석 운송 장부", (-220, -1540, 101)),
}
MIRA = (-2480, -390, 90)
MIRA_NAME = "현장 연락소 · 미라"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def spawn(actors, actor_class, name, location):
    actor = actors.spawn_actor_from_class(actor_class, unreal.Vector(*location))
    require(actor is not None, "Could not place " + name)
    actor.set_actor_label(name)
    actor.tags = [TAG]
    return actor


def main():
    proof = Path(os.environ["RPG_S02_REPORT"])
    require(proof.is_absolute(), "An absolute report path is required.")
    result = {"passed": False, "saved": False}
    try:
        editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
        require(editor.get_game_world() is None, "Preserving active play session.")
        dirty_maps = unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
        require(not any(package.get_name() == MAP for package in dirty_maps), "Preserving unsaved Intro1 map.")
        path = Path(unreal.Paths.project_content_dir()) / (MAP[6:] + ".umap")
        before = hashlib.sha256(path.read_bytes()).hexdigest()
        world = unreal.EditorLoadingAndSavingUtils.load_map(MAP)
        require(world is not None, "The existing Intro1 map is required.")
        mode = world.get_world_settings().get_editor_property("default_game_mode")
        require(mode is not None and mode.get_name() == "BP_Intro1GameMode_C", "Unexpected Intro1 mode.")
        require(len(unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RPGChapterOneClue)) == 3,
                "Expected the existing three M03 clues.")
        actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        existing = list(actors.get_all_level_actors())
        require({"S01_Glove", "S01_Nameplate", "S01_Yuna", "SupplyChest"}.issubset(
            actor.get_actor_label() for actor in existing), "Expected the published S01 and chest placements.")
        owned = [actor for actor in existing if TAG in [str(tag) for tag in actor.tags]]
        apply = "-rpgcreatechaptersidetwocontent" in unreal.SystemLibrary.get_command_line().lower().split()
        new = not owned and apply
        if new:
            cube = unreal.load_asset("/Engine/BasicShapes/Cube")
            wood = unreal.load_asset("/Game/Assets/Arena/Materials/MI_WoodAndMetal")
            require(cube and wood, "Missing transport record props.")
            for label, (target, name, (x, y, z)) in RECORDS.items():
                support = spawn(actors, unreal.StaticMeshActor, label + "_Stand", (x, y, 45))
                support.set_actor_scale3d(unreal.Vector(0.75, 0.62, 0.90))
                support.static_mesh_component.set_static_mesh(cube)
                support.static_mesh_component.set_material(0, wood)
                support.static_mesh_component.set_collision_profile_name("BlockAll")
                clue = spawn(actors, unreal.RPGNarrativeInvestigation, label, (x, y, z))
                clue.set_editor_property("target_id", target)
                clue.set_editor_property("display_name", unreal.Text(name))
                appearance = clue.get_editor_property("appearance")
                appearance.set_static_mesh(cube)
                appearance.set_relative_scale3d(unreal.Vector(0.34, 0.28, 0.04 if label == "S02_Roster" else 0.12))
            mira = spawn(actors, unreal.RPGNarrativeNPC, "S02_Mira", MIRA)
            mira.set_editor_property("display_name", unreal.Text(MIRA_NAME))
            mira.set_editor_property("dialogue_entry_ids", ENTRIES)
            appearance = mira.get_editor_property("appearance")
            appearance.set_static_mesh(cube)
            appearance.set_material(0, wood)
            appearance.set_relative_scale3d(unreal.Vector(0.85, 0.55, 1.65))
            all_actors = list(actors.get_all_level_actors())
            require(set(existing).issubset(all_actors), "An existing Intro1 actor was removed.")
            owned = [actor for actor in all_actors if TAG in [str(tag) for tag in actor.tags]]

        labels = {actor.get_actor_label(): actor for actor in owned}
        expected = set(RECORDS) | {label + "_Stand" for label in RECORDS} | {"S02_Mira"}
        require(len(owned) == len(labels) and labels.keys() == expected, "Missing or duplicated S02 placement.")
        for label, (target, name, location) in RECORDS.items():
            clue = labels[label]
            require(clue.get_class() == unreal.RPGNarrativeInvestigation.static_class()
                    and clue.get_actor_location() == unreal.Vector(*location)
                    and clue.get_editor_property("target_id") == target
                    and str(clue.get_editor_property("display_name")) == name, "Wrong record: " + label)
        mira = labels["S02_Mira"]
        require(mira.get_class() == unreal.RPGNarrativeNPC.static_class()
                and mira.get_actor_location() == unreal.Vector(*MIRA)
                and str(mira.get_editor_property("display_name")) == MIRA_NAME
                and list(mira.get_editor_property("dialogue_entry_ids")) == ENTRIES, "Wrong Mira contact.")
        if new:
            require(hashlib.sha256(path.read_bytes()).hexdigest() == before, "The source map changed during authoring.")
            require(unreal.EditorLoadingAndSavingUtils.save_map(world, MAP), "Intro1 map save failed.")
            result["saved"] = True
        result.update(passed=True, targets=[value[0] for value in RECORDS.values()], entries=ENTRIES,
                      map_sha256=hashlib.sha256(path.read_bytes()).hexdigest())
    finally:
        proof.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")


main()
