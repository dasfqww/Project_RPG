"""Author the Intro1 approach arena without loading Project_RPG modules.

Read-only by default; -RPGCreateProgressionIntro1Arena creates a MISSING map.
Existing maps are inspected, never overwritten. This is a spatial lesson asset,
not an admission, objective-completion or reward authority. It deliberately uses
Engine.GameModeBase until the Intro1 server objective is implemented.

Can run in a closed, content-only editor project with PythonScriptPlugin and
EditorScriptingUtilities enabled. No project plugin or Blueprint is required.
"""

import hashlib
import json
import os
import pathlib


MAP = "/Game/Maps/Progression/L_ProgressionIntro1"
MARKER = "FirstSlice.Intro1Arena.v1"
METADATA = "RPGIntro1Arena"
PREFIX = "Intro1_"
# Unreal units are centimetres. Cube and cylinder primitives are 100 cm wide.
GEOMETRY = {
    "Floor": ("Cube", (0, 0, -50), (60, 40, 1)),
    "WallNorth": ("Cube", (0, 2050, 150), (62, 1, 3)),
    "WallSouth": ("Cube", (0, -2050, 150), (62, 1, 3)),
    "WallEast": ("Cube", (3050, 0, 150), (1, 40, 3)),
    "WallWest": ("Cube", (-3050, 0, 150), (1, 40, 3)),
    # The central cover leaves two 1500 cm passages. Neither requires jumping.
    "Cover": ("Cube", (-900, 0, 120), (2, 10, 2.4)),
    "StartPad": ("Cylinder", (-2400, 0, 4), (13, 13, 0.08)),
    "ApproachPad": ("Cylinder", (300, 0, 4), (8, 8, 0.08)),
    "AttackPad": ("Cylinder", (1700, 0, 4), (14, 14, 0.08)),
    # These low rails indicate the two routes without obstructing them.
    "RouteNorthA": ("Cube", (-1800, 1000, 5), (8, 0.3, 0.1)),
    "RouteNorthB": ("Cube", (-350, 1000, 5), (9, 0.3, 0.1)),
    "RouteSouthA": ("Cube", (-1800, -1000, 5), (8, 0.3, 0.1)),
    "RouteSouthB": ("Cube", (-350, -1000, 5), (9, 0.3, 0.1)),
}
STARTS = {"PlayerStart" + str(i + 1): (-2400, -450 + i * 300, 110) for i in range(4)}
# Stable spatial IDs for the later server objective. Overlap alone grants nothing.
ZONES = {
    "StartZone": ("Progression.Intro1.Zone.Start", (-2400, 0, 140), (650, 650, 140)),
    "ApproachZone": ("Progression.Intro1.Zone.Approach", (300, 0, 140), (400, 400, 140)),
    "AttackZone": ("Progression.Intro1.Zone.Attack", (1700, 0, 140), (700, 700, 140)),
}
# Numbered world signs use the engine distance-field font. The Korean objective
# instructions belong in the later Slate guide, not in a font without Hangul.
SIGNS = {
    "StartSign": ("01  START", (-2730, 0, 200), (0.25, 0.8, 1.0)),
    "ApproachSign": ("02  APPROACH", (0, 0, 200), (1.0, 0.75, 0.2)),
    "AttackSign": ("03  ATTACK", (1400, 0, 200), (1.0, 0.35, 0.2)),
}


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def xyz(vector):
    return (vector.x, vector.y, vector.z)


def close_vector(vector, expected):
    return all(abs(a - b) < 0.01 for a, b in zip(xyz(vector), expected))


def disk_path(unreal):
    return pathlib.Path(unreal.Paths.project_content_dir()) / (MAP[6:] + ".umap")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else None


def dirty_packages(unreal):
    return {p.get_name() for p in (
        list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
        + list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()))}


def actor_tags(actor):
    return {str(tag) for tag in actor.get_editor_property("tags")}


