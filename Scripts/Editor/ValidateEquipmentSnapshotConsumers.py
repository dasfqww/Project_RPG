"""Compile authored equipment consumers in memory. Never save content assets."""

import unreal


ASSETS = (
    "/Game/Blueprints/Character/Player/BP_Player",
    "/Game/Blueprints/Character/Player/Abilities/DefaultAttack/GA_Player_DefaultAttack",
    "/Game/Blueprints/Character/Player/Abilities/Skill/GA_Player_JumpSmash",
    "/Game/Blueprints/Character/Player/Abilities/Skill/GA_Player_ChargeSlash",
    "/Game/Blueprints/Character/Player/Abilities/Skill/GA_Player_WhirlWind",
    "/Game/Blueprints/Character/Player/Abilities/Skill/GA_PlayerSkill_AssultBlade",
    "/Game/Blueprints/Weapon/BP_PlayerWeapon",
    "/Game/Blueprints/Character/Player/Abilities/GA_SpawnWeapon",
    "/Game/Blueprints/Character/Player/Abilities/GA_Spawn_GreatSword",
    "/Game/Blueprints/Character/Player/Abilities/GA_Player_EquipGreatSword",
    "/Game/Blueprints/Character/Player/Abilities/GA_Player_UnequipGreatSword",
    "/Game/Blueprints/Character/NPC/BP_RaidNPCBase",
    "/Game/Blueprints/Character/NPC/SwordMan/BP_RaidSwordMan",
)


def main():
    editor_assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    for path in ASSETS:
        blueprint = editor_assets.load_asset(path)
        if not isinstance(blueprint, unreal.Blueprint):
            raise RuntimeError("Missing equipment consumer Blueprint: " + path)
        succeeded = unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
        status = blueprint.get_editor_property("status")
        unreal.log(f"EQUIPMENT_SNAPSHOT_BP {path} status={status}")
        if not succeeded:
            raise RuntimeError(f"Equipment consumer did not compile: {path} ({status})")
    unreal.log(f"EQUIPMENT_SNAPSHOT_BP COMPLETE Count={len(ASSETS)} NoAssetsSaved=1")


main()
