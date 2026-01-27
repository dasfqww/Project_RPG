// Fill out your copyright notice in the Description page of Project Settings.


#include "Type/RPGStructTypes.h"
#include "Ability/RPGGameplayAbility.h"

bool FRPGPlayerAbilitySet::IsValid() const
{
	return InputTag.IsValid() && AbilityToGrant;
}