def inspect(unreal, world, meshes, preview_mode):
    require(unreal.EditorAssetLibrary.get_metadata_tag(world, METADATA) == MARKER,
            "Preserving a map authored elsewhere")
    settings = world.get_world_settings()
    require(settings.get_editor_property("default_game_mode") == preview_mode,
            "Preserving a map with a different gameplay mode")
    require(settings.get_editor_property("force_no_precomputed_lighting"), "Arena lighting policy differs")
    actors = [a for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor)
              if a.get_actor_label().startswith(PREFIX)]
    by_name = {a.get_actor_label()[len(PREFIX):]: a for a in actors}
    expected = set(GEOMETRY) | set(STARTS) | set(ZONES) | set(SIGNS) | {"Sun", "Sky"}
    require(len(actors) == len(by_name) and set(by_name) == expected, "Arena layout differs or has duplicate labels")
    for name, actor in by_name.items():
        require(MARKER in actor_tags(actor), "Arena actor ownership differs: " + name)
    for name, (shape, location, scale) in GEOMETRY.items():
        actor = by_name[name]
        require(isinstance(actor, unreal.StaticMeshActor)
                and close_vector(actor.get_actor_location(), location)
                and close_vector(actor.get_actor_scale3d(), scale)
                and actor.static_mesh_component.static_mesh == meshes[shape], "Geometry differs: " + name)
        require(str(actor.static_mesh_component.get_collision_profile_name()) == "BlockAll",
                "Geometry must block movement: " + name)
    for name, location in STARTS.items():
        actor = by_name[name]
        require(isinstance(actor, unreal.PlayerStart) and close_vector(actor.get_actor_location(), location),
                "Player start differs: " + name)
        rotation = actor.get_actor_rotation()
        require(abs(rotation.yaw) < 0.01 and abs(rotation.pitch) < 0.01 and abs(rotation.roll) < 0.01,
                "Player start must face the course")
    for name, (tag, location, extent) in ZONES.items():
        actor = by_name[name]
        require(isinstance(actor, unreal.TriggerBox) and tag in actor_tags(actor)
                and close_vector(actor.get_actor_location(), location), "Zone identity differs: " + name)
        component = actor.get_editor_property("collision_component")
        require(close_vector(component.get_editor_property("box_extent"), extent)
                and str(component.get_collision_profile_name()) == "Trigger", "Zone bounds/collision differ: " + name)
    for name, (value, location, color) in SIGNS.items():
        actor = by_name[name]
        require(isinstance(actor, unreal.TextRenderActor) and close_vector(actor.get_actor_location(), location),
                "Sign actor differs: " + name)
        component = actor.get_editor_property("text_render")
        require(str(component.get_editor_property("text")) == value
                and abs(component.get_editor_property("world_size") - 70.0) < 0.01, "Sign text/size differs: " + name)
        tint = component.get_editor_property("text_render_color")
        require((tint.r, tint.g, tint.b) == tuple(round(c * 255) for c in color), "Sign color differs: " + name)
    require(isinstance(by_name["Sun"], unreal.DirectionalLight) and isinstance(by_name["Sky"], unreal.SkyLight),
            "Arena light classes differ")
    # A duplicate unowned gameplay actor must not hide outside the label filter.
    all_starts = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.PlayerStart)
    all_zones = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TriggerBox)
    require(len(all_starts) == 4 and len(all_zones) == 3, "Unexpected start/zone actors")
    return {"owned_actors": len(actors), "geometry": len(GEOMETRY), "player_starts": 4,
            "spatial_zones": {tag: {"centre": location, "half_extent": extent}
                              for tag, location, extent in ZONES.values()},
            "preview_mode": preview_mode.get_path_name(), "blocking_floor_cover_and_bounds": True,
            "two_routes_configured": True, "objective_logic_connected": False}


