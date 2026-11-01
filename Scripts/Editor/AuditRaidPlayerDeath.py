"""Read-only audit of the player startup/death assets used by bossmap."""
import unreal


def main():
    mode_class = unreal.EditorAssetLibrary.load_blueprint_class(
        "/Game/Blueprints/GameMode/BP_BossBattleGameMode")
    mode = unreal.get_default_object(mode_class)
    player = unreal.get_default_object(mode.get_editor_property("default_pawn_class"))
    startup = player.get_editor_property("character_start_up_data")
    unreal.log_warning("RAID_DEATH_AUDIT STARTUP " + startup.get_path_name())
    for group in ("activate_on_given_abilities", "reactive_abilities"):
        for cls in startup.get_editor_property(group):
            ability = unreal.get_default_object(cls)
            unreal.log_warning("RAID_DEATH_AUDIT ABILITY {} {} Policy={} Triggers={}".format(
                group, cls.get_path_name(), ability.get_editor_property("net_execution_policy"),
                [entry.export_text() for entry in ability.get_editor_property("ability_triggers")]))
    death_class = unreal.EditorAssetLibrary.load_blueprint_class(
        "/Game/Blueprints/Character/Player/Abilities/GA_Player_Death")
    death = unreal.get_default_object(death_class)
    # Generic UObject text export includes graph pin links; generated diagnostics
    # only, never modifies or saves the package being inspected.
    export = unreal.AssetExportTask()
    export.set_editor_property("object", unreal.EditorAssetLibrary.load_asset(
        "/Game/Blueprints/Character/Player/Abilities/GA_Player_Death"))
    export.set_editor_property("exporter", unreal.ObjectExporterT3D())
    export.set_editor_property("filename", unreal.Paths.project_saved_dir() + "Tests/BossMap/PlayerDeathGraph.t3d")
    export.set_editor_property("automated", True)
    export.set_editor_property("prompt", False)
    export.set_editor_property("replace_identical", True)
    if not unreal.Exporter.run_asset_export_task(export):
        raise RuntimeError("Cannot export player death diagnostic graph")
    for name in ("ability_tags", "activation_owned_tags", "cancel_abilities_with_tag",
                 "block_abilities_with_tag", "instancing_policy", "net_execution_policy", "net_security_policy"):
        value = death.get_editor_property(name)
        unreal.log_warning("RAID_DEATH_AUDIT DEATH {}={}".format(
            name, value.export_text() if hasattr(value, "export_text") else str(value)))
    unreal.log_warning("RAID_DEATH_AUDIT COMPLETE ReadOnly=1")


main()
