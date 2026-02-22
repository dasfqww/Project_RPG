// Fill out your copyright notice in the Description page of Project Settings.


#include "Team/RPGTeamPublicInfo.h"
#include "Net/UnrealNetwork.h"
#include "Actor/RPGTeamInfoBase.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGTeamPublicInfo)

ARPGTeamPublicInfo::ARPGTeamPublicInfo(const FObjectInitializer& ObjectInitializer)
	:Super(ObjectInitializer)
{
	
}

void ARPGTeamPublicInfo::GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(ThisClass, TeamDisplayAsset, COND_InitialOnly);
}

void ARPGTeamPublicInfo::OnRep_TeamDisplayAsset()
{
	TryRegisterWithTeamSubsystem();
}

void ARPGTeamPublicInfo::SetTeamDisplayAsset(TObjectPtr<URPGTeamDisplayAsset> NewDisplayAsset)
{
	check(HasAuthority());
	check(TeamDisplayAsset == nullptr);

	TeamDisplayAsset = NewDisplayAsset;

	TryRegisterWithTeamSubsystem();
}