def create(unreal, meshes, preview_mode):
    world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    require(world is not None, "Could not create arena world")
    unreal.EditorAssetLibrary.set_metadata_tag(world, METADATA, MARKER)
    settings = world.get_world_settings()
    settings.set_editor_property("default_game_mode", preview_mode)
    settings.set_editor_property("force_no_precomputed_lighting", True)
    editor_actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    def spawn(cls, name, location, rotation=None, extra_tags=()):
        actor = editor_actors.spawn_actor_from_class(cls, unreal.Vector(*location), rotation or unreal.Rotator())
        require(actor is not None, "Could not create: " + name)
        actor.set_actor_label(PREFIX + name)
        actor.set_editor_property("tags", [unreal.Name(MARKER), *(unreal.Name(t) for t in extra_tags)])
        return actor

    for name, (shape, location, scale) in GEOMETRY.items():
        actor = spawn(unreal.StaticMeshActor, name, location)
        actor.static_mesh_component.set_static_mesh(meshes[shape])
        actor.static_mesh_component.set_collision_profile_name("BlockAll")
        actor.set_actor_scale3d(unreal.Vector(*scale))
    for name, location in STARTS.items():
        spawn(unreal.PlayerStart, name, location)
    for name, (tag, location, extent) in ZONES.items():
        actor = spawn(unreal.TriggerBox, name, location, extra_tags=(tag,))
        component = actor.get_editor_property("collision_component")
        component.set_editor_property("box_extent", unreal.Vector(*extent))
        component.set_collision_profile_name("Trigger")
    for name, (value, location, color) in SIGNS.items():
        actor = spawn(unreal.TextRenderActor, name, location, unreal.Rotator(0, 180, 0))
        component = actor.get_editor_property("text_render")
        component.set_editor_property("text", unreal.Text(value))
        component.set_editor_property("world_size", 70.0)
        component.set_editor_property("horizontal_alignment", unreal.HorizTextAligment.EHTA_CENTER)
        component.set_editor_property("text_render_color", unreal.Color(
            r=round(color[0] * 255), g=round(color[1] * 255), b=round(color[2] * 255), a=255))
    sun = spawn(unreal.DirectionalLight, "Sun", (0, 0, 1800), unreal.Rotator(-55, -30, 0))
    sky = spawn(unreal.SkyLight, "Sky", (0, 0, 1000))
    for light in (sun, sky):
        light.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    return world


def main():
    import unreal

    output = pathlib.Path(os.environ.get("RPG_INTRO1_ARENA_REPORT", ""))
    require(output.is_absolute(), "Set absolute RPG_INTRO1_ARENA_REPORT")
    report = {"map": MAP, "saved_count": 0, "published": False, "gameplay_executed": False,
              "project_module_required": False, "objective_logic_connected": False, "passed": False}
    try:
        apply = "-rpgcreateprogressionintro1arena" in unreal.SystemLibrary.get_command_line().lower().split()
        report["read_only"] = not apply
        require(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is None, "Author outside PIE")
        require(MAP not in dirty_packages(unreal), "Preserving unsaved arena changes")
        path = disk_path(unreal)
        before = digest(path)
        report["before_sha256"] = before
        assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
        exists = assets.does_asset_exist(MAP)
        require(exists == path.is_file(), "Asset registry/disk disagree")
        meshes = {name: assets.load_asset("/Engine/BasicShapes/" + name) for name in ("Cube", "Cylinder")}
        require(all(meshes.values()), "Engine primitive meshes unavailable")
        preview_mode = unreal.load_class(None, "/Script/Engine.GameModeBase")
        require(preview_mode is not None, "Engine preview mode unavailable")
        world = None
        if before:
            world = unreal.EditorLoadingAndSavingUtils.load_map(MAP)
            require(world is not None, "Could not load existing arena")
        elif apply:
            world = create(unreal, meshes, preview_mode)
            report["properties"] = inspect(unreal, world, meshes, preview_mode)
            require(not path.exists(), "Target appeared during authoring; refusing overwrite")
            require(unreal.EditorLoadingAndSavingUtils.save_map(world, MAP), "Could not save arena")
            report["saved_count"] = 1
        if world:
            report["properties"] = inspect(unreal, world, meshes, preview_mode)
        after = digest(path)
        require(not before or before == after, "Existing arena was changed")
        require(MAP not in dirty_packages(unreal), "Arena remains dirty")
        report.update({"sha256": after, "missing_count": int(after is None), "passed": True})
        unreal.log("RPG_INTRO1_ARENA " + json.dumps(report, ensure_ascii=False))
    except Exception as error:
        report["error"] = str(error)
        raise
    finally:
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
