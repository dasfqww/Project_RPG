"""Read-only audit of ordinary NPC startup and authored death graphs. No asset saves."""
import unreal

PREFIX = "RPG_NPC_DEATH_AUDIT"
ROOT = "/Game/Blueprints/Character/"


def log_property(obj, name):
    value = obj.get_editor_property(name)
    text = value.export_text() if hasattr(value, "export_text") else str(value)
    unreal.log(f"{PREFIX} PROPERTY Object={obj.get_path_name()} Name={name} Value={text}")
    return value


def main():
    unreal.AssetRegistryHelpers.get_asset_registry().wait_for_completion()
    for relative in ("NPC/Gruntling/Guardian/BP_Gruntling_Guardian", "NPC/DummyNPC/BP_Dummy_Gdn"):
        blueprint = unreal.load_asset(ROOT + relative)
        if not isinstance(blueprint, unreal.Blueprint):
            raise RuntimeError(f"Missing NPC Blueprint: {relative}")
        unreal.log(f"{PREFIX} NPC Class={blueprint.generated_class().get_path_name()}")
        default = unreal.get_default_object(blueprint.generated_class())
        startup = log_property(default, "character_start_up_data")
        if not isinstance(startup, unreal.Object):
            startup = unreal.load_asset(str(startup))
        if startup is None:
            raise RuntimeError(f"Missing NPC startup data: {relative}")
        for group in ("activate_on_given_abilities", "reactive_abilities", "start_up_gameplay_effects"):
            for cls in log_property(startup, group):
                if cls:
                    ability = unreal.get_default_object(cls)
                    unreal.log(f"{PREFIX} STARTUP Group={group} Class={cls.get_path_name()}")
                    if isinstance(ability, unreal.GameplayAbility):
                        for prop in ("net_execution_policy", "net_security_policy", "ability_triggers"):
                            log_property(ability, prop)
    for relative in ("Shared/GameplayAbility/GA_NPC_Death_Base", "NPC/Gruntling/GameplayAbility/GA_Guardian_Death"):
        blueprint = unreal.load_asset(ROOT + relative)
        if not isinstance(blueprint, unreal.Blueprint):
            raise RuntimeError(f"Missing death Blueprint: {relative}")
        default = unreal.get_default_object(blueprint.generated_class())
        for name in ("ability_tags", "activation_owned_tags", "cancel_abilities_with_tag",
                     "net_execution_policy", "net_security_policy", "ability_triggers"):
            log_property(default, name)
        for graph in unreal.BlueprintEditorLibrary.list_graphs(blueprint):
            for node in unreal.BlueprintGraphEditor.get_graph_editor(graph).list_all_nodes():
                unreal.log(f"{PREFIX} NODE BP={blueprint.get_name()} Graph={graph.get_name()} Title={node.get_node_title()}")
    unreal.log(f"{PREFIX} COMPLETE ReadOnly=1 NoAssetsSaved=1")


main()
