#include "Component/Skill/RPGPlayerSkillComponent.h"

#include "Engine/GameInstance.h"
#include "Net/UnrealNetwork.h"
#include "RPGDebugHelper.h"
#include "PlayerClass/RPGPlayerClassDefinition.h"
#include "Security/RPGDungeonGameplayAuthority.h"
#include "Skill/RPGClassSkillEligibility.h"
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
	DOREPLIFETIME_CONDITION(URPGPlayerSkillComponent, SkillAccessState, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(
		URPGPlayerSkillComponent,
		TotalSP,
		COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(
		URPGPlayerSkillComponent,
		UsedSP,
		COND_OwnerOnly);
}

void URPGPlayerSkillComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bEndingPlay = true;
	++SkillAccessState.RestoreGeneration;
	SkillAccessState.LoadState = ERPGPlayerSkillLoadState::Failed;
	Super::EndPlay(EndPlayReason);
}

uint32 URPGPlayerSkillComponent::BeginClassSkillRestore(const FPrimaryAssetId ExpectedClassId)
{
	if (!IsAuthorityOwner() || bEndingPlay || IsDungeonGameplayRevoked(GetOwner()) || !ExpectedClassId.IsValid()
		|| ExpectedClassId.PrimaryAssetType != URPGPlayerClassDefinition::PrimaryAssetType
		|| SkillAccessState.LoadState == ERPGPlayerSkillLoadState::Ready) return 0;
	++SkillAccessState.RestoreGeneration;
	if (SkillAccessState.RestoreGeneration == 0) ++SkillAccessState.RestoreGeneration;
	SkillAccessState.ClassDefinition = nullptr;
	SkillAccessState.ExpectedClassId = ExpectedClassId;
	SkillAccessState.LoadState = ERPGPlayerSkillLoadState::Loading;
	PredictedUsedSP = INDEX_NONE;
	const uint32 RequestId = SkillAccessState.RestoreGeneration;
	TouchAuthoritativeRevision();
	OnSkillDataChanged.Broadcast(FGameplayTag());
	return RequestId;
}

bool URPGPlayerSkillComponent::CompleteClassSkillRestore(const uint32 RequestId,
	URPGPlayerClassDefinition* Definition, const FRPGPlayerSkillRestoreData& Data, FText& OutError)
{
	OutError = FText::GetEmpty();
	if (!IsAuthorityOwner() || bEndingPlay || IsDungeonGameplayRevoked(GetOwner())
		|| RequestId == 0 || RequestId != SkillAccessState.RestoreGeneration
		|| SkillAccessState.LoadState != ERPGPlayerSkillLoadState::Loading)
	{
		OutError = FText::FromString(TEXT("The skill restore request is no longer current."));
		return false;
	}
	const auto Fail = [this, RequestId, &OutError](const TCHAR* Message)
	{
		OutError = FText::FromString(Message);
		FailClassSkillRestore(RequestId);
		return false;
	};
	if (Data.Version != FRPGPlayerSkillRestoreData::CurrentVersion || Data.TotalSP < 0
		|| !IsValid(Definition) || Data.ClassId != SkillAccessState.ExpectedClassId
		|| Definition->GetPrimaryAssetId() != Data.ClassId)
		return Fail(TEXT("Skill restore version, class identity or SP is invalid."));
	if (!Definition->ValidateConfiguration(OutError))
	{
		FailClassSkillRestore(RequestId);
		return false;
	}

	// Validate the entire input and recalculate SP before replacing any growth record.
	TMap<FGameplayTag, FRPGSkillSaveData> Restored;
	TArray<FRPGReplicatedSkillSaveEntry> Entries;
	int64 RestoredUsedSP = 0;
	for (const FRPGReplicatedSkillSaveEntry& Input : Data.AcquiredSkills)
	{
		const auto* Binding = Definition->FindSkill(Input.SkillTag);
		const auto* Skill = Binding ? Binding->Definition.Get() : nullptr;
		if (!Skill || Restored.Contains(Input.SkillTag) || Input.SaveData.SkillLevel < 1
			|| Input.SaveData.SkillLevel > Skill->MaxSkillLevel)
			return Fail(TEXT("Acquired skills must be unique class members with valid levels."));
		// Unknown content/invalid selections need an explicit migration, never silent refund/defaulting.
		for (int32 Tier = 0; Tier < Input.SaveData.SelectedTripodIndices.Num(); ++Tier)
		{
			const int32 Option = Input.SaveData.SelectedTripodIndices[Tier];
			if (Option != INDEX_NONE && !Skill->IsTripodSelectionAllowed(Input.SaveData.SkillLevel, Tier, Option))
				return Fail(TEXT("Saved tripod selections require content migration."));
		}
		for (int32 Level = 2; Level <= Input.SaveData.SkillLevel; ++Level)
		{
			RestoredUsedSP += GetRequiredSPForLevel(Level);
			if (RestoredUsedSP > Data.TotalSP) return Fail(TEXT("Restored skill costs exceed the server SP budget."));
		}
		FRPGReplicatedSkillSaveEntry& Entry = Entries.AddDefaulted_GetRef();
		Entry = Input;
		Entry.RestoreGeneration = RequestId; // Never accept a caller's generation.
		Skill->NormalizeSaveData(Entry.SaveData);
		Restored.Add(Entry.SkillTag, Entry.SaveData);
	}
	SkillDataMap = MoveTemp(Restored);
	ReplicatedSkillData = MoveTemp(Entries);
	TotalSP = Data.TotalSP;
	UsedSP = static_cast<int32>(RestoredUsedSP);
	PredictedUsedSP = INDEX_NONE;
	SkillAccessState.ClassDefinition = Definition;
	SkillAccessState.bIdentityUnlocked = Data.bIdentityUnlocked;
	SkillAccessState.bConfigurationMutable = Data.bConfigurationMutable;
	SkillAccessState.LoadState = ERPGPlayerSkillLoadState::Ready;
	TouchAuthoritativeRevision();
	OnSkillDataChanged.Broadcast(FGameplayTag());
	return true;
}

