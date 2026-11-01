"""Only Guardian drop BP execution/security policies; save requires explicit opt-in."""
import unreal

path = "/Game/Blueprints/Character/NPC/Gruntling/Guardian/BP_Guardian_DropItemAbility"
blueprint = unreal.load_asset(path)
ability_class = unreal.load_class(None, path + ".BP_Guardian_DropItemAbility_C")
if not isinstance(blueprint, unreal.Blueprint) or not ability_class:
    raise RuntimeError("Saved Guardian drop BP is missing")
cdo = unreal.get_default_object(ability_class)
desired = {
    "net_execution_policy": unreal.GameplayAbilityNetExecutionPolicy.SERVER_ONLY,
    "net_security_policy": unreal.GameplayAbilityNetSecurityPolicy.SERVER_ONLY,
}
apply = "-rpgapplynpcdropauthority" in unreal.SystemLibrary.get_command_line().lower()
changes = [key for key, value in desired.items() if cdo.get_editor_property(key) != value]
if changes and apply:
    blueprint.modify()
    cdo.modify()
    for key, value in desired.items():
        cdo.set_editor_property(key, value)
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    if "ERROR" in str(blueprint.get_editor_property("status")).upper():
        raise RuntimeError("Guardian drop BP compile failed; refusing save")
    if not unreal.EditorAssetLibrary.save_loaded_asset(blueprint, only_if_is_dirty=False):
        raise RuntimeError("Could not save Guardian drop BP")
    unreal.log("RPG_NPC_DROP_AUTHORITY APPLIED OnlyGuardianDropBPSaved=1 TableAndGraphUnchanged=1")
elif changes:
    unreal.log("RPG_NPC_DROP_AUTHORITY PREFLIGHT RequiresApply=1 NoAssetsSaved=1")
else:
    unreal.log("RPG_NPC_DROP_AUTHORITY VERIFIED ServerExecutionOnly=1 ServerSecurityOnly=1 NoAssetsSaved=1")
