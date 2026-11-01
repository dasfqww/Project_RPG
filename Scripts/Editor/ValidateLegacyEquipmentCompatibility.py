"""Compile legacy equipment references in memory; never save content assets."""

import unreal


ASSETS = (
    "/Game/Blueprints/Character/Player/BP_Player",
    "/Game/Blueprints/Character/Player/Abilities/DefaultAttack/GA_Player_DefaultAttack",
    "/Game/Blueprints/Character/Player/Abilities/Skill/GA_Player_JumpSmash",
    "/Game/Blueprints/Character/Player/Abilities/Skill/GA_Player_ChargeSlash",
    "/Game/Blueprints/Character/Player/Abilities/Skill/GA_Player_WhirlWind",
    "/Game/Blueprints/Character/Player/Abilities/Skill/GA_PlayerSkill_AssultBlade",
    "/Game/Blueprints/UI/Equipment/WBP_EquipmentWidget",
    "/Game/Blueprints/UI/Equipment/WBP_EquipmentGridSlot_Glove",
    "/Game/Blueprints/UI/Equipment/WBP_EquipmentGridSlot_Chest",
    "/Game/Blueprints/UI/Equipment/WBP_EquipmentGridSlot_Helm",
    "/Game/Blueprints/UI/Equipment/WBP_EquipmentGridSlot_Pants",
    "/Game/Blueprints/UI/Equipment/WBP_EquipmentGridSlot_Shoulder",
    "/Game/Blueprints/UI/Equipment/Projection/WBP_EquipmentProjection",
    # Derived from the same base character with retained component names/types.
    "/Game/Blueprints/Character/NPC/BP_RaidNPCBase",
    "/Game/Blueprints/Character/NPC/SwordMan/BP_RaidSwordMan",
)


def main():
    editor_assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    for path in ASSETS:
        blueprint = editor_assets.load_asset(path)
        if not isinstance(blueprint, unreal.Blueprint):
            raise RuntimeError("Missing equipment compatibility Blueprint: " + path)
        succeeded = unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
        status = blueprint.get_editor_property("status")
        unreal.log(f"LEGACY_EQUIPMENT_BP {path} status={status}")
        if not succeeded:
            raise RuntimeError(f"Equipment compatibility compile failed: {path} ({status})")
    unreal.log(f"LEGACY_EQUIPMENT_BP COMPLETE Count={len(ASSETS)} NoAssetsSaved=1")


main()
