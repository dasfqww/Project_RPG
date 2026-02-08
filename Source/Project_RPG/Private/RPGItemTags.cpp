// Fill out your copyright notice in the Description page of Project Settings.

#include "RPGItemTags.h"

namespace RPGGameplayTags
{
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
}
