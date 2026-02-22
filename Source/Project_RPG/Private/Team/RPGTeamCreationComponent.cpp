// Fill out your copyright notice in the Description page of Project Settings.


#include "Team/RPGTeamCreationComponent.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

URPGTeamCreationComponent::URPGTeamCreationComponent(const FObjectInitializer& ObjectInitializer)
	:Super(ObjectInitializer)
{
}

void URPGTeamCreationComponent::BeginPlay()
{
}

void URPGTeamCreationComponent::ServerCreatePartyTeam(int32 PartyId, URPGTeamDisplayAsset* DisplayAsset)
{
}

void URPGTeamCreationComponent::ServerCreateFactionTeam(int32 FactionId, URPGTeamDisplayAsset* DisplayAsset)
{
}

void URPGTeamCreationComponent::ServerCreateTeams()
{
}

void URPGTeamCreationComponent::OnPlayerInitialized(AGameModeBase* GameMode, AController* NewPlayer)
{
}

void URPGTeamCreationComponent::ServerCreateTeam(int32 TeamId, URPGTeamDisplayAsset* DisplayAsset)
{
}
