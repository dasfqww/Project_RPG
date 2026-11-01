"""Make the existing player death trigger server-initiated (no graph rewrite)."""
import unreal

PATH = "/Game/Blueprints/Character/Player/Abilities/GA_Player_Death"
blueprint = unreal.EditorAssetLibrary.load_asset(PATH)
if not isinstance(blueprint, unreal.Blueprint):
    raise RuntimeError("Missing player death Blueprint")
death = unreal.get_default_object(blueprint.generated_class())
triggers = death.get_editor_property("ability_triggers")
if not any('TagName="Shared.Status.Death"' in entry.export_text() and
           "OwnedTagAdded" in entry.export_text() for entry in triggers):
    raise RuntimeError("Player death no longer has the expected death tag trigger")
policy = unreal.GameplayAbilityNetExecutionPolicy.SERVER_INITIATED
security = unreal.GameplayAbilityNetSecurityPolicy.SERVER_ONLY
if (death.get_editor_property("net_execution_policy") != policy or
        death.get_editor_property("net_security_policy") != security):
    death.set_editor_property("net_execution_policy", policy)
    # Remote clients render the server's death, but cannot request or terminate it.
    death.set_editor_property("net_security_policy", security)
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    if not unreal.EditorAssetLibrary.save_loaded_asset(blueprint, only_if_is_dirty=False):
        raise RuntimeError("Could not save player death policy")
if unreal.get_default_object(blueprint.generated_class()).get_editor_property("net_execution_policy") != policy:
    raise RuntimeError("Player death policy did not persist")
if unreal.get_default_object(blueprint.generated_class()).get_editor_property("net_security_policy") != security:
    raise RuntimeError("Player death security policy did not persist")
unreal.log_warning("RAID_DEATH_CONFIG COMPLETE ServerInitiated=1 ServerOnlySecurity=1 ExistingGraphPreserved=1")
