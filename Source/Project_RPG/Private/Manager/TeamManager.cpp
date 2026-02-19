// Fill out your copyright notice in the Description page of Project Settings.

#include "Manager/TeamManager.h"
#include "Interface/RPGTeamAgentInterface.h"
#include "Actor/RPGTeamInfoBase.h"
#include "DataAsset/Team/RPGTeamDisplayAsset.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerState.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TeamManager)

UTeamManager::UTeamManager()
{
}

void UTeamManager::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// 기본 팀들 초기화
	TeamMap.FindOrAdd(ERPGTeamID::Player);
	TeamMap.FindOrAdd(ERPGTeamID::Enemy);
	TeamMap.FindOrAdd(ERPGTeamID::Neutral);
}

void UTeamManager::Deinitialize()
{
	Super::Deinitialize();
}

bool UTeamManager::RegisterTeamInfo(ARPGTeamInfoBase* InTeamInfo)
{
	if (!ensure(InTeamInfo)) return false;

	const ERPGTeamID TeamId = InTeamInfo->GetTeamId();
	if (ensure(TeamId != ERPGTeamID::NoTeam))
	{
		FRPGTeamTrackingInfo& Entry = TeamMap.FindOrAdd(TeamId);
		Entry.TeamInfo = InTeamInfo;

		URPGTeamDisplayAsset* OldAsset = Entry.DisplayAsset;
		Entry.DisplayAsset = InTeamInfo->GetDisplayAsset();

		if (OldAsset != Entry.DisplayAsset)
		{
			Entry.OnTeamDisplayAssetChanged.Broadcast(Entry.DisplayAsset);
		}

		return true;
	}

	return false;
}

void UTeamManager::UnregisterTeamInfo(ARPGTeamInfoBase* InTeamInfo)
{
	if (!InTeamInfo) return;

	const ERPGTeamID TeamId = InTeamInfo->GetTeamId();
	if (FRPGTeamTrackingInfo* Entry = TeamMap.Find(TeamId))
	{
		if (Entry->TeamInfo == InTeamInfo)
		{
			Entry->TeamInfo = nullptr;
		}
	}
}

URPGTeamDisplayAsset* UTeamManager::GetTeamDisplayAsset(ERPGTeamID TeamId) const
{
	if (const FRPGTeamTrackingInfo* Entry = TeamMap.Find(TeamId))
	{
		return Entry->DisplayAsset;
	}
	return nullptr;
}

URPGTeamDisplayAsset* UTeamManager::GetDisplayAssetFromObject(const UObject* TestObject) const
{
	const int32 TeamInt = FindTeamFromObject(TestObject);
	return GetTeamDisplayAsset(static_cast<ERPGTeamID>(TeamInt));
}

FOnRPGTeamDisplayAssetChangedDelegate& UTeamManager::GetTeamDisplayAssetChangedDelegate(ERPGTeamID TeamId)
{
	return TeamMap.FindOrAdd(TeamId).OnTeamDisplayAssetChanged;
}

bool UTeamManager::ChangeTeamForActor(AActor* ActorToChange, ERPGTeamID NewTeamId)
{
	if (!ActorToChange || !ActorToChange->HasAuthority()) return false;

	const FGenericTeamId NewTeamID = RPGEnumToGenericTeamId(NewTeamId);

	// 인터페이스 구현 여부 확인
	if (IRPGTeamAgentInterface* TeamAgent = Cast<IRPGTeamAgentInterface>(ActorToChange))
	{
		TeamAgent->SetGenericTeamId(NewTeamID);
		return true;
	}

	// PlayerState를 통한 변경 시도
	APawn* Pawn = Cast<APawn>(ActorToChange);
	if (Pawn)
	{
		if (IRPGTeamAgentInterface* PSTeamAgent = Pawn->GetPlayerState<IRPGTeamAgentInterface>())
		{
			PSTeamAgent->SetGenericTeamId(NewTeamID);
			return true;
		}
	}

	return false;
}

int32 UTeamManager::FindTeamFromObject(const UObject* TestObject) const
{
	if (!TestObject) return 255;

	// 1. 직접 인터페이스를 구현하고 있는지 확인
	if (const IRPGTeamAgentInterface* TeamInterface = Cast<IRPGTeamAgentInterface>(TestObject))
	{
		return RPGGenericTeamIdToInteger(TeamInterface->GetGenericTeamId());
	}

	// 2. 액터인 경우 소유주나 인스티게이터 확인
	if (const AActor* TestActor = Cast<const AActor>(TestObject))
	{
		if (const IRPGTeamAgentInterface* InstigatorTeam = Cast<IRPGTeamAgentInterface>(TestActor->GetInstigator()))
		{
			return RPGGenericTeamIdToInteger(InstigatorTeam->GetGenericTeamId());
		}

		if (const IRPGTeamAgentInterface* OwnerTeam = Cast<IRPGTeamAgentInterface>(TestActor->GetOwner()))
		{
			return RPGGenericTeamIdToInteger(OwnerTeam->GetGenericTeamId());
		}

		// 3. Pawn인 경우 PlayerState 또는 Controller 확인
		if (const APawn* Pawn = Cast<const APawn>(TestActor))
		{
			if (const IRPGTeamAgentInterface* ControllerTeam = Cast<IRPGTeamAgentInterface>(Pawn->GetController()))
			{
				return RPGGenericTeamIdToInteger(ControllerTeam->GetGenericTeamId());
			}

			if (const IRPGTeamAgentInterface* PSTeam = Pawn->GetPlayerState<IRPGTeamAgentInterface>())
			{
				return RPGGenericTeamIdToInteger(PSTeam->GetGenericTeamId());
			}
		}
	}

	return 255; // NoTeam
}

ERPGTeamComparison UTeamManager::CompareTeams(const UObject* A, const UObject* B) const
{
	const int32 TeamA = FindTeamFromObject(A);
	const int32 TeamB = FindTeamFromObject(B);

	if (TeamA == 255 || TeamB == 255)
	{
		return ERPGTeamComparison::InvalidArgument;
	}

	return (TeamA == TeamB) ? ERPGTeamComparison::OnSameTeam : ERPGTeamComparison::DifferentTeams;
}

bool UTeamManager::CanCauseDamage(const UObject* Instigator, const UObject* Target, bool bAllowDamageToSelf) const
{
	if (!Instigator || !Target) return false;

	if (Instigator == Target) return bAllowDamageToSelf;

	const ERPGTeamComparison Relationship = CompareTeams(Instigator, Target);

	// 다른 팀이면 데미지 가능
	if (Relationship == ERPGTeamComparison::DifferentTeams)
	{
		return true;
	}

	// 팀 정보가 없는 대상(예: 기믹, 파괴 가능 오브젝트)에 대해서는 데미지 허용
	if (Relationship == ERPGTeamComparison::InvalidArgument)
	{
		return true;
	}

	// 같은 팀이면 데미지 불가 (Friendly Fire Off)
	return false;
}

bool UTeamManager::DoesTeamExist(ERPGTeamID TeamId) const
{
	return TeamMap.Contains(TeamId);
}

TArray<ERPGTeamID> UTeamManager::GetTeamIDs() const
{
	TArray<ERPGTeamID> Result;
	TeamMap.GenerateKeyArray(Result);
	return Result;
}
