// Fill out your copyright notice in the Description page of Project Settings.

#include "RPGAbilityTags.h"

namespace RPGGameplayTags
{
	/*Player Tags*/
	UE_DEFINE_GAMEPLAY_TAG(Player_Ability_Equip_GreatSword, "Player.Ability.Equip.GreatSword");
	UE_DEFINE_GAMEPLAY_TAG(Player_Ability_Unequip_GreatSword, "Player.Ability.Unequip.GreatSword");
	UE_DEFINE_GAMEPLAY_TAG(Player_Ability_Attack_Light_GreatSword, "Player.Ability.Attack.Light.GreatSword");
	UE_DEFINE_GAMEPLAY_TAG(Player_Ability_Attack_Heavy_GreatSword, "Player.Ability.Attack.Heavy.GreatSword");
	UE_DEFINE_GAMEPLAY_TAG(Player_Ability_HitPause, "Player.Ability.HitPause");
	UE_DEFINE_GAMEPLAY_TAG(Player_Ability_Roll, "Player.Ability.Roll");
	UE_DEFINE_GAMEPLAY_TAG(Player_Ability_Block, "Player.Ability.Block");
	UE_DEFINE_GAMEPLAY_TAG(Player_Ability_TargetLock, "Player.Ability.TargetLock");
	UE_DEFINE_GAMEPLAY_TAG(Player_Ability_PickUp_Instant, "Player.Ability.PickUp.Instant");
	UE_DEFINE_GAMEPLAY_TAG(Player_Ability_PickUp_Hold, "Player.Ability.PickUp.Hold");
	UE_DEFINE_GAMEPLAY_TAG(Player_Ability_Rage, "Player.Ability.Rage");
	UE_DEFINE_GAMEPLAY_TAG(Player_Ability_Skill_AssultBlade, "Player.Ability.Skill.AssultBlade");
	UE_DEFINE_GAMEPLAY_TAG(Player_Ability_Skill_WhirlWind, "Player.Ability.Skill.WhirlWind");
	UE_DEFINE_GAMEPLAY_TAG(Player_Ability_Skill_JumpSmash, "Player.Ability.Skill.JumpSmash");
	UE_DEFINE_GAMEPLAY_TAG(Player_Ability_Skill_Charge, "Player.Ability.Skill.Charge");
	UE_DEFINE_GAMEPLAY_TAG(Player_Ability_Skill_GainIdentity, "Player.Ability.Skill.GainIdentity");
	UE_DEFINE_GAMEPLAY_TAG(Player_Ability_IdentitySkill, "Player.Ability.IdentitySkill");
	UE_DEFINE_GAMEPLAY_TAG(Player_Ability_RestoreHealth, "Player.Ability.RestoreHealth");

	UE_DEFINE_GAMEPLAY_TAG(Player_CoolDown_Skill_AssultBlade, "Player.CoolDown.Skill.AssultBlade");
	UE_DEFINE_GAMEPLAY_TAG(Player_CoolDown_Skill_JumpSmash, "Player.CoolDown.Skill.JumpSmash");
	UE_DEFINE_GAMEPLAY_TAG(Player_CoolDown_Skill_WhirlWind, "Player.CoolDown.Skill.WhirlWind");
	UE_DEFINE_GAMEPLAY_TAG(Player_CoolDown_Skill_ChargeSlash, "Player.CoolDown.Skill.ChargeSlash");

	UE_DEFINE_GAMEPLAY_TAG(Player_Weapon_GreatSword, "Player.Weapon.GreatSword");

	UE_DEFINE_GAMEPLAY_TAG(Player_Event_Equip_GreatSword, "Player.Event.Equip.GreatSword");
	UE_DEFINE_GAMEPLAY_TAG(Player_Event_Unquip_GreatSword, "Player.Event.Unequip.GreatSword");
	UE_DEFINE_GAMEPLAY_TAG(Player_Event_HitPause, "Player.Event.HitPause");
	UE_DEFINE_GAMEPLAY_TAG(Player_Event_SuccessfulBlock, "Player.Event.SuccessfulBlock");
	UE_DEFINE_GAMEPLAY_TAG(Player_Event_SwitchTarget_Left, "Player.Event.SwitchTarget.Left");
	UE_DEFINE_GAMEPLAY_TAG(Player_Event_SwitchTarget_Right, "Player.Event.SwitchTarget.Right");
	UE_DEFINE_GAMEPLAY_TAG(Player_Event_ActivateRage, "Player.Event.ActivateRage");
	UE_DEFINE_GAMEPLAY_TAG(Player_Event_AOE, "Player.Event.AOE");
	UE_DEFINE_GAMEPLAY_TAG(Player_Event_ConsumeItems, "Player.Event.ConsumeItems");

