// Fill out your copyright notice in the Description page of Project Settings.

#include "RPGInputTags.h"

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
	UE_DEFINE_GAMEPLAY_TAG(InputTag_ToggleOptionMenu, "InputTag.ToggleMenu");

	UE_DEFINE_GAMEPLAY_TAG(InputTag_QuickSkill_1, "InputTag.QuickSkill.1");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_QuickSkill_2, "InputTag.QuickSkill.2");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_QuickSkill_3, "InputTag.QuickSkill.3");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_QuickSkill_4, "InputTag.QuickSkill.4");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_QuickSkill_5, "InputTag.QuickSkill.5");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_QuickSkill_6, "InputTag.QuickSkill.6");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_QuickSkill_7, "InputTag.QuickSkill.7");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_QuickSkill_8, "InputTag.QuickSkill.8");

	UE_DEFINE_GAMEPLAY_TAG(InputTag_QuickItem_1, "InputTag.QuickItem.1");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_QuickItem_2, "InputTag.QuickItem.2");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_QuickItem_3, "InputTag.QuickItem.3");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_QuickItem_4, "InputTag.QuickItem.4");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_QuickItem_5, "InputTag.QuickItem.5");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_QuickItem_6, "InputTag.QuickItem.6");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_QuickItem_7, "InputTag.QuickItem.7");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_QuickItem_8, "InputTag.QuickItem.8");

	UE_DEFINE_GAMEPLAY_TAG(InputTag_PickUp_Items, "InputTag.PickUp.Items");

	UE_DEFINE_GAMEPLAY_TAG(InputTag_MustBeHeld, "InputTag.MustBeHeld");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_MustBeHeld_Block, "InputTag.MustBeHeld.Block");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_MustBeHeld_Charge, "InputTag.MustBeHeld.Charge");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_MustBeHeld_Charge_IdentitySkill, "InputTag.MustBeHeld.Charge.IdentitySkill");

	UE_DEFINE_GAMEPLAY_TAG(InputTag_Toggleable, "InputTag.Toggleadble");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_Toggleable_TargetLock, "InputTag.Toggleadble.TargetLock");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_Toggleable_Rage, "InputTag.Toggleadble.Rage");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_Toggleable_ToggleSkill, "InputTag.Toggleadble.ToggleSkill");
}
