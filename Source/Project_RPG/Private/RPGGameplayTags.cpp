// Fill out your copyright notice in the Description page of Project Settings.


#include "RPGGameplayTags.h"

namespace RPGGameplayTags
{
	/** Input Tags **/
	UE_DEFINE_GAMEPLAY_TAG(InputTag_Move, "InputTag.Move");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_Look, "InputTag.Look");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_CameraZoom, "InputTag.CameraZoom");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_ShowEquipmentWidget, "InputTag.ShowEquipmentWidget");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_EquipWeapon, "InputTag.EquipWeapon");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_UnequipWeapon, "InputTag.UnequipWeapon");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_LightAttack_GreatSword, "InputTag.LightAttack.GreatSword");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_HeavyAttack_GreatSword, "InputTag.HeavyAttack.GreatSword");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_Roll, "InputTag.Roll");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_SwitchTarget, "InputTag.SwitchTarget");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_Skill1, "InputTag.Skill1");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_Skill2, "InputTag.Skill2");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_Skill3, "InputTag.Skill3");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_IdentitySkill, "InputTag.IdentitySkill");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_ToggleMenu, "InputTag.ToggleMenu");

	UE_DEFINE_GAMEPLAY_TAG(InputTag_UseQuickSlotF1, "InputTag.UseQuickSlotF1");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_UseQuickSlot1, "InputTag.UseQuickSlot1");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_UseQuickSlot2, "InputTag.UseQuickSlot2");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_UseQuickSlot3, "InputTag.UseQuickSlot3");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_UseQuickSlot4, "InputTag.UseQuickSlot4");

	UE_DEFINE_GAMEPLAY_TAG(InputTag_PickUp_Items, "InputTag.PickUp.Items");

	//UE_DEFINE_GAMEPLAY_TAG(InputTag_MustBeCharge, "InputTag.MustBeCharge");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_MustBeHeld, "InputTag.MustBeHeld");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_MustBeHeld_Block, "InputTag.MustBeHeld.Block");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_MustBeHeld_Charge, "InputTag.MustBeHeld.Charge");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_MustBeHeld_Charge_IdentitySkill, "InputTag.MustBeHeld.Charge.IdentitySkill");

	UE_DEFINE_GAMEPLAY_TAG(InputTag_Toggleable, "InputTag.Toggleadble");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_Toggleable_TargetLock, "InputTag.Toggleadble.TargetLock");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_Toggleable_Rage, "InputTag.Toggleadble.Rage");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_Toggleable_ToggleSkill, "InputTag.Toggleadble.ToggleSkill");

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
	//UE_DEFINE_GAMEPLAY_TAG(Player_Ability_Skill_CoolDown, "Player.Ability.Skill.CoolDown");
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

	/*Shared Tags*/
	UE_DEFINE_GAMEPLAY_TAG(Shared_Ability_HitReact, "Shared.Ability.HitReact");
	UE_DEFINE_GAMEPLAY_TAG(Shared_Ability_Death, "Shared.Ability.Death");

	UE_DEFINE_GAMEPLAY_TAG(Shared_Event_Hit, "Shared.Event.Hit");
	UE_DEFINE_GAMEPLAY_TAG(Shared_Event_HitReact, "Shared.Event.HitReact");
	UE_DEFINE_GAMEPLAY_TAG(Shared_Event_SpawnProjectile, "Shared.Event.SpawnProjectile");

	UE_DEFINE_GAMEPLAY_TAG(Shared_SetByCaller_BaseDamage, "Shared.SetByCaller.BaseDamage");
	UE_DEFINE_GAMEPLAY_TAG(Shared_SetByCaller_IdentityGain, "Shared.SetByCaller.IdentityGain");

	UE_DEFINE_GAMEPLAY_TAG(Shared_Status_Death, "Shared.Status.Death");
	UE_DEFINE_GAMEPLAY_TAG(Shared_Status_HitReact_Front, "Shared.Status.Front");
	UE_DEFINE_GAMEPLAY_TAG(Shared_Status_HitReact_Back, "Shared.Status.Back");
	UE_DEFINE_GAMEPLAY_TAG(Shared_Status_HitReact_Left, "Shared.Status.Left");
	UE_DEFINE_GAMEPLAY_TAG(Shared_Status_HitReact_Right, "Shared.Status.Right");
	UE_DEFINE_GAMEPLAY_TAG(Shared_Status_Invincible, "Shared.Status.Invincible");

	/** Game Data tags **/
	UE_DEFINE_GAMEPLAY_TAG(GameData_Level_SurvivalGameModeMap, "GameData.Level.SurvivalGameModeMap");
	UE_DEFINE_GAMEPLAY_TAG(GameData_Level_LobbyMap, "GameData.Level.LobbyMap");
	UE_DEFINE_GAMEPLAY_TAG(GameData_Level_MainMenuMap, "GameData.Level.MainMenuMap");
	UE_DEFINE_GAMEPLAY_TAG(GameData_Level_BossBattleMap, "GameData.Level.BossBattleMap");

	/* Item tags */
	UE_DEFINE_GAMEPLAY_TAG(GameItem_Equipment_Weapon_GreatSword, "GameItem.Equipment.Weapon.GreatSword");

	UE_DEFINE_GAMEPLAY_TAG(GameItem_Equipment_Helm_Default, "GameItem.Equipment.Helm.Default");
	UE_DEFINE_GAMEPLAY_TAG(GameItem_Equipment_Shoulder_Default, "GameItem.Equipment.Shoulder.Default");
	UE_DEFINE_GAMEPLAY_TAG(GameItem_Equipment_Chest_Default, "GameItem.Equipment.Chest.Default");
	UE_DEFINE_GAMEPLAY_TAG(GameItem_Equipment_Pants_Default, "GameItem.Equipment.Pants.Default");
	UE_DEFINE_GAMEPLAY_TAG(GameItem_Equipment_Glove_Default, "GameItem.Equipment.Glove.Default");

	UE_DEFINE_GAMEPLAY_TAG(GameItem_Consume_Potion_Red_Small, "GameItem.Consume.Potion.Red.Small");
	UE_DEFINE_GAMEPLAY_TAG(GameItem_Consume_Potion_Red_Large, "GameItem.Consume.Potion.Red.Large");
	
	UE_DEFINE_GAMEPLAY_TAG(GameItem_Consume_Potion_Blue_Small, "GameItem.Consume.Potion.Blue.Small");
	UE_DEFINE_GAMEPLAY_TAG(GameItem_Consume_Potion_Blue_Large, "GameItem.Consume.Potion.Blue.Large");

	UE_DEFINE_GAMEPLAY_TAG(GameItem_Craft_fruit, "GameItem.Craft.Fruit");
	UE_DEFINE_GAMEPLAY_TAG(GameItem_Craft_daisy, "GameItem.Craft.Daisy");
	UE_DEFINE_GAMEPLAY_TAG(GameItem_Craft_Blossom, "GameItem.Craft.Blossom");

	UE_DEFINE_GAMEPLAY_TAG(Fragment_GridFragment, "Fragment.GridFragment");
	UE_DEFINE_GAMEPLAY_TAG(Fragment_IconFragment, "Fragment.IconFragment");
	UE_DEFINE_GAMEPLAY_TAG(Fragment_StackableFragment, "Fragment.StackableFragment");
	UE_DEFINE_GAMEPLAY_TAG(Fragment_ConsumableFragment, "Fragment.ConsumableFragment");
	UE_DEFINE_GAMEPLAY_TAG(Fragment_ItemNameFragment, "Fragment.ItemNameFragment");
	UE_DEFINE_GAMEPLAY_TAG(Fragment_PrimaryStatFragment, "Fragment.PrimaryStatFragment");

	UE_DEFINE_GAMEPLAY_TAG(Fragment_StatMod_1, "Fragment.StatMod_1");
	UE_DEFINE_GAMEPLAY_TAG(Fragment_StatMod_2, "Fragment.StatMod_2");
	UE_DEFINE_GAMEPLAY_TAG(Fragment_StatMod_3, "Fragment.StatMod_3");

#pragma region UILayer

	UE_DEFINE_GAMEPLAY_TAG(Widget_Layer_Game, "Widget.Layer.Game");
	UE_DEFINE_GAMEPLAY_TAG(Widget_Layer_GameMenu, "Widget.Layer.GameMenu");
	UE_DEFINE_GAMEPLAY_TAG(Widget_Layer_Menu, "Widget.Layer.MenuLayer");
	UE_DEFINE_GAMEPLAY_TAG(Widget_Layer_Modal, "Widget.Layer.Modal");

#pragma endregion

}