	UE_DEFINE_GAMEPLAY_TAG(Player_Status_JumpToFinisher, "Player.Status.JumpToFinisher");
	UE_DEFINE_GAMEPLAY_TAG(Player_Status_Rolling, "Player.Status.Rolling");
	UE_DEFINE_GAMEPLAY_TAG(Player_Status_Blocking, "Player.Status.Blocking");
	UE_DEFINE_GAMEPLAY_TAG(Player_Status_Charging, "Player.Status.Charging");
	UE_DEFINE_GAMEPLAY_TAG(Player_Status_Holding, "Player.Status.Holding");
	UE_DEFINE_GAMEPLAY_TAG(Player_Status_TargetLock, "Player.Status.TargetLock");
	UE_DEFINE_GAMEPLAY_TAG(Player_Status_Rage_Activating, "Player.Status.Rage.Activating");
	UE_DEFINE_GAMEPLAY_TAG(Player_Status_Rage_Active, "Player.Status.Rage.Active");
	UE_DEFINE_GAMEPLAY_TAG(Player_Status_Rage_Full, "Player.Status.Rage.Full");
	UE_DEFINE_GAMEPLAY_TAG(Player_Status_Rage_None, "Player.Status.Rage.None");
	UE_DEFINE_GAMEPLAY_TAG(Player_Status_Identity_Full, "Player.Status.Identity.Full");
	UE_DEFINE_GAMEPLAY_TAG(Player_Status_Identity_None, "Player.Status.Identity.None");

	UE_DEFINE_GAMEPLAY_TAG(Player_SetByCaller_AttackType_Light, "Player.SetByCaller.AttackType.Light");
	UE_DEFINE_GAMEPLAY_TAG(Player_SetByCaller_AttackType_Heavy, "Player.SetByCaller.AttackType.Heavy");

	/*Enemy Tags*/
	UE_DEFINE_GAMEPLAY_TAG(NPC_Ability_Melee, "NPC.Ability.Melee");
	UE_DEFINE_GAMEPLAY_TAG(NPC_Ability_MeleeAttack, "NPC.Ability.MeleeAttack");
	UE_DEFINE_GAMEPLAY_TAG(NPC_Ability_Ranged, "NPC.Ability.Ranged");
	UE_DEFINE_GAMEPLAY_TAG(NPC_Ability_SummonNPCs, "NPC.Ability.SummonNPCs");
	UE_DEFINE_GAMEPLAY_TAG(NPC_Ability_Berserk, "NPC.Ability.Berserk");
	UE_DEFINE_GAMEPLAY_TAG(NPC_Ability_BerserkAttack, "NPC.Ability.BerserkAttack");
	UE_DEFINE_GAMEPLAY_TAG(NPC_Ability_DropItem, "NPC.Ability.DropItem");

	UE_DEFINE_GAMEPLAY_TAG(NPC_Weapon, "NPC.Weapon");

	UE_DEFINE_GAMEPLAY_TAG(NPC_Event_SummonNPCs, "NPC.Event.SummonNPCs");
	UE_DEFINE_GAMEPLAY_TAG(NPC_Event_Berserk, "NPC.Event.Berserk");

	UE_DEFINE_GAMEPLAY_TAG(NPC_Status_Strafing, "NPC.Status.Strafing");
	UE_DEFINE_GAMEPLAY_TAG(NPC_Status_UnderAttack, "NPC.Status.UnderAttack");
	UE_DEFINE_GAMEPLAY_TAG(NPC_Status_Unblockable, "NPC.Status.Unblockable");
	UE_DEFINE_GAMEPLAY_TAG(NPC_Status_Berserk_Activating, "NPC.Status.Berserk.Activating");
	UE_DEFINE_GAMEPLAY_TAG(NPC_Status_Berserk_Active, "NPC.Status.Berserk.Active");
}
