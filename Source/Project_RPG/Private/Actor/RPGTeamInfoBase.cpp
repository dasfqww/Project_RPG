// Fill out your copyright notice in the Description page of Project Settings.

#include "Actor/RPGTeamInfoBase.h"
#include "Net/UnrealNetwork.h"
#include "Manager/TeamManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGTeamInfoBase)

ARPGTeamInfoBase::ARPGTeamInfoBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, TeamId(ERPGTeamID::NoTeam)
{
	bReplicates = true;
	bAlwaysRelevant = true;
	NetPriority = 3.0f;
	SetReplicatingMovement(false);
}

void ARPGTeamInfoBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ARPGTeamInfoBase, TeamTags);
	DOREPLIFETIME_CONDITION(ARPGTeamInfoBase, TeamId, COND_InitialOnly);
}

void ARPGTeamInfoBase::BeginPlay()
{
	Super::BeginPlay();

	TryRegisterWithTeamSubsystem();
}

void ARPGTeamInfoBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (TeamId != ERPGTeamID::NoTeam)
	{
		if (UWorld* World = GetWorld())
		{
			if (UTeamManager* TeamManager = World->GetSubsystem<UTeamManager>())
			{
				TeamManager->UnregisterTeamInfo(this);
			}
		}
	}

	Super::EndPlay(EndPlayReason);
}

void ARPGTeamInfoBase::RegisterWithTeamSubsystem(UTeamManager* Subsystem)
{
	if (Subsystem)
	{
		Subsystem->RegisterTeamInfo(this);
	}
}

void ARPGTeamInfoBase::TryRegisterWithTeamSubsystem()
{
	if (TeamId != ERPGTeamID::NoTeam)
	{
		if (UWorld* World = GetWorld())
		{
			if (UTeamManager* TeamManager = World->GetSubsystem<UTeamManager>())
			{
				RegisterWithTeamSubsystem(TeamManager);
			}
		}
	}
}

void ARPGTeamInfoBase::SetTeamId(ERPGTeamID NewTeamId)
{
	check(HasAuthority());
	check(TeamId == ERPGTeamID::NoTeam);
	check(NewTeamId != ERPGTeamID::NoTeam);

	TeamId = NewTeamId;

	TryRegisterWithTeamSubsystem();
}

void ARPGTeamInfoBase::OnRep_TeamId()
{
	TryRegisterWithTeamSubsystem();
}
