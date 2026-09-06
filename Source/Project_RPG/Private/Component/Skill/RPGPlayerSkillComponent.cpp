#include "Component/Skill/RPGPlayerSkillComponent.h"

#include "Engine/GameInstance.h"
#include "Net/UnrealNetwork.h"
#include "RPGDebugHelper.h"
#include "Skill/RPGSkillCatalogSubsystem.h"
#include "Skill/RPGSkillDefinition.h"

URPGPlayerSkillComponent::URPGPlayerSkillComponent()
{
	SetIsReplicatedByDefault(true);
}

void URPGPlayerSkillComponent::BeginPlay()
{
	Super::BeginPlay();
	UWorld* World = GetWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	if (URPGSkillCatalogSubsystem* Catalog = GameInstance
		? GameInstance->GetSubsystem<URPGSkillCatalogSubsystem>()
		: nullptr)
	{
		Catalog->RequestSkillDefinitions();
	}
}

void URPGPlayerSkillComponent::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(
		URPGPlayerSkillComponent,
		ReplicatedSkillData,
		COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(
		URPGPlayerSkillComponent,
		ReplicatedSkillDataRevision,
		COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(
		URPGPlayerSkillComponent,
		TotalSP,
		COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(
		URPGPlayerSkillComponent,
		UsedSP,
		COND_OwnerOnly);
}

bool URPGPlayerSkillComponent::TryLevelUpSkill(const FGameplayTag SkillTag)
{
	const bool bSucceeded = ApplyLevelUpSkill(SkillTag);
	if (!IsAuthorityOwner() && bSucceeded)
	{
		ServerTryLevelUpSkill(SkillTag);
	}
	return bSucceeded;
}

bool URPGPlayerSkillComponent::ApplyLevelUpSkill(
	const FGameplayTag SkillTag)
{
	if (!SkillTag.IsValid())
	{
		return false;
	}
	const URPGSkillDefinition* Definition = FindSkillDefinition(SkillTag);
	if (!Definition)
	{
		return false;
	}

	FRPGSkillSaveData& Data = SkillDataMap.FindOrAdd(SkillTag);
	Definition->NormalizeSaveData(Data);
	if (Data.SkillLevel >= Definition->MaxSkillLevel)
	{
		return false;
	}

	const int32 NextLevel = Data.SkillLevel + 1;
	const int32 Cost = GetRequiredSPForLevel(NextLevel);
	if (GetRemainingSP() < Cost)
	{
		Debug::Print(TEXT("Not enough SP for Level "), NextLevel);
		return false;
	}

	Data.SkillLevel = NextLevel;
	UsedSP += Cost;
	PublishAuthoritativeSkillData(SkillTag);
	OnSkillDataChanged.Broadcast(SkillTag);
	Debug::Print(
		SkillTag.ToString() + TEXT(" Level Up! Current: "),
		Data.SkillLevel);
	return true;
}

bool URPGPlayerSkillComponent::TryLevelDownSkill(
	const FGameplayTag SkillTag)
{
	const bool bSucceeded = ApplyLevelDownSkill(SkillTag);
	if (!IsAuthorityOwner() && bSucceeded)
	{
		ServerTryLevelDownSkill(SkillTag);
	}
	return bSucceeded;
}

bool URPGPlayerSkillComponent::ApplyLevelDownSkill(
	const FGameplayTag SkillTag)
{
	const URPGSkillDefinition* Definition = FindSkillDefinition(SkillTag);
	FRPGSkillSaveData* Data = SkillDataMap.Find(SkillTag);
	if (!Definition || !Data)
	{
		return false;
	}
	Definition->NormalizeSaveData(*Data);
	if (Data->SkillLevel <= 1)
	{
		return false;
	}

	const int32 Refund = GetRequiredSPForLevel(Data->SkillLevel);
	--Data->SkillLevel;
	UsedSP = FMath::Max(0, UsedSP - Refund);
	Definition->NormalizeSaveData(*Data);

	PublishAuthoritativeSkillData(SkillTag);
	OnSkillDataChanged.Broadcast(SkillTag);
	return true;
}

void URPGPlayerSkillComponent::LevelUpToMax(
	const FGameplayTag SkillTag,
	const int32 TargetGoalLevel)
{
	ApplyLevelUpToMax(SkillTag, TargetGoalLevel);
	if (!IsAuthorityOwner())
	{
		ServerLevelUpToMax(SkillTag, TargetGoalLevel);
	}
}

void URPGPlayerSkillComponent::ApplyLevelUpToMax(
	const FGameplayTag SkillTag,
	const int32 TargetGoalLevel)
{
	if (!SkillTag.IsValid())
	{
		return;
	}
	const URPGSkillDefinition* Definition = FindSkillDefinition(SkillTag);
	if (!Definition)
	{
		return;
	}

	const int32 ClampedGoalLevel =
		Definition->ClampSkillLevel(TargetGoalLevel);
	FRPGSkillSaveData& Data = SkillDataMap.FindOrAdd(SkillTag);
	Definition->NormalizeSaveData(Data);
	while (Data.SkillLevel < ClampedGoalLevel && ApplyLevelUpSkill(SkillTag))
	{
	}
	while (Data.SkillLevel > ClampedGoalLevel && ApplyLevelDownSkill(SkillTag))
	{
	}
}

void URPGPlayerSkillComponent::ResetSkillLevel(
	const FGameplayTag SkillTag)
{
	ApplyResetSkillLevel(SkillTag);
	if (!IsAuthorityOwner())
	{
		ServerResetSkillLevel(SkillTag);
	}
}

void URPGPlayerSkillComponent::ApplyResetSkillLevel(
	const FGameplayTag SkillTag)
{
	while (ApplyLevelDownSkill(SkillTag))
	{
	}
}

bool URPGPlayerSkillComponent::SelectTripod(
	const FGameplayTag SkillTag,
	const int32 TierIndex,
	const int32 OptionIndex)
{
	const bool bSucceeded = ApplySelectTripod(
		SkillTag,
		TierIndex,
		OptionIndex);
	if (!IsAuthorityOwner() && bSucceeded)
	{
		ServerSelectTripod(SkillTag, TierIndex, OptionIndex);
	}
	return bSucceeded;
}

bool URPGPlayerSkillComponent::ApplySelectTripod(
	const FGameplayTag SkillTag,
	const int32 TierIndex,
	const int32 OptionIndex)
{
	if (!SkillTag.IsValid() || OptionIndex < INDEX_NONE)
	{
		return false;
	}
	const URPGSkillDefinition* Definition = FindSkillDefinition(SkillTag);
	if (!Definition)
	{
		return false;
	}

	FRPGSkillSaveData* Data = SkillDataMap.Find(SkillTag);
	if (!Data)
	{
		return false;
	}
	Definition->NormalizeSaveData(*Data);
	if (!Definition->IsTripodSelectionAllowed(
		Data->SkillLevel,
		TierIndex,
		OptionIndex))
	{
		Debug::Print(TEXT("Skill Level too low for Tier "), TierIndex + 1);
		return false;
	}

	if (Data->SelectedTripodIndices[TierIndex] == OptionIndex)
	{
		return false;
	}
	Data->SelectedTripodIndices[TierIndex] = OptionIndex;
	PublishAuthoritativeSkillData(SkillTag);
	OnSkillDataChanged.Broadcast(SkillTag);
	Debug::Print(TEXT("Tripod Selected! Tier: "), TierIndex + 1);
	return true;
}

FRPGSkillSaveData URPGPlayerSkillComponent::GetSkillSaveData(
	const FGameplayTag SkillTag) const
{
	if (const FRPGSkillSaveData* Data = SkillDataMap.Find(SkillTag))
	{
		return *Data;
	}
	return FRPGSkillSaveData();
}

int32 URPGPlayerSkillComponent::GetRequiredSPForLevel(
	const int32 TargetLevel) const
{
	if (TargetLevel <= 4)
	{
		return 1;
	}
	if (TargetLevel <= 7)
	{
		return 2;
	}
	if (TargetLevel <= 10)
	{
		return 4;
	}
	return 6;
}

void URPGPlayerSkillComponent::AddTotalSP(const int32 Amount)
{
	if (!IsAuthorityOwner() || Amount <= 0)
	{
		return;
	}
	TotalSP = FMath::Max(0, TotalSP + Amount);
	TouchAuthoritativeRevision();
	OnSkillDataChanged.Broadcast(FGameplayTag());
}

void URPGPlayerSkillComponent::ServerTryLevelUpSkill_Implementation(
	const FGameplayTag SkillTag)
{
	ApplyLevelUpSkill(SkillTag);
	TouchAuthoritativeRevision();
}

void URPGPlayerSkillComponent::ServerTryLevelDownSkill_Implementation(
	const FGameplayTag SkillTag)
{
	ApplyLevelDownSkill(SkillTag);
	TouchAuthoritativeRevision();
}

void URPGPlayerSkillComponent::ServerLevelUpToMax_Implementation(
	const FGameplayTag SkillTag,
	const int32 TargetGoalLevel)
{
	ApplyLevelUpToMax(SkillTag, TargetGoalLevel);
	TouchAuthoritativeRevision();
}

void URPGPlayerSkillComponent::ServerResetSkillLevel_Implementation(
	const FGameplayTag SkillTag)
{
	ApplyResetSkillLevel(SkillTag);
	TouchAuthoritativeRevision();
}

void URPGPlayerSkillComponent::ServerSelectTripod_Implementation(
	const FGameplayTag SkillTag,
	const int32 TierIndex,
	const int32 OptionIndex)
{
	ApplySelectTripod(SkillTag, TierIndex, OptionIndex);
	TouchAuthoritativeRevision();
}

void URPGPlayerSkillComponent::OnRep_ReplicatedSkillData()
{
	TMap<FGameplayTag, FRPGSkillSaveData> PreviousData = SkillDataMap;
	SkillDataMap.Reset();
	for (const FRPGReplicatedSkillSaveEntry& Entry : ReplicatedSkillData)
	{
		if (Entry.SkillTag.IsValid())
		{
			SkillDataMap.Add(Entry.SkillTag, Entry.SaveData);
		}
	}

	for (const TPair<FGameplayTag, FRPGSkillSaveData>& Entry : SkillDataMap)
	{
		const FRPGSkillSaveData* Previous = PreviousData.Find(Entry.Key);
		if (!Previous || Previous->SkillLevel != Entry.Value.SkillLevel ||
			Previous->SelectedTripodIndices !=
				Entry.Value.SelectedTripodIndices)
		{
			OnSkillDataChanged.Broadcast(Entry.Key);
		}
		PreviousData.Remove(Entry.Key);
	}
	for (const TPair<FGameplayTag, FRPGSkillSaveData>& Removed : PreviousData)
	{
		OnSkillDataChanged.Broadcast(Removed.Key);
	}

	// The shared revision also represents TotalSP/UsedSP changes. An invalid
	// tag is the explicit signal for consumers to refresh aggregate state.
	OnSkillDataChanged.Broadcast(FGameplayTag());
}

void URPGPlayerSkillComponent::PublishAuthoritativeSkillData(
	const FGameplayTag SkillTag)
{
	if (!IsAuthorityOwner())
	{
		return;
	}

	const FRPGSkillSaveData* SaveData = SkillDataMap.Find(SkillTag);
	if (!SaveData)
	{
		return;
	}

	FRPGReplicatedSkillSaveEntry* Entry =
		ReplicatedSkillData.FindByPredicate(
			[SkillTag](const FRPGReplicatedSkillSaveEntry& Candidate)
			{
				return Candidate.SkillTag == SkillTag;
			});
	if (!Entry)
	{
		Entry = &ReplicatedSkillData.AddDefaulted_GetRef();
		Entry->SkillTag = SkillTag;
	}
	Entry->SaveData = *SaveData;
}

void URPGPlayerSkillComponent::TouchAuthoritativeRevision()
{
	if (!IsAuthorityOwner())
	{
		return;
	}
	++ReplicatedSkillDataRevision;
	if (AActor* Owner = GetOwner())
	{
		Owner->ForceNetUpdate();
	}
}

bool URPGPlayerSkillComponent::IsAuthorityOwner() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}

const URPGSkillDefinition*
URPGPlayerSkillComponent::FindSkillDefinition(
	const FGameplayTag SkillTag) const
{
	if (!SkillTag.IsValid())
	{
		return nullptr;
	}

	UWorld* World = GetWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const URPGSkillCatalogSubsystem* Catalog = GameInstance
		? GameInstance->GetSubsystem<URPGSkillCatalogSubsystem>()
		: nullptr;
	return Catalog ? Catalog->FindSkillDefinition(SkillTag) : nullptr;
}
