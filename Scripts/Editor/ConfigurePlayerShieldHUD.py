"""Author the owner's shield amount/countdown immediately above the existing HP module.

Read-only by default; -RPGConfigureShieldHUD saves only WBP_PlayerVitals.
Use an owned Content overlay, then reload in a fresh process before installation.
"""
import copy
import hashlib
import json
from pathlib import Path
import runpy
import unreal
from toolset_registry.helpers import require_editable


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def main():
    workspace = Path(__file__).resolve().parents[2]
    _, switches, params = unreal.SystemLibrary.parse_command_line(unreal.SystemLibrary.get_command_line())
    params = {str(k).lower(): str(v) for k, v in params.items()}
    apply = any(str(s).lower() == "rpgconfigureshieldhud" for s in switches)
    output = Path(params["rpgshieldhudproof"])
    require(output.is_absolute(), "Use an absolute result path")
    require(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is None, "Preserve PIE")
    path = "/Game/Blueprints/UI/PlayerHUD/Modules/WBP_PlayerVitals"
    assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    module = assets.load_asset(path)
    require(isinstance(module, unreal.WidgetBlueprint), "Missing existing Vitals module")
    require(path not in {p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}, "Preserve dirty Vitals")
    helpers = runpy.run_path(str(workspace / "Scripts/Editor/ConfigureGreatSwordFourthSkillSlot.py"), run_name="shield_layout_helpers")
    before = helpers["snapshot_widgets"](unreal, module)
    infos = helpers["widget_infos"](unreal, module)
    require(all(n in before for n in ("Health_RPGSizeBox_Base", "HealthBar", "ManaBar", "HealthText", "ManaText", "PlayerDebuffs")), "Review changed Vitals controls")
    parent = infos["Health_RPGSizeBox_Base"].get_editor_property("parent")
    require(isinstance(parent, unreal.CanvasPanel), "Health must remain on the authored canvas")
    layout = copy.deepcopy(before["Health_RPGSizeBox_Base"]["slot_properties"]["LayoutData"])
    require(layout["anchors"]["minimum"] == layout["anchors"]["maximum"], "Review stretched health anchor")
    layout["offsets"].update(top=layout["offsets"]["top"] - 34, right=390, bottom=28)
    target = {"LayoutData": layout, "bAutoSize": False, "ZOrder": 3}
    native_class = unreal.load_class(None, "/Script/Project_RPG.RPGPlayerShieldWidget")
    require(native_class is not None, "Compile the native shield module before authoring")
    changed = "PlayerShield" not in before
    content = Path(unreal.Paths.project_content_dir()).resolve()
    disk = Path(unreal.Paths.project_content_dir()) / "Blueprints/UI/PlayerHUD/Modules/WBP_PlayerVitals.uasset"
    original = hashlib.sha256(disk.read_bytes()).hexdigest()
    protected = [workspace / "Content/Blueprints/UI/PlayerHUD/WBP_PlayerHUD.uasset",
                 workspace / "Content/Blueprints/UI/TemplateWidget/TPWBP_StatusBar.uasset"]
    protected_hashes = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in protected}
    if not changed:
        entry = before["PlayerShield"]
        require(entry["class"] == native_class.get_path_name(), "Preserve customized shield class")
        require(all(entry["slot_properties"][key] == value for key, value in target.items()), "Preserve customized shield placement")
    elif apply:
        require(content in disk.resolve().parents, "Do not author a linked original Vitals asset")
        require(any(" / equipment-shield-hud-20261103 |" in row and "진행 중" in row for row in
                    (workspace / "Docs/Online_RPG_Development_Roadmap.md").read_text(encoding="utf-8-sig").splitlines()), "Missing current owner reservation")
        require_editable(module)
        tool = unreal.get_default_object(unreal.UMGToolSet)
        added = tool.call_method("AddWidget", (module, native_class, "PlayerShield", parent, -1))
        require(isinstance(added.get_editor_property("slot"), unreal.CanvasPanelSlot), "Could not add shield module")
        require(unreal.ToolsetLibrary.set_object_properties(added.get_editor_property("slot"), json.dumps(target)), "Could not set shield placement")
        unreal.BlueprintEditorLibrary.compile_blueprint(module)
        require(module.get_editor_property("status") == unreal.BlueprintStatus.BS_UP_TO_DATE, "Vitals compile failed")
        after = helpers["snapshot_widgets"](unreal, module)
        require(set(after) == set(before) | {"PlayerShield"}, "Unexpected widget additions/removals")
        for name, old in before.items():
            old, current = copy.deepcopy(old), copy.deepcopy(after[name])
            if name == parent.get_name():
                old["properties"].pop("Slots", None)
                current["properties"].pop("Slots", None)
            require(old == current, "Existing Vitals control changed: " + name)
        require(hashlib.sha256(disk.read_bytes()).hexdigest() == original, "Target changed during authoring")
        require(assets.save_loaded_asset(module, only_if_is_dirty=False), "Could not save Vitals")
    require(all(hashlib.sha256(p.read_bytes()).hexdigest() == digest for p, digest in protected_hashes.items()), "Protected HUD/template changed")
    result = {"passed": True, "apply": apply, "needs_change": changed and not apply,
              "saved_assets": [path] if apply and changed else [], "target_slot": target,
              "widget_count": len(helpers["snapshot_widgets"](unreal, module)),
              "sha256": hashlib.sha256(disk.read_bytes()).hexdigest(), "protected_unchanged": True}
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    unreal.log("PLAYER_SHIELD_HUD_LAYOUT PASS Saved=" + str(len(result["saved_assets"])))


if __name__ == "__main__":
    main()