void URPGPlayerSkillComponent::FailClassSkillRestore(const uint32 RequestId)
{
	if (!IsAuthorityOwner() || RequestId == 0 || RequestId != SkillAccessState.RestoreGeneration
		|| SkillAccessState.LoadState != ERPGPlayerSkillLoadState::Loading) return;
	SkillAccessState.LoadState = ERPGPlayerSkillLoadState::Failed;
	TouchAuthoritativeRevision();
	OnSkillDataChanged.Broadcast(FGameplayTag());
}

void URPGPlayerSkillComponent::InvalidateClassSkillRestore(const uint32 RequestId)
{
	if (!IsAuthorityOwner() || bEndingPlay || !RequestId || RequestId != SkillAccessState.RestoreGeneration) return;
	SkillAccessState.LoadState = ERPGPlayerSkillLoadState::Failed;
	SkillAccessState.ClassDefinition = nullptr;
	TouchAuthoritativeRevision();
	OnSkillDataChanged.Broadcast(FGameplayTag());
}

bool URPGPlayerSkillComponent::IsClassIdentityUnlocked() const
{
	return GetPlayerClassDefinition() && SkillAccessState.bIdentityUnlocked;
}

URPGPlayerClassDefinition* URPGPlayerSkillComponent::GetPlayerClassDefinition() const
{
	URPGPlayerClassDefinition* Definition = SkillAccessState.ClassDefinition;
	return !bEndingPlay && !IsDungeonGameplayRevoked(GetOwner())
		&& SkillAccessState.LoadState == ERPGPlayerSkillLoadState::Ready && IsValid(Definition)
		&& Definition->GetPrimaryAssetId() == SkillAccessState.ExpectedClassId ? Definition : nullptr;
}

bool URPGPlayerSkillComponent::IsSkillInCurrentClass(const FGameplayTag SkillTag) const
{
	const auto* Definition = GetPlayerClassDefinition();
	return Definition && Definition->FindSkill(SkillTag);
}

bool URPGPlayerSkillComponent::IsSkillAcquired(const FGameplayTag SkillTag) const
{
	if (!IsSkillInCurrentClass(SkillTag)) return false;
	return ReplicatedSkillData.ContainsByPredicate([this, SkillTag](const FRPGReplicatedSkillSaveEntry& Entry)
	{
		return Entry.SkillTag == SkillTag && Entry.RestoreGeneration == SkillAccessState.RestoreGeneration
			&& Entry.SaveData.SkillLevel >= 1;
	});
}

bool URPGPlayerSkillComponent::TryGetSkillSaveData(const FGameplayTag SkillTag, FRPGSkillSaveData& OutData) const
{
	OutData = FRPGSkillSaveData();
	OutData.SkillLevel = 0; // UI fallback is never acquisition evidence.
	const auto* Data = IsSkillAcquired(SkillTag) ? SkillDataMap.Find(SkillTag) : nullptr;
	if (!Data) return false;
	OutData = *Data;
	return true;
}

