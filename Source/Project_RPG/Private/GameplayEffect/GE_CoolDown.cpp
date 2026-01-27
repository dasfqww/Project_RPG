// Fill out your copyright notice in the Description page of Project Settings.


#include "GameplayEffect/GE_CoolDown.h"

UGE_CoolDown::UGE_CoolDown()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	
	/*FSetByCallerFloat SetByCaller;
	SetByCaller.DataTag = FGameplayTag::RequestGameplayTag(FName("Player.Ability.Skill.CoolDown"));
	DurationMagnitude = FGameplayEffectModifierMagnitude(SetByCaller);*/
}
