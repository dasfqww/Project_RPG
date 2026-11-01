"""Read-only saved Controller -> WBP selection check in an isolated commandlet.

Does not repeat the empty-snapshot suite or modify assets, admissions or player
state. Widget creation is unattached; it is not party gameplay/Iris proof.
"""

import json

import unreal


def check(condition, description):
    if not condition:
        raise AssertionError(description)
    unreal.log("PARTY_SELECTION_CHECK " + description)


def run():
    controller_path = "/Game/Blueprints/Controller/BP_PlayerController.BP_PlayerController_C"
    expected_widget = "/Game/Blueprints/UI/Party/WBP_DungeonPartyCombat_LeftCenter.WBP_DungeonPartyCombat_LeftCenter_C"
    controller = unreal.load_class(None, controller_path)
    check(controller is not None, "saved controller class loads")
    selected = unreal.get_default_object(controller).get_editor_property("DungeonPartyCombatWidgetClass")
    check(selected is not None and selected.get_path_name() == expected_widget, "saved controller selects left-centre WBP")
    check(isinstance(unreal.get_default_object(selected), unreal.RPGDungeonPartyCombatWidget), "selected WBP has native party HUD parent")
    for mode_name in ["BP_GameModeBase", "BP_BossBattleGameMode"]:
        mode_path = "/Game/Blueprints/GameMode/" + mode_name + "." + mode_name + "_C"
        mode = unreal.load_class(None, mode_path)
        check(mode is not None and unreal.get_default_object(mode).get_editor_property("PlayerControllerClass") == controller,
              mode_name + " retains configured controller")
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    factory = unreal.get_default_object(unreal.load_class(None, "/Script/UMG.WidgetBlueprintLibrary"))
    widget = factory.call_method("Create", args=(world, selected, None))
    check(widget is not None and widget.get_class() == selected, "controller-selected WBP instantiates")
    unreal.log("PARTY_SELECTION_RESULT " + json.dumps({
        "passed": True, "controller": controller_path, "selectedWidget": selected.get_path_name(),
        "savedAssetReload": True, "unattachedWidgetCreated": True,
        "partyGameplay": False, "iris": False, "http": False, "steam": False,
    }))


run()