bool URPGPlayerSkillComponent::CanModifySkill(const FGameplayTag SkillTag) const
{
	return IsSkillAcquired(SkillTag) && RPGClassSkillEligibility::CanChangeConfiguration(GetOwner());
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
	if (!CanModifySkill(SkillTag))
	{
		return false;
	}
	const URPGSkillDefinition* Definition = FindSkillDefinition(SkillTag);
	if (!Definition)
	{
		return false;
	}

	FRPGSkillSaveData* ExistingData = SkillDataMap.Find(SkillTag);
	if (!ExistingData) return false;
	FRPGSkillSaveData& Data = *ExistingData;
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
	SetEffectiveUsedSP(GetEffectiveUsedSP() + Cost);
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
	if (!CanModifySkill(SkillTag)) return false;
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
	SetEffectiveUsedSP(GetEffectiveUsedSP() - Refund);
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
	if (!CanModifySkill(SkillTag))
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
	FRPGSkillSaveData* ExistingData = SkillDataMap.Find(SkillTag);
	if (!ExistingData) return;
	FRPGSkillSaveData& Data = *ExistingData;
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
	if (!CanModifySkill(SkillTag) || OptionIndex < INDEX_NONE)
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
	FRPGSkillSaveData Data;
	TryGetSkillSaveData(SkillTag, Data);
	return Data;
}

int32 URPGPlayerSkillComponent::GetRemainingSP() const
{
	return TotalSP - GetEffectiveUsedSP();
}

int32 URPGPlayerSkillComponent::GetEffectiveUsedSP() const
{
	return !IsAuthorityOwner() && PredictedUsedSP != INDEX_NONE
		? PredictedUsedSP : UsedSP;
}

void URPGPlayerSkillComponent::SetEffectiveUsedSP(const int32 Amount)
{
	if (IsAuthorityOwner())
	{
		UsedSP = FMath::Max(0, Amount);
	}
	else
	{
		PredictedUsedSP = FMath::Max(0, Amount);
	}
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
	if (!IsAuthorityOwner() || Amount <= 0 || !IsConfigurationMutable())
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
	if (!IsAuthorityOwner()) return;
	ApplyLevelUpSkill(SkillTag);
	TouchAuthoritativeRevision();
}

void URPGPlayerSkillComponent::ServerTryLevelDownSkill_Implementation(
	const FGameplayTag SkillTag)
{
	if (!IsAuthorityOwner()) return;
	ApplyLevelDownSkill(SkillTag);
	TouchAuthoritativeRevision();
}

void URPGPlayerSkillComponent::ServerLevelUpToMax_Implementation(
	const FGameplayTag SkillTag,
	const int32 TargetGoalLevel)
{
	if (!IsAuthorityOwner()) return;
	ApplyLevelUpToMax(SkillTag, TargetGoalLevel);
	TouchAuthoritativeRevision();
}

void URPGPlayerSkillComponent::ServerResetSkillLevel_Implementation(
	const FGameplayTag SkillTag)
{
	if (!IsAuthorityOwner()) return;
	ApplyResetSkillLevel(SkillTag);
	TouchAuthoritativeRevision();
}

void URPGPlayerSkillComponent::ServerSelectTripod_Implementation(
	const FGameplayTag SkillTag,
	const int32 TierIndex,
	const int32 OptionIndex)
{
	if (!IsAuthorityOwner()) return;
	ApplySelectTripod(SkillTag, TierIndex, OptionIndex);
	TouchAuthoritativeRevision();
}

void URPGPlayerSkillComponent::OnRep_ReplicatedSkillData()
{
	// Reconcile the whole predicted view before notifying UI consumers, even
	// when only the revision changed because the server rejected a request.
	PredictedUsedSP = INDEX_NONE;
	TMap<FGameplayTag, FRPGSkillSaveData> PreviousData = SkillDataMap;
	SkillDataMap.Reset();
	for (const FRPGReplicatedSkillSaveEntry& Entry : ReplicatedSkillData)
	{
		if (Entry.SkillTag.IsValid() && Entry.RestoreGeneration == SkillAccessState.RestoreGeneration
			&& IsSkillInCurrentClass(Entry.SkillTag))
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
	Entry->RestoreGeneration = SkillAccessState.RestoreGeneration;
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
	const auto* Definition = GetPlayerClassDefinition();
	const auto* Binding = Definition ? Definition->FindSkill(SkillTag) : nullptr;
	return Binding ? Binding->Definition.Get() : nullptr;
}
