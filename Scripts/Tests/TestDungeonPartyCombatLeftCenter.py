"""Isolated saved-WBP/empty-snapshot test; not a gameplay/network test.

The native snapshot setter is intentionally not Blueprint/Python writable. Do
not weaken it for this test. Populated/reconnect snapshots need the native or
authenticated gameplay harness; this test exercises the actual WBP override's
empty branch so designer placeholder names/health cannot persist at runtime.
No assets, live player state, or backend records are changed.
"""

import json

import unreal


def check(condition, message):
    if not condition:
        raise AssertionError(message)
    unreal.log("PARTY_WBP_CHECK " + message)


def run():
    widget_class = unreal.load_class(None, "/Game/Blueprints/UI/Party/WBP_DungeonPartyCombat_LeftCenter.WBP_DungeonPartyCombat_LeftCenter_C")
    check(widget_class is not None, "saved WBP class loads")
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    factory = unreal.get_default_object(unreal.load_class(None, "/Script/UMG.WidgetBlueprintLibrary"))
    widget = factory.call_method("Create", args=(world, widget_class, None))
    check(widget is not None, "isolated widget instance initializes")
    get = widget.get_editor_property
    check(get("PartyMember0") is not None and get("PartyHealth3") is not None, "designer variables resolve on runtime instance")
    check(len(widget.call_method("GetPartyMembers")) == 0, "fresh instance has no real party data")
    check(any(str(get("PartyMember" + str(i)).get_editor_property("text")) for i in range(4)), "designer labels exist before runtime refresh")
    widget.call_method("OnPartyPresentationUpdated")
    for i in range(4):
        suffix = str(i)
        check(get("PartyRow" + suffix).get_editor_property("visibility") == unreal.SlateVisibility.COLLAPSED, "empty row " + suffix + " collapses")
        check(str(get("PartyMember" + suffix).get_editor_property("text")) == "", "empty row " + suffix + " clears placeholder name")
        check(get("PartyHealth" + suffix).get_editor_property("percent") == 0, "empty row " + suffix + " clears placeholder health")
        check(get("PartyHealthHeight" + suffix).get_editor_property("visibility") == unreal.SlateVisibility.COLLAPSED, "empty row " + suffix + " hides health wrapper")
    check(all(get("PartyNotice" + str(i)).get_editor_property("visibility") == unreal.SlateVisibility.COLLAPSED for i in range(3)), "empty notice feed collapses")
    check(get("PartyOverflow").get_editor_property("visibility") == unreal.SlateVisibility.COLLAPSED, "empty snapshot clears overflow hint")
    check("0" in str(get("PartyHeader").get_editor_property("text")), "common header formatter updates")
    unreal.log("PARTY_WBP_RESULT " + json.dumps({"passed": True, "actualWbpBlueprintEvent": True,
        "snapshot": "empty-only", "attachedToViewport": False, "populatedParty": False,
        "iris": False, "http": False, "steam": False}))


run()
