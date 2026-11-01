"""Read only: SwordMan attack animation and existing warning cue contracts."""
import unreal

ROOT = "/Game/Blueprints/Character/NPC/SwordMan"
PREFIX = "RAID_ATTACK_PRESENTATION_AUDIT"

for name in ("AM_SwordMan_Attack", "AM_SwordMan_AttackBerserk"):
    montage = unreal.load_asset(ROOT + "/AnimBP/" + name)
    if not isinstance(montage, unreal.AnimMontage):
        raise RuntimeError("Missing SwordMan attack montage: " + name)
    unreal.log(f"{PREFIX} MONTAGE Name={name} Length={montage.get_play_length()}")
    for index in range(montage.get_num_sections()):
        unreal.log(f"{PREFIX} SECTION Index={index} Name={montage.get_section_name(index)}")
    for notify in unreal.AnimationLibrary.get_animation_notify_events(montage):
        unreal.log(f"{PREFIX} NOTIFY {notify.export_text()}")

cue = unreal.load_asset("/Game/GameplayCue/GC_NPC_AttackWarning")
if not isinstance(cue, unreal.Blueprint):
    raise RuntimeError("Missing existing NPC attack warning cue")
unreal.log(f"{PREFIX} CUE Parent={cue.get_blueprint_parent_class().get_path_name()}")
default = unreal.get_default_object(cue.generated_class())
for prop in ("gameplay_cue_tag", "auto_destroy_on_remove"):
    try:
        value = default.get_editor_property(prop)
        unreal.log(f"{PREFIX} CUE_PROPERTY Name={prop} Value={value}")
    except Exception as error:
        if "Failed to find property" not in str(error):
            raise
for graph in unreal.BlueprintEditorLibrary.list_graphs(cue):
    for node in unreal.BlueprintGraphEditor.get_graph_editor(graph).list_all_nodes():
        unreal.log(f"{PREFIX} CUE_NODE Graph={graph.get_name()} Title={node.get_node_title()}")
unreal.log(f"{PREFIX} COMPLETE NoAssetsSaved=1")
