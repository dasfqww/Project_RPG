// Fill out your copyright notice in the Description page of Project Settings.

#include "RPGSharedTags.h"

namespace RPGGameplayTags
{
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

	/* UI Widget Tags */
	UE_DEFINE_GAMEPLAY_TAG(Widget_Layer_Game, "Widget.Layer.Game");
	UE_DEFINE_GAMEPLAY_TAG(Widget_Layer_GameMenu, "Widget.Layer.GameMenu");
	UE_DEFINE_GAMEPLAY_TAG(Widget_Layer_Menu, "Widget.Layer.MenuLayer");
	UE_DEFINE_GAMEPLAY_TAG(Widget_Layer_Modal, "Widget.Layer.Modal");
}
